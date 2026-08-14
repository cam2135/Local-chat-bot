#!/usr/bin/env python3
"""
A quick check that the bot builds and behaves. No test framework needed:

    python3 tests/smoke.py

It drives run.py exactly the way a person would, by typing lines at it.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUNNER = ROOT / "run.py"

failures: list[str] = []


def say(lines: list[str]) -> str:
    """Run one conversation and return everything the bot printed."""
    result = subprocess.run(
        [sys.executable, str(RUNNER), "--no-color"],
        input="\n".join(lines) + "\n",
        capture_output=True,
        text=True,
        cwd=ROOT,
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


print("building and chatting ...")

out = say([
    "Hello",
    "I am worried about my mother",
    "I can't sleep at night",
    "You are only a machine",
    "make me a button in css",
    "write hello world in py",
    "show me a loop in javascript",
    "give me a table in html",
    "asdf qwer zxcv",
    "bye",
])

print("checks:")
check("greets on start", "I am ELIZA" in out)
check("swaps pronouns", "your mother" in out, "expected 'my mother' -> 'your mother'")
check("expands contractions", "can not sleep" in out.lower())
check("answers 'you are ...'", "I am only a machine" in out)
check("writes css", "--- css ---" in out and ".btn {" in out)
check("writes python", "--- python ---" in out and 'print("Hello, world!")' in out)
check("writes javascript", "--- javascript ---" in out and "console.log" in out)
check("writes html", "--- html ---" in out and "<table>" in out)
check("brings back a remembered remark", "Earlier you said your mother" in out)
check("says goodbye", "Goodbye" in out)

# The same input twice should not give the same answer twice: reassembly rotates.
rotation = say(["I am sad", "I am sad", "I am sad", "bye"])
replies = [line for line in rotation.splitlines() if line.startswith("eliza> ")]
sad_replies = [line for line in replies if "you been sad" in line or "sad" in line]
check("rotates replies", len(set(sad_replies)) >= 2, f"got {sad_replies}")

# A farewell word inside a longer sentence must not end the chat.
lingering = say(["I said goodbye to my mother yesterday", "what do you think", "bye"])
check("does not quit mid sentence", "what do you think" in lingering.lower())

# The engine on its own, no Python involved.
binary = ROOT / "build" / "eliza"
direct = subprocess.run(
    [str(binary)], input="men are all alike\nbye\n",
    capture_output=True, text=True, timeout=30,
)
check("engine runs standalone", "--END--" in direct.stdout and direct.returncode == 0)

print()
if failures:
    print(f"{len(failures)} problem(s):")
    for item in failures:
        print(f" - {item}")
    sys.exit(1)

print("all good.")
