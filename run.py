#!/usr/bin/env python3
"""
run.py -- the easy way to start the chat bot.

The bot itself (the "model") is written in C and lives in engine/. This script
is only the front door: it finds a C compiler, builds the engine if it needs
building, and then hosts the conversation.

    python3 run.py

Nothing is downloaded and nothing is installed. Standard library only.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ENGINE_DIR = ROOT / "engine"
BUILD_DIR = ROOT / "build"
BINARY = BUILD_DIR / ("eliza.exe" if os.name == "nt" else "eliza")
SOURCES = [ENGINE_DIR / "eliza.c", ENGINE_DIR / "codegen.c"]
HEADERS = [ENGINE_DIR / "script.h", ENGINE_DIR / "codegen.h"]

END_NORMAL = "--END--"
END_QUIT = "--END--QUIT--"

HELP_TEXT = """\
Just type and press enter -- ELIZA will reply to whatever you say.

It can also write small snippets of Python, JavaScript, HTML and CSS
when you ask for them, for example:

  make me a button in css
  write hello world in py
  show me a loop in javascript
  give me a form in html
  /code css dark mode

Commands:
  /help      this message
  /save      write the conversation so far to a text file
  /rebuild   recompile the C engine and restart it
  /quit      leave (or just say "bye", or press Ctrl-D)
"""

NO_COMPILER = """\
I could not find a C compiler, and the chat engine is written in C.

  macOS          xcode-select --install
  Debian/Ubuntu  sudo apt install build-essential
  Fedora         sudo dnf install gcc
  Arch           sudo pacman -S gcc
  Windows        install MinGW-w64, or run this inside WSL

Then run this script again. If your compiler has an unusual name, point me
at it directly:  python3 run.py --cc /path/to/mycc
"""


class Colors:
    """ANSI colours, switched off when the output is not a terminal."""

    def __init__(self, enabled: bool) -> None:
        self.bot = "\033[36m" if enabled else ""
        self.you = "\033[32m" if enabled else ""
        self.dim = "\033[2m" if enabled else ""
        self.off = "\033[0m" if enabled else ""


def find_compiler(preferred: str | None) -> str | None:
    """Return the compiler to use, or None if there isn't one."""
    candidates = []
    if preferred:
        candidates.append(preferred)
    if os.environ.get("CC"):
        candidates.append(os.environ["CC"])
    candidates += ["cc", "gcc", "clang", "tcc"]

    for name in candidates:
        found = shutil.which(name) or (name if Path(name).is_file() else None)
        if found:
            return found
    return None


def needs_build() -> bool:
    """True when the binary is missing or older than any source file."""
    if not BINARY.exists():
        return True
    built = BINARY.stat().st_mtime
    return any(path.stat().st_mtime > built for path in SOURCES + HEADERS)


def build(compiler: str, quiet: bool = False) -> None:
    """Compile the engine, or exit with the compiler's own error output."""
    BUILD_DIR.mkdir(exist_ok=True)
    command = [
        compiler,
        "-O2",
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-o",
        str(BINARY),
        *[str(path) for path in SOURCES],
    ]

    if not quiet:
        print(f"Building the chat engine with {Path(compiler).name} ...")

    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        print("The engine did not compile:\n", file=sys.stderr)
        print(result.stdout + result.stderr, file=sys.stderr)
        sys.exit(1)

    if result.stderr.strip() and not quiet:
        print(result.stderr.strip())


