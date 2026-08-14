#!/usr/bin/env python3
"""
run.py -- the easy way to start Vespra.

Vespra itself (the "model") is written in C and lives in engine/. This script is
the front door: it finds a C compiler, builds the engine if it needs building,
draws the boot screen, and hosts the conversation.

    python3 run.py

Nothing is downloaded and nothing is installed. Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

BOT = "VESPRA"
PROMPT_BOT = "vespra"
VERSION = "1.0"

ROOT = Path(__file__).resolve().parent
ENGINE_DIR = ROOT / "engine"
BUILD_DIR = ROOT / "build"
CHAT_DIR = ROOT / "chats"
BINARY = BUILD_DIR / ("vespra.exe" if os.name == "nt" else "vespra")
SOURCES = [ENGINE_DIR / "vespra.c", ENGINE_DIR / "codegen.c"]
HEADERS = [ENGINE_DIR / "script.h", ENGINE_DIR / "codegen.h"]

END_NORMAL = "--END--"
END_QUIT = "--END--QUIT--"

SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,39}$")

# name -> (pause before answering, one line description)
MODES = {
    "fast":  (0.0, "short answers, no waiting"),
    "smart": (0.9, "the full answer, after a moment's thought"),
    "pro":   (3.0, "the long look: an answer plus a second thought"),
}

# Every command in one place, so /help and /HELP are always accurate.
# (command, argument, one line, longer explanation)
COMMANDS = [
    ("/help", "", "the short list of commands",
     "Prints every command on one line each. Use /HELP for the long version."),
    ("/HELP", "", "the long list, with what everything does",
     "This screen: every command explained, plus the modes and where chats live."),
    ("/save", "<chatname>", "save this conversation under a name",
     "Writes the conversation to chats/<chatname>.json and keeps saving it after\n"
     "  every turn from then on. Once a chat has a name, plain /save is enough.\n"
     "  /save_work does the same thing as /save work."),
    ("/open", "<chatname>", "reopen a saved conversation",
     "Loads the conversation back, and quietly feeds every earlier line through\n"
     "  the engine again so it remembers what you told it -- what you said about\n"
     "  \"my ...\" comes back, and replies carry on rotating where they left off."),
    ("/list", "", "list your saved chats",
     "Shows each saved chat with its turn count, mode and when it was last used."),
    ("/rm", "<chatname>|ALL", "delete a saved chat, or all of them",
     "Asks y/n first. /rm ALL deletes every saved chat, so it asks twice."),
    ("/rename", "<newname>", "rename the chat that is open",
     "Renames the file too. The old file is removed once the new one is written."),
    ("/new", "[chatname]", "start a fresh conversation",
     "Forgets everything in the current conversation and starts over. With a name,\n"
     "  the new conversation is saved under it straight away."),
    ("/name", "<yourname>", "tell Vespra what to call you",
     "It will use your name in conversation now and then. /name on its own tells\n"
     "  you the name it has; /name off forgets it. Saved with the chat."),
    ("/mode", "fast|smart|pro", "how hard Vespra thinks",
     "Or use /fast, /smart and /pro directly. /mode on its own shows the current\n"
     "  one. Saved with the chat."),
    ("/fast", "", "switch to fast mode", "Same as /mode fast."),
    ("/smart", "", "switch to smart mode", "Same as /mode smart."),
    ("/pro", "", "switch to pro mode", "Same as /mode pro."),
    ("/code", "<lang> <thing>", "ask for a code snippet outright",
     "For example /code css dark mode. Python, JavaScript, HTML and CSS.\n"
     "  You can also just say \"make me a button in css\" in normal conversation."),
    ("/who", "", "what Vespra currently knows",
     "The open chat, your name, the mode, and how many turns you have had."),
    ("/history", "[n]", "show the last n lines again",
     "Defaults to the last 20 lines of the conversation."),
    ("/export", "[file.txt]", "write the conversation out as plain text",
     "Defaults to <chatname>.txt, or chat-<date>.txt if the chat has no name."),
    ("/clear", "", "clear the screen",
     "Redraws the boot screen. The conversation is not touched."),
    ("/rebuild", "", "recompile the C engine",
     "Rebuilds engine/*.c and restarts it, keeping the conversation you are in."),
    ("/bye", "", "save and leave",
     "Saves first if the chat has a name. Ctrl-D, /quit and saying \"bye\" all do\n"
     "  the same thing."),
]

NO_COMPILER = f"""\
I could not find a C compiler, and {BOT}'s engine is written in C.

  macOS          xcode-select --install
  Debian/Ubuntu  sudo apt install build-essential
  Fedora         sudo dnf install gcc
  Arch           sudo pacman -S gcc
  Windows        install MinGW-w64, or run this inside WSL

