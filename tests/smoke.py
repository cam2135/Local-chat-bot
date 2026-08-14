#!/usr/bin/env python3
"""
A quick check that Vespra builds and behaves. No test framework needed:

    python3 tests/smoke.py

It drives run.py exactly the way a person would, by typing lines at it.
Saved chats are written to a throwaway copy of the chats folder, so your own
conversations are left alone.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUNNER = ROOT / "run.py"

failures: list[str] = []
sandbox = Path(tempfile.mkdtemp(prefix="vespra-test-"))


def say(lines: list[str], *flags: str) -> str:
    """Run one conversation and return everything the bot printed."""
    result = subprocess.run(
        [sys.executable, str(RUNNER), "--no-color", "--no-delay", *flags],
        input="\n".join(lines) + "\n",
        capture_output=True,
        text=True,
        cwd=sandbox,
        timeout=120,
    )
    if result.returncode != 0:
        failures.append(f"runner exited {result.returncode}\n{result.stderr}")
    return result.stdout


def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"  ok   {name}")
    else:
        print(f"  FAIL {name}")
        failures.append(f"{name}{': ' + detail if detail else ''}")


# run.py lives in the repo, but chats and the binary should land in the sandbox
for item in ("run.py", "engine", "build"):
    source = ROOT / item
    if source.is_dir():
        shutil.copytree(source, sandbox / item, dirs_exist_ok=True)
    elif source.exists():
        shutil.copy2(source, sandbox / item)
RUNNER = sandbox / "run.py"

MEMORY_LINES = ("mentioned your mother", "back to your mother",
                "about your mother before", "thinking about your mother")


def remembers(text: str) -> bool:
    return any(phrase in text for phrase in MEMORY_LINES)


print("building and chatting ...")

out = say([
    "Hello",
    "I am worried about my mother",
    "I can't stand it",
    "you are very good at this",
    "tell me a joke",
    "make me a button in css",
    "write hello world in py",
    "show me a loop in javascript",
    "give me a table in html",
    "asdf qwer zxcv",
    "/bye",
])

print("conversation:")
check("draws the boot screen", "eeeeeee" in out and "aaaaaaa" in out and
      "sssss" in out, "expected big letters drawn out of little ones")
check("greets on start", "I am Vespra" in out)
check("sounds friendly, not clinical",
      not any(phrase in out for phrase in
              ("What does that suggest to you", "Does talking about this bother",
               "How does that make you feel")))
check("no trace of the old name", "eliza" not in out.lower())
check("swaps pronouns", "your mother" in out, "expected 'my mother' -> 'your mother'")
check("expands contractions", "stand it" in out.lower(),
      "\"i can't stand it\" should match the \"i can not\" pattern")
check("takes a compliment", "I will take that" in out or "too kind" in out)
check("tells a joke", "bugs" in out.lower() or "console it" in out.lower())
check("writes css", "--- css ---" in out and ".btn {" in out)
check("writes python", "--- python ---" in out and 'print("Hello, world!")' in out)
check("writes javascript", "--- javascript ---" in out and "console.log" in out)
check("writes html", "--- html ---" in out and "<table>" in out)
check("brings back a remembered remark", remembers(out))
check("says goodbye", "See you" in out or "Take it easy" in out)

print("commands:")
helped = say(["/help", "/bye"])
check("/help lists every command", all(name in helped for name, *_ in
                                       [("/save",), ("/open",), ("/rm",),
                                        ("/mode",), ("/name",), ("/bye",)]))
long_help = say(["/HELP", "/bye"])
check("/HELP explains the modes",
      "fast" in long_help and "pro" in long_help and "pattern matcher" in long_help)
check("/HELP is longer than /help", len(long_help) > len(helped))

named = say(["/name Cam", "/who", "/bye"])
check("/name is remembered", "you      Cam" in named)

modes = say(["/fast", "i am very tired of all of this", "/pro",
             "i am very tired of all of this", "/bye"])
fast_lines = modes.split("/pro")[0]
pro_lines = modes.split("/pro")[1]
check("fast mode answers in one sentence",
      max((len(line) for line in fast_lines.splitlines()
           if line.startswith("vespra> ")), default=999) < 90)
check("pro mode adds a second thought",
      sum(1 for line in pro_lines.splitlines()
          if line.startswith("vespra> ") or line.startswith("        ")) >= 2)

print("saved chats:")
say(["/name Cam", "/pro", "I am worried about my mother", "/save mychat", "/bye"])
check("chat file written", (sandbox / "chats" / "mychat.json").exists())

reopened = say(["/open mychat", "gibberish nonsense words", "/who", "/bye"])
check("/open restores the conversation", "i am worried about my mother"
      in reopened.lower())
check("/open restores your name", "you      Cam" in reopened)
check("/open restores the mode", "mode     pro" in reopened)
check("/open restores the memory", remembers(reopened))

listed = say(["/list", "/bye"])
check("/list shows the chat", "mychat" in listed)

kept = say(["/rm mychat", "n", "/list", "/bye"])
check("/rm asks first", "[y/n]" in kept)
check("/rm keeps it when you say no", "mychat" in kept.split("[y/n]")[-1])

removed = say(["/rm mychat", "y", "/list", "/bye"])
check("/rm removes it when you say yes",
      not (sandbox / "chats" / "mychat.json").exists())
check("/list copes with nothing saved", "no saved chats" in removed)

say(["/save one", "/new two", "/new three", "/bye"])
all_gone = say(["/rm ALL", "y", "y", "/list", "/bye"])
check("/rm ALL confirms twice", all_gone.count("[y/n]") >= 2)
check("/rm ALL removes everything",
      not list((sandbox / "chats").glob("*.json")))

underscore = say(["/save_undertest", "/list", "/bye"])
check("/save_name works like /save name", "undertest" in underscore)

print("swearing:")
sweary = say(["this is fucking ridiculous", "the whole shitty thing is late",
              "fuck all of it", "/bye"])
check("swears back when you do",
      any(word in sweary.lower() for word in ("damn", "hell", "bloody", "crap",
                                              "fuck", "sod", "shit")))
escalates = [line for line in sweary.splitlines() if line.startswith("vespra> ")]
check("starts mild and builds", len(escalates) >= 3 and
      escalates[0] != escalates[-1])

clean = say(["/swear off", "this is fucking ridiculous", "/bye"])
check("/swear off keeps it clean",
      not any(word in clean.lower() for word in
              ("fuck it", "bloody hell", "sod that", "screw them")))
check("stays polite by default in ordinary chat",
      not any(word in out.lower() for word in ("fuck", "shit", "bloody")))

print("settings that stick:")
say(["/pro", "/swear off", "/name Testy", "/bye"])
again = say(["/who", "/bye"])
check("mode survives closing the app", "mode     pro" in again)
check("swearing setting survives", "swearing off" in again)
check("your name survives", "you      Testy" in again)
check("config file written", (sandbox / "config.json").exists())
fresh = say(["/smart", "/swear on", "/name off", "/bye"])
check("changing it back sticks too", "mode smart" in fresh)

print("summary:")
summed = say(["hello there you", "i am typing some words at you now",
              "/summary", "/bye"])
check("/summary reports thinking time", "she thought for" in summed)
check("/summary counts what you typed",
      "characters" in summed and "words" in summed)
check("/summary mentions words a minute",
      "typing speed" in summed and "wpm" in summed.lower()
      or "not enough typing" in summed)
check("/summary covers session, chat and all time",
      "this session" in summed and "this chat" in summed
      and "all time" in summed)
check("/summary counts characters, not zero",
      any(line.strip().startswith("you said") and "0 characters" not in line
          for line in summed.splitlines()))

print("layout:")
laid = say(["/bye"])
rules = [line.strip() for line in laid.splitlines()
         if line.strip().startswith("─")]
boxed = laid.splitlines()
check("nothing hangs off the rule", all(
    len(boxed[i].strip()) <= len(rules[0])
    for i in range(boxed.index("  " + rules[0]) + 1,
                   len(boxed)) if boxed[i].strip().startswith(("local chat",
                                                               "chat:"))),
      "the header lines must fit inside the ─── rule")

print("live status:")
status = say(["/pro", "/save trial", "/who", "/bye"])
check("prompt shows the mode you switched to", "trial\u00b7pro you>" in status
      or "trial\u00b7pro" in status)
check("status bar refreshes on change", "mode: pro" in status)
check("status bar shows the saved chat name", "chat: trial" in status)
check("no stale mode left on screen", "mode: smart" not in status.split("/pro")[-1])
say(["/rm trial", "y", "/bye"])

print("engine:")
binary = sandbox / "build" / ("vespra.exe" if sys.platform == "win32" else "vespra")
direct = subprocess.run(
    [str(binary)], input="men are all alike\nbye\n",
    capture_output=True, text=True, timeout=30,
)
check("engine runs standalone", "--END--" in direct.stdout and direct.returncode == 0)

shutil.rmtree(sandbox, ignore_errors=True)

print()
if failures:
    print(f"{len(failures)} problem(s):")
    for item in failures:
        print(f" - {item}")
    sys.exit(1)

print("all good.")