def start_engine() -> subprocess.Popen:
    return subprocess.Popen(
        [str(BINARY)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        bufsize=1,
    )


def read_reply(engine: subprocess.Popen) -> tuple[list[str], bool]:
    """
    Read lines until the engine's sentinel.

    Returns the reply lines and whether the engine wants to end the chat.
    """
    lines: list[str] = []
    while True:
        line = engine.stdout.readline()
        if line == "":  # engine died
            return lines, True
        line = line.rstrip("\n")
        if line == END_NORMAL:
            return lines, False
        if line == END_QUIT:
            return lines, True
        lines.append(line)


def show(lines: list[str], colors: Colors, transcript: list[str]) -> None:
    """
    Print a reply. Prose gets the "eliza>" gutter; anything between the engine's
    "--- lang ---" and "--- end ---" markers is printed flush left so it can be
    copied straight into a file.
    """
    in_code = False

    for index, line in enumerate(lines):
        if line.startswith("--- ") and line.endswith(" ---"):
            in_code = line != "--- end ---"
            print(f"{colors.dim}{line}{colors.off}")
            transcript.append(line)
            continue

        if in_code:
            print(line)
            transcript.append(line)
            continue

        if not line.strip():
            print()
            transcript.append("")
            continue

        label = "eliza> " if index == 0 else "       "
        print(f"{colors.bot}{label}{colors.off}{line}")
        transcript.append(f"eliza> {line}" if index == 0 else f"       {line}")


def save_transcript(transcript: list[str]) -> Path:
    name = f"chat-log-{time.strftime('%Y%m%d-%H%M%S')}.txt"
    path = ROOT / name
    path.write_text("\n".join(transcript) + "\n", encoding="utf-8")
    return path


def chat(engine: subprocess.Popen, colors: Colors, compiler: str) -> int:
    transcript: list[str] = []

    greeting, done = read_reply(engine)
    show(greeting, colors, transcript)
    print(f"{colors.dim}(type /help for the commands, /quit to leave){colors.off}")

    while not done:
        try:
            print(f"{colors.you}you> {colors.off}", end="", flush=True)
            text = input()
            if not sys.stdin.isatty():
                print(text)  # piped input is not echoed for us
        except (EOFError, KeyboardInterrupt):
            print()
            print(f"{colors.bot}eliza> {colors.off}Goodbye. Take care of yourself.")
            break

        transcript.append(f"you> {text}")
        command = text.strip().lower()

        if command in ("/quit", "/exit", "/q"):
            print(f"{colors.bot}eliza> {colors.off}Goodbye. Take care of yourself.")
            break

        if command == "/help":
            print(HELP_TEXT)
            transcript.append(HELP_TEXT)
            continue

        if command == "/save":
            path = save_transcript(transcript)
            print(f"{colors.dim}saved to {path.name}{colors.off}")
            continue

        if command == "/rebuild":
            engine.stdin.close()
            engine.wait(timeout=5)
            build(compiler)
            engine = start_engine()
            fresh, done = read_reply(engine)
            show(fresh, colors, transcript)
            continue

        try:
            engine.stdin.write(text + "\n")
            engine.stdin.flush()
        except BrokenPipeError:
            print("The chat engine stopped unexpectedly.", file=sys.stderr)
            return 1

        reply, done = read_reply(engine)
        show(reply, colors, transcript)

    try:
        if engine.stdin and not engine.stdin.closed:
            engine.stdin.close()
        engine.wait(timeout=5)
    except (subprocess.TimeoutExpired, BrokenPipeError):
        engine.kill()

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Chat with a local ELIZA. The engine is C; this runner is Python."
    )
    parser.add_argument("--rebuild", action="store_true",
                        help="recompile the engine even if it looks up to date")
    parser.add_argument("--no-color", action="store_true",
                        help="plain output, no ANSI colours")
    parser.add_argument("--cc", metavar="PATH",
                        help="use this C compiler instead of searching for one")
    args = parser.parse_args()

    if sys.version_info < (3, 8):
        print("This runner needs Python 3.8 or newer.", file=sys.stderr)
        return 1

    for path in SOURCES + HEADERS:
        if not path.exists():
            print(f"Missing engine source: {path}", file=sys.stderr)
            return 1

    compiler = find_compiler(args.cc)
    if compiler is None:
        print(NO_COMPILER, file=sys.stderr)
        return 1

    if args.rebuild or needs_build():
        build(compiler)

    colors = Colors(enabled=not args.no_color and sys.stdout.isatty())

    try:
        engine = start_engine()
    except OSError as err:
        print(f"Could not start the engine: {err}", file=sys.stderr)
        return 1

    return chat(engine, colors, compiler)


if __name__ == "__main__":
    sys.exit(main())