Then run this script again. If your compiler has an unusual name, point me
at it directly:  python3 run.py --cc /path/to/mycc
"""

# Each big letter is drawn out of little copies of itself.
LETTERS = {
    "V": ["#.....#", "#.....#", "#.....#", "#.....#", ".#...#.", "..#.#..", "...#..."],
    "E": ["#######", "#......", "#......", "#####..", "#......", "#......", "#######"],
    "S": [".#####.", "#......", "#......", ".#####.", "......#", "......#", ".#####."],
    "P": ["######.", "#.....#", "#.....#", "######.", "#......", "#......", "#......"],
    "R": ["######.", "#.....#", "#.....#", "######.", "#...#..", "#....#.", "#.....#"],
    "A": ["..###..", ".#...#.", "#.....#", "#######", "#.....#", "#.....#", "#.....#"],
    "?": ["#######", "#######", "#######", "#######", "#######", "#######", "#######"],
}


class Screen:
    """The old green terminal look, switched off when output is not a tty."""

    def __init__(self, enabled: bool) -> None:
        self.on = enabled
        self.bright = "\033[1;32m" if enabled else ""   # what Vespra says
        self.green = "\033[32m" if enabled else ""      # code, chrome
        self.dim = "\033[2;32m" if enabled else ""      # labels, notes
        self.you = "\033[1;37m" if enabled else ""      # your own typing
        self.off = "\033[0m" if enabled else ""

    def banner(self, word: str) -> list[str]:
        rows = ["" for _ in range(7)]
        for char in word.upper():
            art = LETTERS.get(char, LETTERS["?"])
            ink = char.lower()
            for i, row in enumerate(art):
                rows[i] += row.replace("#", ink).replace(".", " ") + "  "
        return [row.rstrip() for row in rows]

    def boot(self, mode: str, chat: str | None) -> None:
        if self.on:
            print("\033[2J\033[H", end="")

        print()
        for row in self.banner(BOT):
            print(f"  {self.bright}{row}{self.off}")
        print()
        bar = "─" * 54
        print(f"  {self.green}{bar}{self.off}")
        print(f"  {self.dim}local chat terminal   v{VERSION}   "
              f"engine: c   runner: python {sys.version_info.major}."
              f"{sys.version_info.minor}{self.off}")
        print(f"  {self.dim}offline · no model · no network · "
              f"mode: {mode}   chat: {chat or 'unsaved'}{self.off}")
        print(f"  {self.green}{bar}{self.off}")
        print(f"  {self.dim}/help for commands, /HELP for the long version, "
              f"/bye to leave{self.off}")
        print()


# ----------------------------------------------------------------- building


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
    command = [compiler, "-O2", "-std=c99", "-Wall", "-Wextra",
               "-o", str(BINARY), *[str(path) for path in SOURCES]]

    if not quiet:
        print(f"building the engine with {Path(compiler).name} ...")

    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        print("The engine did not compile:\n", file=sys.stderr)
        print(result.stdout + result.stderr, file=sys.stderr)
        sys.exit(1)

    if result.stderr.strip() and not quiet:
        print(result.stderr.strip())


# ------------------------------------------------------------- the engine io


class Engine:
    """The C process, and the little protocol used to talk to it."""

    def __init__(self) -> None:
        self.process = subprocess.Popen(
            [str(BINARY)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self.read()  # the greeting it prints on startup

    def read(self) -> tuple[list[str], bool]:
        """Read reply lines up to the sentinel. Second value: chat is over."""
        lines: list[str] = []
        while True:
            line = self.process.stdout.readline()
            if line == "":
                return lines, True
            line = line.rstrip("\n")
            if line == END_NORMAL:
                return lines, False
            if line == END_QUIT:
                return lines, True
            lines.append(line)

    def send(self, line: str) -> tuple[list[str], bool]:
        try:
            self.process.stdin.write(line + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, ValueError):
            return ["The engine stopped unexpectedly."], True
        return self.read()

    def say(self, text: str) -> tuple[list[str], bool]:
        return self.send(">" + text)

    def control(self, command: str) -> list[str]:
        return self.send("!" + command)[0]

    def close(self) -> None:
        try:
            if self.process.stdin and not self.process.stdin.closed:
                self.process.stdin.close()
            self.process.wait(timeout=5)
        except (subprocess.TimeoutExpired, BrokenPipeError, ValueError):
            self.process.kill()


# ------------------------------------------------------------------- chats


class Chat:
    """One conversation: what was said, who said it, and how it is stored."""

    def __init__(self) -> None:
        self.name: str | None = None
        self.user: str = ""
        self.mode: str = "smart"
        self.created: str = time.strftime("%Y-%m-%d %H:%M")
        self.turns: list[dict] = []

    # ---- disk

    @staticmethod
    def path_for(name: str) -> Path:
        return CHAT_DIR / f"{name}.json"

    @staticmethod
    def saved_names() -> list[str]:
        if not CHAT_DIR.exists():
            return []
        return sorted(p.stem for p in CHAT_DIR.glob("*.json"))

    def save(self) -> None:
        if not self.name:
            return
        CHAT_DIR.mkdir(exist_ok=True)
        payload = {
            "name": self.name,
            "user": self.user,
            "mode": self.mode,
            "created": self.created,
            "updated": time.strftime("%Y-%m-%d %H:%M"),
            "turns": self.turns,
        }
        self.path_for(self.name).write_text(
            json.dumps(payload, indent=2) + "\n", encoding="utf-8"
        )

    @classmethod
    def load(cls, name: str) -> "Chat":
        data = json.loads(cls.path_for(name).read_text(encoding="utf-8"))
        chat = cls()
        chat.name = data.get("name", name)
        chat.user = data.get("user", "")
        chat.mode = data.get("mode", "smart")
        chat.created = data.get("created", "")
        chat.turns = data.get("turns", [])
        return chat

    # ---- content

    def add(self, who: str, text: str) -> None:
        self.turns.append({"who": who, "text": text})

    def said_by_user(self) -> list[str]:
        return [turn["text"] for turn in self.turns if turn["who"] == "you"]

    def as_text(self) -> str:
        head = [f"# {BOT} chat: {self.name or 'unsaved'}",
                f"# started {self.created}, mode {self.mode}"]
        if self.user:
            head.append(f"# you: {self.user}")
        body = [f"{'you' if t['who'] == 'you' else PROMPT_BOT}> {t['text']}"
                for t in self.turns]
        return "\n".join(head + [""] + body) + "\n"


# ------------------------------------------------------------------ session


class Session:
    def __init__(self, engine: Engine, screen: Screen, compiler: str,
                 delays: bool) -> None:
        self.engine = engine
        self.screen = screen
        self.compiler = compiler
        self.delays = delays
        self.chat = Chat()
        self.running = True

    # ---- output helpers

    def note(self, text: str) -> None:
        print(f"  {self.screen.dim}{text}{self.screen.off}")

    def para(self, text: str, indent: int = 6) -> None:
        """Print an explanation, every line lined up under the same margin."""
        for line in text.split("\n"):
            print(f"{' ' * indent}{self.screen.green}{line.strip()}"
                  f"{self.screen.off}")

    def speak(self, lines: list[str], remember: bool = True) -> None:
        """Print a reply. Code between the engine's markers stays flush left."""
        in_code = False
        first = True

        for line in lines:
            if line.startswith("--- ") and line.endswith(" ---"):
                in_code = line != "--- end ---"
                print(f"{self.screen.green}{line}{self.screen.off}")
            elif in_code:
                print(f"{self.screen.green}{line}{self.screen.off}")
            elif not line.strip():
                print()
            else:
                label = f"{PROMPT_BOT}> " if first else " " * (len(PROMPT_BOT) + 2)
                print(f"{self.screen.bright}{label}{line}{self.screen.off}")
                first = False

        if remember and lines:
            self.chat.add(PROMPT_BOT, "\n".join(lines))

    def think(self) -> None:
        """The pause that makes the modes feel like modes."""
        pause = MODES[self.chat.mode][0]
        if pause <= 0 or not self.delays or not sys.stdout.isatty():
            return

        end = time.time() + pause
        step = 0
        while time.time() < end:
            dots = "." * (step % 4)
            sys.stdout.write(f"\r{self.screen.dim}{PROMPT_BOT}> "
                             f"thinking{dots:<3}{self.screen.off}")
            sys.stdout.flush()
            time.sleep(0.18)
            step += 1
        sys.stdout.write("\r" + " " * 40 + "\r")
        sys.stdout.flush()

    def ask_yes_no(self, question: str) -> bool:
        try:
            print(f"  {self.screen.dim}{question} [y/n] {self.screen.off}",
                  end="", flush=True)
            answer = input().strip().lower()
            if not sys.stdin.isatty():
                print(answer)
        except (EOFError, KeyboardInterrupt):
            print()
            return False
        return answer in ("y", "yes")

    # ---- engine state

    def push_state(self) -> None:
        self.engine.control(f"mode {self.chat.mode}")
        if self.chat.user:
            self.engine.control(f"name {self.chat.user}")
        else:
            self.engine.control("name")

    def restart_engine(self) -> None:
        self.engine.close()
        self.engine = Engine()
        self.push_state()

    def replay(self) -> None:
        """Feed a reopened chat back through a fresh engine, silently."""
        self.restart_engine()
        for line in self.chat.said_by_user():
            if line.startswith("/"):
                continue
            self.engine.control("replay " + line)

    # ---- the turn

    def user_says(self, text: str) -> None:
        self.chat.add("you", text)
        self.think()
        lines, done = self.engine.say(text)
        self.speak(lines)
        self.chat.save()
        if done:
            self.running = False

    # ---- commands

    def cmd_help(self, long: bool) -> None:
        print()
        if not long:
            self.note("commands (/HELP for the long version)")
            for name, arg, short, _ in COMMANDS:
                label = f"{name} {arg}".strip()
                print(f"  {self.screen.bright}{label:<24}{self.screen.off}"
                      f"{self.screen.dim}{short}{self.screen.off}")
            print()
            self.note("or just talk. ask for code like: make me a button in css")
            print()
            return

        self.note(f"{BOT} v{VERSION} -- every command")
        print()
        for name, arg, short, long_text in COMMANDS:
            label = f"{name} {arg}".strip()
            print(f"  {self.screen.bright}{label}{self.screen.off}"
                  f"  {self.screen.dim}{short}{self.screen.off}")
            self.para(long_text)
            print()

        self.note("modes")
        for name, (pause, description) in MODES.items():
            wait = "no pause" if pause == 0 else f"~{pause:g}s pause"
            print(f"  {self.screen.bright}{name:<8}{self.screen.off}"
                  f"{self.screen.dim}{description} ({wait}){self.screen.off}")
        self.para(f"Modes change how much {BOT.title()} says and how long it "
                  f"waits, not how clever\nit is underneath -- it is a pattern "
                  f"matcher either way.")
        print()

        self.note("talking to it")
        self.para("Say anything. It picks out keywords and turns your words back "
                  "at you.\nAsk for code by naming a language: \"make me a button "
                  "in css\",\n\"write hello world in py\", \"show me a loop in "
                  "javascript\".")
        print()

        self.note(f"chats are plain json in {CHAT_DIR.name}/, one file each -- "
                  f"yours to read, move or delete")
        print()

    def cmd_save(self, arg: str) -> None:
        name = arg or self.chat.name
        if not name:
            self.note("save it under what name? try /save mychat")
            return
        if not SAFE_NAME.match(name):
            self.note("names can use letters, numbers, - and _ (up to 40)")
            return

        first_time = self.chat.name != name
        self.chat.name = name
        self.chat.save()
        self.note(f"saved as {name} ({len(self.chat.turns)} lines)"
                  + (" -- it will keep saving itself now" if first_time else ""))

    def cmd_open(self, arg: str) -> None:
        if not arg:
            self.note("open which one? /list shows them")
            return
        if not Chat.path_for(arg).exists():
            self.note(f"no chat called {arg}. /list shows what there is")
            return

        if self.chat.turns and not self.chat.name:
            if not self.ask_yes_no("this conversation is not saved. leave it?"):
                return

        try:
            self.chat = Chat.load(arg)
        except (json.JSONDecodeError, OSError) as err:
            self.note(f"could not read that chat: {err}")
            return

        self.replay()
        self.note(f"opened {arg} -- {len(self.chat.turns)} lines, "
                  f"mode {self.chat.mode}"
                  + (f", you are {self.chat.user}" if self.chat.user else ""))
        self.show_history(6)
        self.speak(self.engine.control("back"), remember=False)

    def cmd_list(self) -> None:
        names = Chat.saved_names()
        if not names:
            self.note("no saved chats yet. /save mychat keeps this one")
            return

        print()
        self.note(f"{len(names)} saved chat(s) in {CHAT_DIR.name}/")
        for name in names:
            try:
                data = json.loads(Chat.path_for(name).read_text(encoding="utf-8"))
                turns = len(data.get("turns", []))
                mode = data.get("mode", "?")
                updated = data.get("updated", "?")
                here = "  <- open" if name == self.chat.name else ""
            except (json.JSONDecodeError, OSError):
                turns, mode, updated, here = 0, "?", "unreadable", ""
            print(f"  {self.screen.bright}{name:<20}{self.screen.off}"
                  f"{self.screen.dim}{turns:>4} lines   {mode:<6} "
                  f"{updated}{here}{self.screen.off}")
        print()

    def cmd_rm(self, arg: str) -> None:
        if not arg:
            self.note("remove what? /rm mychat, or /rm ALL for all of them")
            return

        if arg.upper() == "ALL":
            names = Chat.saved_names()
            if not names:
                self.note("there are no saved chats to remove")
                return
            if not self.ask_yes_no(f"remove ALL {len(names)} saved chats?"):
                self.note("nothing removed")
                return
            if not self.ask_yes_no("this cannot be undone. really remove all?"):
                self.note("nothing removed")
                return
            for name in names:
                Chat.path_for(name).unlink(missing_ok=True)
            if self.chat.name in names:
                self.chat.name = None
            self.note(f"removed {len(names)} chat(s)")
            return

        if not Chat.path_for(arg).exists():
            self.note(f"no chat called {arg}")
            return
        if not self.ask_yes_no(f"remove chat {arg}?"):
            self.note("kept")
            return

        Chat.path_for(arg).unlink()
        if self.chat.name == arg:
            self.chat.name = None
            self.note(f"removed {arg} -- this conversation is now unsaved")
        else:
            self.note(f"removed {arg}")

    def cmd_rename(self, arg: str) -> None:
        if not self.chat.name:
            self.note("this chat has no name yet. /save mychat first")
            return
        if not arg or not SAFE_NAME.match(arg):
            self.note("rename to what? letters, numbers, - and _")
            return
        if Chat.path_for(arg).exists():
            self.note(f"{arg} already exists")
            return

        old = self.chat.name
        self.chat.name = arg
        self.chat.save()
        Chat.path_for(old).unlink(missing_ok=True)
        self.note(f"{old} is now {arg}")

    def cmd_new(self, arg: str) -> None:
        if self.chat.turns and not self.chat.name:
            if not self.ask_yes_no("this conversation is not saved. start over?"):
                return

        keep_user, keep_mode = self.chat.user, self.chat.mode
        self.chat = Chat()
        self.chat.user, self.chat.mode = keep_user, keep_mode
        self.restart_engine()

        if arg:
            if not SAFE_NAME.match(arg):
                self.note("names can use letters, numbers, - and _")
            else:
                self.chat.name = arg
                self.chat.save()

        self.note("fresh conversation"
                  + (f", saved as {self.chat.name}" if self.chat.name else ""))
        self.speak(self.engine.control("hello"), remember=False)

    def cmd_name(self, arg: str) -> None:
        if not arg:
            self.note(f"I call you {self.chat.user}" if self.chat.user
                      else "I do not know your name. try /name Cam")
            return
        if arg.lower() in ("off", "none", "forget"):
            self.chat.user = ""
            self.push_state()
            self.chat.save()
            self.note("forgotten")
            return

        self.chat.user = arg[:40]
        self.push_state()
        self.chat.save()
        self.note(f"hello, {self.chat.user}")

    def cmd_mode(self, arg: str) -> None:
        if not arg:
            pause, description = MODES[self.chat.mode]
            self.note(f"mode {self.chat.mode}: {description}"
                      + ("" if pause == 0 else f" (~{pause:g}s)"))
            return
        want = arg.lower()
        if want not in MODES:
            self.note("modes are fast, smart and pro")
            return

        self.chat.mode = want
        self.push_state()
        self.chat.save()
        self.note(f"mode {want}: {MODES[want][1]}")

    def cmd_who(self) -> None:
        lines = self.chat.turns
        yours = sum(1 for t in lines if t["who"] == "you")
        print()
        self.note(f"chat     {self.chat.name or 'unsaved (nothing on disk yet)'}")
        self.note(f"you      {self.chat.user or 'unnamed -- try /name Cam'}")
        self.note(f"mode     {self.chat.mode} -- {MODES[self.chat.mode][1]}")
        self.note(f"lines    {len(lines)} ({yours} from you)")
        self.note(f"started  {self.chat.created}")
        print()

    def show_history(self, count: int) -> None:
        if not self.chat.turns:
            self.note("nothing said yet")
            return
        print()
        for turn in self.chat.turns[-count:]:
            who = "you" if turn["who"] == "you" else PROMPT_BOT
            colour = self.screen.you if who == "you" else self.screen.dim
            for index, line in enumerate(turn["text"].split("\n")):
                label = f"{who}> " if index == 0 else " " * (len(who) + 2)
                print(f"  {colour}{label}{line}{self.screen.off}")
        print()

    def cmd_export(self, arg: str) -> None:
        default = (f"{self.chat.name}.txt" if self.chat.name
                   else f"chat-{time.strftime('%Y%m%d-%H%M%S')}.txt")
        target = ROOT / (arg or default)
        try:
            target.write_text(self.chat.as_text(), encoding="utf-8")
        except OSError as err:
            self.note(f"could not write that file: {err}")
            return
        self.note(f"written to {target.name}")

    def cmd_rebuild(self) -> None:
        build(self.compiler)
        self.replay()
        self.note("engine rebuilt, conversation restored")

    def cmd_bye(self) -> None:
        if self.chat.name:
            self.chat.save()
            self.note(f"saved as {self.chat.name}")
        elif self.chat.turns:
            self.note("this chat was not saved (/save mychat next time)")
        print(f"{self.screen.bright}{PROMPT_BOT}> "
              f"Goodbye{', ' + self.chat.user if self.chat.user else ''}. "
              f"Take care of yourself.{self.screen.off}")
        self.running = False

    # ---- dispatch

    def command(self, raw: str) -> None:
        body = raw[1:]

        if " " in body:
            word, arg = body.split(" ", 1)
        elif "_" in body:                      # /save_work, /open_work, /rm_ALL
            word, arg = body.split("_", 1)
        else:
            word, arg = body, ""
        arg = arg.strip()
        key = word.lower()

        if word == "HELP":
            self.cmd_help(long=True)
        elif key == "help":
            self.cmd_help(long=False)
        elif key == "save":
            self.cmd_save(arg)
        elif key == "open":
            self.cmd_open(arg)
        elif key in ("list", "chats", "ls"):
            self.cmd_list()
        elif key == "rm":
            self.cmd_rm(arg)
        elif key == "rename":
            self.cmd_rename(arg)
        elif key == "new":
            self.cmd_new(arg)
        elif key == "name":
            self.cmd_name(arg)
        elif key == "mode":
            self.cmd_mode(arg)
        elif key in MODES:
            self.cmd_mode(key)
        elif key == "who":
            self.cmd_who()
        elif key == "history":
            self.show_history(int(arg) if arg.isdigit() else 20)
        elif key == "export":
            self.cmd_export(arg)
        elif key == "clear":
            self.screen.boot(self.chat.mode, self.chat.name)
        elif key == "rebuild":
            self.cmd_rebuild()
        elif key in ("bye", "quit", "exit", "q"):
            self.cmd_bye()
        elif key == "code":
            self.user_says(raw)          # the engine handles /code itself
        else:
            self.note(f"no such command: /{word}. /help lists them")

    # ---- loop

    def run(self) -> int:
        self.screen.boot(self.chat.mode, self.chat.name)

        if self.chat.turns:          # opened with --open: pick up where we left off
            self.show_history(6)
            self.speak(self.engine.control("back"), remember=False)
        else:
            self.speak(self.engine.control("hello"), remember=False)

        while self.running:
            try:
                print(f"{self.screen.you}you> {self.screen.off}", end="", flush=True)
                text = input()
                if not sys.stdin.isatty():
                    print(text)         # piped input is not echoed for us
            except (EOFError, KeyboardInterrupt):
                print()
                self.cmd_bye()
                break

            if not text.strip():
                continue

            if text.strip().startswith("/"):
                self.command(text.strip())
            else:
                self.user_says(text)

        self.engine.close()
        return 0


