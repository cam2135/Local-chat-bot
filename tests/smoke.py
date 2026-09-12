#!/usr/bin/env python3
"""
A quick check that Vespra builds, trains and behaves. No test framework:

    python3 tests/smoke.py

It compiles the engine and the trainer, checks that training actually reduces
the loss, and then drives run.py the way a person would. Everything happens in
a throwaway folder, so your own chats, settings and trained model are untouched.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
failures: list[str] = []
sandbox = Path(tempfile.mkdtemp(prefix="vespra-test-"))
RUNNER = sandbox / "run.py"


def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"  ok   {name}")
    else:
        print(f"  FAIL {name}")
        failures.append(f"{name}{': ' + detail if detail else ''}")


def say(lines: list[str], *flags: str, timeout: int = 300) -> str:
    """Run one conversation and return everything the bot printed."""
    result = subprocess.run(
        [sys.executable, str(RUNNER), "--no-color", *flags],
        input="\n".join(lines) + "\n",
        capture_output=True, text=True, cwd=sandbox, timeout=timeout,
    )
    if result.returncode != 0:
        failures.append(f"runner exited {result.returncode}\n{result.stderr}")
    return result.stdout


def bot_lines(text: str) -> list[str]:
    return [line[len("vespra> "):] for line in text.splitlines()
            if line.startswith("vespra> ")]


# ---- set the sandbox up -----------------------------------------------------

for item in ("run.py", "engine", "train"):
    source = ROOT / item
    if source.is_dir():
        shutil.copytree(source, sandbox / item, dirs_exist_ok=True)
    else:
        shutil.copy2(source, sandbox / item)

# the trained model and the token stream, if this checkout has them
(sandbox / "data").mkdir(exist_ok=True)
for name in ("vocab.txt", "corpus.bin"):
    if (ROOT / "data" / name).exists():
        shutil.copy2(ROOT / "data" / name, sandbox / "data" / name)
have_data = (sandbox / "data" / "corpus.bin").exists()

trained = ROOT / "model" / "vespra.lm"
if trained.exists():
    (sandbox / "model").mkdir(exist_ok=True)
    shutil.copy2(trained, sandbox / "model" / "vespra.lm")
have_model = (sandbox / "model" / "vespra.lm").exists()

print("building ...")
build = subprocess.run([sys.executable, str(RUNNER), "--rebuild", "--no-color"],
                       input="/bye\n", capture_output=True, text=True,
                       cwd=sandbox, timeout=600)

print("build:")
check("engine compiles", (sandbox / "build" / "vespra").exists(),
      build.stderr[-400:])
check("trainer compiles", (sandbox / "build" / "train").exists(),
      build.stderr[-400:])
check("no compiler warnings", "warning:" not in build.stderr,
      build.stderr[-400:])

# ---- the part that makes it a model and not a lookup table ------------------

print("training:")
if not have_data:
    print("  skip (no data/corpus.bin -- run python3 train/prepare.py first)")
else:
    # --resume, so this builds on the real shipped model (if there is one)
    # instead of quietly overwriting thousands of steps of training with a
    # few seconds of fresh random weights. 1.5 minutes rather than a shorter
    # window: the shipped model is big enough now (a few million parameters)
    # that a too-short run can land right at the "at least 3 readings"
    # boundary below and flake depending on how fast the machine is.
    train_cmd = [str(sandbox / "build" / "train"), "--minutes", "1.5"]
    if have_model:
        train_cmd.append("--resume")
    fresh = subprocess.run(train_cmd, capture_output=True, text=True, cwd=sandbox,
                           timeout=300, env={"PATH": "/usr/bin:/bin",
                                             "OMP_NUM_THREADS": "4"})
    numbers = [float(word) for line in fresh.stdout.split("loss ")[1:]
               for word in [line.split()[0]] if word.replace(".", "").isdigit()]
    check("training runs", len(numbers) >= 3,
          fresh.stdout[-300:] + fresh.stderr[-300:])
    if len(numbers) >= 3:
        if have_model:
            # resuming a model that is already well trained: 30-odd steps at
            # low loss is dominated by minibatch noise, so just check nothing
            # has blown up (a real bug -- exploding gradients, a bad load --
            # would show up as a large jump, not noise).
            check("loss stays sane when resuming an already-trained model",
                  numbers[-1] < numbers[0] + 0.5,
                  f"started {numbers[0]:.2f}, ended {numbers[-1]:.2f}")
        else:
            # from random weights it is still warming up, so this should be a
            # clear, fast drop, not noise.
            check("the loss comes down from random weights",
                  numbers[-1] < numbers[0] - 0.2,
                  f"started {numbers[0]:.2f}, ended {numbers[-1]:.2f}")
    check("a model file is written", (sandbox / "model" / "vespra.lm").exists())
    if have_model:
        trained_info = say(["/model", "/bye"])
        steps = [int(w) for line in trained_info.splitlines()
                 if "training steps" in line for w in line.split() if w.isdigit()]
        check("training resumed from the shipped model rather than restarting",
              bool(steps) and steps[0] > 100, f"steps: {steps}")

# ---- talking ---------------------------------------------------------------

print("conversation:")
if not have_model:
    print("  skip (no trained model -- run python3 run.py --train 20 first)")
else:
    out = say(["hello there", "what do you think about all this",
               "i went to the shops today", "/bye"])
    replies = bot_lines(out)

    check("draws the boot screen", "eeeeeee" in out and "aaaaaaa" in out)
    check("answers every turn", len(replies) >= 3, f"got {len(replies)}")
    check("answers are not empty", all(len(r.strip()) > 1 for r in replies))
    check("answers differ from each other", len(set(replies)) == len(replies),
          f"{replies}")

    # every word it says must be a word it learned -- nothing is hardcoded
    vocab = set((sandbox / "data" / "vocab.txt").read_text().split())
    spoken = {word.strip(".,!?").lower()
              for reply in replies for word in reply.split()}
    unknown = {w for w in spoken if w and w not in vocab}
    check("it only says words it was trained on", not unknown, f"{unknown}")

    check("no trace of the old name", "eliza" not in out.lower())
    check("says goodbye", "See you" in out or "Come back" in out)

    print("modes:")

    def thinking_seconds(mode: str) -> float:
        """What the runner itself measured around the model, minus startup."""
        text = say([f"/{mode}", "tell me something", "and something else",
                    "/summary", "/bye"])
        for line in text.splitlines():
            if "she thought for" in line:
                spent = line.split("she thought for")[1].split("(")[0].strip()
                if spent.endswith("s") and "m" not in spent:
                    return float(spent[:-1])
                minutes, seconds = spent.split("m")
                return float(minutes) * 60 + float(seconds.strip().rstrip("s"))
        return -1.0

    quick, slow = thinking_seconds("fast"), thinking_seconds("pro")
    check("pro really does more work than fast", slow > quick,
          f"fast {quick:.1f}s of model time, pro {slow:.1f}s")

    model_info = say(["/model", "/bye"])
    check("/model reports the real network",
          "weights" in model_info and "training steps" in model_info,
          model_info[-300:])

print("code snippets:")
code = say(["make me a button in css", "write hello world in py", "/bye"])
check("writes css", "--- css ---" in code and ".btn {" in code)
check("writes python", "--- python ---" in code and 'print("Hello, world!")' in code)

print("commands:")
helped = say(["/help", "/bye"])
check("/help lists every command",
      all(name in helped for name in ("/save", "/open", "/rm", "/mode",
                                      "/train", "/model", "/bye")))
long_help = say(["/HELP", "/bye"])
check("/HELP is the long version", len(long_help) > len(helped))
check("/HELP explains the modes honestly", "candidates" in long_help.lower()
      or "samples" in long_help.lower())

print("saved chats:")
say(["/name Testy", "/pro", "hello there", "/save mychat", "/bye"])
check("chat file written", (sandbox / "chats" / "mychat.json").exists())

reopened = say(["/open mychat", "/who", "/bye"])
check("/open restores the conversation", "hello there" in reopened.lower())
check("/open restores your name", "you      Testy" in reopened)
check("/open restores the mode", "mode     pro" in reopened)

kept = say(["/rm mychat", "n", "/list", "/bye"])
check("/rm asks first", "[y/n]" in kept)
check("/rm keeps it when you say no", "mychat" in kept.split("[y/n]")[-1])

removed = say(["/rm mychat", "y", "/list", "/bye"])
check("/rm removes it when you say yes",
      not (sandbox / "chats" / "mychat.json").exists())
check("/list copes with nothing saved", "no saved chats" in removed)

say(["/save one", "/new two", "/bye"])
all_gone = say(["/rm ALL", "y", "y", "/list", "/bye"])
check("/rm ALL confirms twice", all_gone.count("[y/n]") >= 2)
check("/rm ALL removes everything", not list((sandbox / "chats").glob("*.json")))

print("settings that stick:")
say(["/pro", "/swear off", "/name Testy", "/bye"])
again = say(["/who", "/bye"])
check("mode survives closing the app", "mode     pro" in again)
check("swearing setting survives", "swearing off" in again)
check("your name survives", "you      Testy" in again)
check("config file written", (sandbox / "config.json").exists())

print("summary and layout:")
summed = say(["hello", "/summary", "/bye"])
check("/summary reports thinking time", "she thought for" in summed)
check("/summary covers session, chat and all time",
      "this session" in summed and "this chat" in summed and "all time" in summed)

rules = [line.strip() for line in summed.splitlines() if line.strip().startswith("─")]
inside = [line.strip() for line in summed.splitlines()
          if line.strip().startswith(("local chat", "chat:"))]
check("nothing hangs off the rule",
      all(len(line) <= len(rules[0]) for line in inside) if rules and inside else False)

shutil.rmtree(sandbox, ignore_errors=True)

print()
if failures:
    print(f"{len(failures)} problem(s):")
    for item in failures:
        print(f" - {item}")
    sys.exit(1)

print("all good.")