# --------------------------------------------------------------------- main


def main() -> int:
    parser = argparse.ArgumentParser(
        description=f"{BOT} -- a local chat bot. The engine is C, this runner is Python."
    )
    parser.add_argument("--open", metavar="CHAT", help="open a saved chat on startup")
    parser.add_argument("--mode", choices=sorted(MODES), help="start in this mode")
    parser.add_argument("--rebuild", action="store_true",
                        help="recompile the engine even if it looks up to date")
    parser.add_argument("--no-color", action="store_true",
                        help="plain output, no green, no ANSI codes")
    parser.add_argument("--no-delay", action="store_true",
                        help="skip the thinking pauses (handy for scripts)")
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

    screen = Screen(enabled=not args.no_color and sys.stdout.isatty())

    try:
        engine = Engine()
    except OSError as err:
        print(f"Could not start the engine: {err}", file=sys.stderr)
        return 1

    session = Session(engine, screen, compiler,
                      delays=not args.no_delay and sys.stdout.isatty())

    if args.open:
        if Chat.path_for(args.open).exists():
            session.chat = Chat.load(args.open)
            session.replay()
        else:
            print(f"No saved chat called {args.open}.", file=sys.stderr)

    if args.mode:
        session.chat.mode = args.mode

    session.push_state()
    return session.run()


if __name__ == "__main__":
    sys.exit(main())
