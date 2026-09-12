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
TRAINER = BUILD_DIR / ("train.exe" if os.name == "nt" else "train")
MODEL_DIR = ROOT / "model"
MODEL = MODEL_DIR / "vespra.lm"
DATA_DIR = ROOT / "data"
CORPUS = DATA_DIR / "corpus.bin"
PREPARE = ROOT / "train" / "prepare.py"

SOURCES = [ENGINE_DIR / "vespra.c", ENGINE_DIR / "codegen.c",
           ENGINE_DIR / "tinylm.c"]
TRAIN_SOURCES = [ENGINE_DIR / "train_lm.c", ENGINE_DIR / "tinylm.c"]
HEADERS = [ENGINE_DIR / "codegen.h", ENGINE_DIR / "tinylm.h"]

END_NORMAL = "--END--"
END_QUIT = "--END--QUIT--"

SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,39}$")

# name -> (candidates the model draws, one line description)
MODES = {
    "fast":  (1, "one draw from the model, straight back to you"),
    "smart": (3, "draws 3 answers and keeps the best one"),
    "pro":   (8, "draws 8 and keeps the best -- slowest, and it shows"),
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
     "Loads the conversation back and feeds it through the model again, so it\n"
     "  carries on with the conversation in mind rather than from nothing."),
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
    ("/mode", "fast|smart|pro", "how hard the model thinks",
     "fast draws one answer, smart draws 3 and keeps the best, pro draws 8. More\n"
     "  candidates means better answers and a longer wait -- real work, not a\n"
     "  fake pause. /mode on its own shows the current one. Saved with the chat."),
    ("/fast", "", "switch to fast mode", "Same as /mode fast."),
    ("/smart", "", "switch to smart mode", "Same as /mode smart."),
    ("/pro", "", "switch to pro mode", "Same as /mode pro."),
    ("/code", "<lang> <thing>", "ask for a code snippet outright",
     "For example /code css dark mode. Python, JavaScript, HTML and CSS.\n"
     "  You can also just say \"make me a button in css\" in normal conversation."),
    ("/swear", "on|off", "filter out any swearing in her replies",
     "The training data is near-spotless (an instruction/response dataset, not\n"
     "  casual dialogue), so there is rarely anything to catch -- this is a safety\n"
     "  net, not a personality trait. Any reply that does contain swearing is\n"
     "  thrown away and resampled when this is off."),
    ("/summary", "", "who did the talking, and how long it took",
     "How long she has spent thinking, how much you have typed, your average\n"
     "  words a minute, and the same again for this chat and for all time."),
    ("/who", "", "what Vespra currently knows",
     "The open chat, your name, the mode, and how many turns you have had."),
    ("/history", "[n]", "show the last n lines again",
     "Defaults to the last 20 lines of the conversation."),
    ("/export", "[file.txt]", "write the conversation out as plain text",
     "Defaults to <chatname>.txt, or chat-<date>.txt if the chat has no name."),
    ("/clear", "", "clear the screen",
     "Redraws the boot screen. The conversation is not touched."),
    ("/train", "[minutes]", "train the model for longer",
     "Carries on training the neural net from where it got to, for however many\n"
     "  minutes you give it (20 by default). The longer it trains, the better it\n"
     "  talks. Ctrl-C stops it early and keeps everything learned so far."),
    ("/model", "", "what the model actually is",
     "Its size, how many training steps it has had, and how much text it has read."),
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

    @property
    def width(self) -> int:
        return max(28, shutil.get_terminal_size(fallback=(80, 24)).columns)

    def banner(self, word: str) -> list[str]:
        """Each big letter drawn out of little copies of itself."""
        rows = ["" for _ in range(7)]
        for char in word.upper():
            art = LETTERS.get(char, LETTERS["?"])
            ink = char.lower()
            for i, row in enumerate(art):
                rows[i] += row.replace("#", ink).replace(".", " ") + "  "
        return [row.rstrip() for row in rows]

    def fit(self, text: str) -> str:
        """Never let a line wrap awkwardly in a narrow window."""
        room = self.width - 4
        return text if len(text) <= room else text[: room - 1] + "…"

    def boxed(self, lines: list[str], title: str = "") -> None:
        """
        Print lines between two rules. The rule is drawn to fit whatever is
        inside it, so nothing ever hangs off the end of the line.
        """
        shown = [self.fit(line) for line in lines]
        if title:
            shown.insert(0, self.fit(title))
        bar = "─" * min(self.width - 4, max(len(line) for line in shown))

        print(f"  {self.green}{bar}{self.off}")
        for index, line in enumerate(shown):
            if not line.strip():
                print()
                continue
            colour = self.bright if title and index == 0 else self.dim
            print(f"  {colour}{line}{self.off}")
        print(f"  {self.green}{bar}{self.off}")

    def boot(self, status: str) -> None:
        if self.on:
            print("\033[2J\033[H", end="")

        print()
        big = self.banner(BOT)
        if len(big[0]) + 4 <= self.width:
            for row in big:
                print(f"  {self.bright}{row}{self.off}")
        else:                       # narrow window: small sign instead
            print(f"  {self.bright}{' '.join(BOT)}{self.off}")
        print()

        self.boxed([
            f"local chat terminal   v{VERSION}   engine: c   "
            f"runner: python {sys.version_info.major}.{sys.version_info.minor}",
            status,
        ])
        print(f"  {self.dim}"
              f"{self.fit('/help for commands, /HELP for the long version, /bye to leave')}"
              f"{self.off}")
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
    """True when either binary is missing or older than any source file."""
    if not BINARY.exists() or not TRAINER.exists():
        return True
    built = min(BINARY.stat().st_mtime, TRAINER.stat().st_mtime)
    return any(path.stat().st_mtime > built
               for path in SOURCES + TRAIN_SOURCES + HEADERS)


def compile_one(compiler: str, target: Path, sources: list[Path],
                extra: list[str]) -> subprocess.CompletedProcess:
    command = [compiler, "-O3", "-ffast-math", "-std=c99", "-Wall", "-Wextra",
               *extra, "-o", str(target), *[str(s) for s in sources], "-lm"]
    return subprocess.run(command, cwd=ROOT, capture_output=True, text=True)


def build(compiler: str, quiet: bool = False) -> None:
    """
    Compile the chat engine and the trainer. Matrix multiplication is the whole
    job here, so we ask for OpenMP and fall back quietly if this compiler has
    not got it.
    """
    BUILD_DIR.mkdir(exist_ok=True)
    if not quiet:
        print(f"building with {Path(compiler).name} ...")

    for target, sources in ((BINARY, SOURCES), (TRAINER, TRAIN_SOURCES)):
        result = compile_one(compiler, target, sources, ["-fopenmp"])
        if result.returncode != 0:
            result = compile_one(compiler, target, sources, [])
        if result.returncode != 0:
            print(f"{target.name} did not compile:\n", file=sys.stderr)
            print(result.stdout + result.stderr, file=sys.stderr)
            sys.exit(1)


def prepare_data(quiet: bool = False) -> bool:
    """Build data/corpus.bin if it is not there yet."""
    if CORPUS.exists():
        return True
    print("preparing the training data (this downloads the corpus once) ...")
    result = subprocess.run([sys.executable, str(PREPARE)], cwd=ROOT)
    return result.returncode == 0 and CORPUS.exists()


def train(minutes: float, compiler: str, resume: bool = True) -> bool:
    """Run the trainer, showing its progress live."""
    if not prepare_data():
        print("could not prepare the training data", file=sys.stderr)
        return False
    if not TRAINER.exists():
        build(compiler)

    MODEL_DIR.mkdir(exist_ok=True)
    command = [str(TRAINER), "--minutes", str(minutes)]
    if resume and MODEL.exists():
        command.append("--resume")

    print()
    try:
        subprocess.run(command, cwd=ROOT)
    except KeyboardInterrupt:
        print("\nstopped -- what it learned so far is saved")
    print()
    return MODEL.exists()


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
            cwd=ROOT,
        )
        # It says nothing until it is asked, so there is nothing to read here.

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


def blank_stats() -> dict:
    return {"lines": 0, "chars": 0, "words": 0,
            "typing_seconds": 0.0, "think_seconds": 0.0,
            "bot_lines": 0, "bot_chars": 0}


def add_stats(into: dict, **amounts: float) -> None:
    for key, amount in amounts.items():
        into[key] = into.get(key, 0) + amount


def plural(count: float, thing: str) -> str:
    return f"{count:,} {thing}" + ("" if count == 1 else "s")


def spell_time(seconds: float) -> str:
    seconds = int(round(seconds))
    if seconds < 60:
        return f"{seconds}s"
    if seconds < 3600:
        return f"{seconds // 60}m {seconds % 60:02d}s"
    return f"{seconds // 3600}h {(seconds % 3600) // 60:02d}m"


class Config:
    """
    Settings that outlive a single run: the mode you were last in, whether she
    swears, your name, and the running totals /summary reports. Kept in
    config.json next to run.py.
    """

    PATH = ROOT / "config.json"

    def __init__(self) -> None:
        self.mode = "smart"
        self.swearing = True
        self.user = ""
        self.last_chat: str | None = None
        self.stats = blank_stats()
        self.mode_use = {name: 0 for name in MODES}

    @classmethod
    def load(cls) -> "Config":
        config = cls()
        try:
            data = json.loads(cls.PATH.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return config

        if data.get("mode") in MODES:
            config.mode = data["mode"]
        config.swearing = bool(data.get("swearing", True))
        config.user = str(data.get("user", ""))[:40]
        config.last_chat = data.get("last_chat")
        config.stats = {**blank_stats(), **data.get("stats", {})}
        config.mode_use = {**config.mode_use, **data.get("mode_use", {})}
        return config

    def save(self) -> None:
        payload = {
            "mode": self.mode,
            "swearing": self.swearing,
            "user": self.user,
            "last_chat": self.last_chat,
            "stats": self.stats,
            "mode_use": self.mode_use,
        }
        try:
            self.PATH.write_text(json.dumps(payload, indent=2) + "\n",
                                 encoding="utf-8")
        except OSError:
            pass          # a read-only folder should not stop the chat


class Chat:
    """One conversation: what was said, who said it, and how it is stored."""

    def __init__(self) -> None:
        self.name: str | None = None
        self.user: str = ""
        self.mode: str = "smart"
        self.swearing: bool = True
        self.created: str = time.strftime("%Y-%m-%d %H:%M")
        self.turns: list[dict] = []
        self.stats: dict = blank_stats()

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
            "swearing": self.swearing,
            "created": self.created,
            "updated": time.strftime("%Y-%m-%d %H:%M"),
            "stats": self.stats,
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
        chat.swearing = data.get("swearing", True)
        chat.created = data.get("created", "")
        chat.turns = data.get("turns", [])
        chat.stats = {**blank_stats(), **data.get("stats", {})}
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
                 delays: bool, config: Config) -> None:
        self.engine = engine
        self.screen = screen
        self.compiler = compiler
        self.delays = delays
        self.config = config
        self.chat = Chat()
        self.chat.mode = config.mode
        self.chat.swearing = config.swearing
        self.chat.user = config.user
        self.session = blank_stats()
        self.started = time.time()
        self.running = True

    def remember_settings(self) -> None:
        """Keep config.json in step, so the next run starts where this one is."""
        self.config.mode = self.chat.mode
        self.config.swearing = self.chat.swearing
        self.config.user = self.chat.user
        self.config.last_chat = self.chat.name
        self.config.save()

    # ---- output helpers

    def status_line(self) -> str:
        """The live state, built fresh every time so it can never go stale."""
        if self.screen.width < 52:      # narrow window: keep the useful half
            return (f"{self.chat.name or 'unsaved'} · {self.chat.mode} · "
                    f"{self.chat.user or '-'}")
        return (f"chat: {self.chat.name or 'unsaved'}   "
                f"mode: {self.chat.mode}   "
                f"you: {self.chat.user or '-'}   "
                f"lines: {len(self.chat.turns)}")

    def show_status(self) -> None:
        self.screen.boxed([self.status_line()])

    def note(self, text: str) -> None:
        print(f"  {self.screen.dim}{self.screen.fit(text)}{self.screen.off}")

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

    def show_thinking(self) -> None:
        """Say something is happening while the model actually computes."""
        if not sys.stdout.isatty():
            return
        sys.stdout.write(f"{self.screen.dim}{PROMPT_BOT}> thinking ..."
                         f"{self.screen.off}")
        sys.stdout.flush()

    def clear_thinking(self) -> None:
        if not sys.stdout.isatty():
            return
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
        self.engine.control(f"swear {'on' if self.chat.swearing else 'off'}")
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
        self.engine.control("forget")
        for turn in self.chat.turns:
            text = turn["text"].replace("\n", " ").strip()
            if not text or text.startswith("/"):
                continue
            who = "you" if turn["who"] == "you" else "bot"
            self.engine.control(f"replay {who} {text}")

    # ---- the turn

    def user_says(self, text: str, typing_seconds: float = 0.0) -> None:
        self.chat.add("you", text)
        self.learn_name(text)

        self.show_thinking()
        began = time.time()
        lines, done = self.engine.say(text)
        thought = time.time() - began
        self.clear_thinking()
        self.speak(lines)

        counted = dict(
            lines=1,
            chars=len(text),
            words=len(text.split()),
            typing_seconds=typing_seconds,
            think_seconds=thought,
            bot_lines=len(lines),
            bot_chars=sum(len(line) for line in lines),
        )
        for book in (self.session, self.chat.stats, self.config.stats):
            add_stats(book, **counted)
        self.config.mode_use[self.chat.mode] = \
            self.config.mode_use.get(self.chat.mode, 0) + 1

        self.chat.save()
        self.config.save()
        if done:
            self.running = False

    def learn_name(self, text: str) -> None:
        """Pick up "my name is Cam" without needing the /name command."""
        match = re.search(r"\b(?:my name is|call me|i am called|im called)\s+"
                          r"([A-Za-z][A-Za-z'-]{1,30})", text, re.IGNORECASE)
        if not match:
            return
        name = match.group(1).capitalize()
        if name.lower() == self.chat.user.lower():
            return
        self.chat.user = name
        self.push_state()
        self.remember_settings()

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
        for name, (draws, description) in MODES.items():
            print(f"  {self.screen.bright}{name:<8}{self.screen.off}"
                  f"{self.screen.dim}{description}{self.screen.off}")
        self.para("Every mode runs the same trained network. The difference is "
                  "how many\nanswers it samples before picking one, so pro really "
                  "does think longer\n-- that is where the wait comes from, not a "
                  "sleep.")
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
        self.remember_settings()
        self.note(f"saved as {name} ({len(self.chat.turns)} lines)"
                  + (" -- it will keep saving itself now" if first_time else ""))
        self.show_status()

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
        self.remember_settings()
        self.note(f"opened {arg}")
        self.show_status()
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
        self.show_status()

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
        self.show_status()
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
        self.remember_settings()
        self.note(f"hello, {self.chat.user}!")
        self.show_status()

    def cmd_mode(self, arg: str) -> None:
        if not arg:
            draws, description = MODES[self.chat.mode]
            self.note(f"mode {self.chat.mode}: {description} "
                      f"({draws} candidate{'' if draws == 1 else 's'})")
            return
        want = arg.lower()
        if want not in MODES:
            self.note("modes are fast, smart and pro")
            return

        self.chat.mode = want
        self.push_state()
        self.chat.save()
        self.remember_settings()
        self.note(f"mode {want}: {MODES[want][1]}")
        self.show_status()

    def cmd_swear(self, arg: str) -> None:
        if not arg:
            self.note("swear filter is "
                      + ("off -- nothing gets caught (she rarely swears anyway)"
                         if self.chat.swearing else "on -- any swearing gets filtered out"))
            return

        self.chat.swearing = arg.lower() not in ("off", "no", "false", "0")
        self.push_state()
        self.chat.save()
        self.remember_settings()
        self.note("filter off -- nothing gets caught" if self.chat.swearing
                  else "filter on -- keeping it clean from here")

    def cmd_summary(self) -> None:
        """Who did how much of the talking, and how long everybody took."""
        session, chat, all_time = self.session, self.chat.stats, self.config.stats
        elapsed = time.time() - self.started

        def typing_speed(book: dict) -> str:
            minutes = book["typing_seconds"] / 60
            if minutes < 0.05 or book["words"] < 5:
                return "not enough typing to tell yet"
            wpm = book["words"] / minutes
            cpm = book["chars"] / minutes
            return f"{wpm:.0f} wpm ({cpm:.0f} characters a minute)"

        lines = [
            "this session",
            f"    chatting for       {spell_time(elapsed)}",
            f"    you said           {plural(session['lines'], 'line')}, "
            f"{plural(session['chars'], 'character')}, "
            f"{plural(session['words'], 'word')}",
            f"    typing speed       {typing_speed(session)}",
            f"    she thought for    {spell_time(session['think_seconds'])}"
            f"   (mode {self.chat.mode})",
            f"    she said           {plural(session['bot_lines'], 'line')}, "
            f"{plural(session['bot_chars'], 'character')}",
            "",
            f"this chat ({self.chat.name or 'unsaved'})",
            f"    started            {self.chat.created}",
            f"    lines              {len(self.chat.turns)} in total, "
            f"{chat['lines']:,} from you",
            f"    you have typed     {plural(chat['chars'], 'character')}, "
            f"{plural(chat['words'], 'word')}",
            f"    typing speed       {typing_speed(chat)}",
            f"    she has thought    {spell_time(chat['think_seconds'])}",
            "",
            "all time",
            f"    saved chats        {len(Chat.saved_names())}",
            f"    you have typed     {plural(all_time['chars'], 'character')} "
            f"over {plural(all_time['lines'], 'line')}",
            f"    typing speed       {typing_speed(all_time)}",
            f"    she has thought    {spell_time(all_time['think_seconds'])}",
            f"    she has typed      {plural(all_time['bot_chars'], 'character')} back",
            f"    favourite mode     {self.favourite_mode()}",
        ]

        print()
        self.screen.boxed(lines, title="summary")
        if not sys.stdin.isatty():
            self.note("(typing speed only counts when you are really typing)")
        print()

    def favourite_mode(self) -> str:
        used = self.config.mode_use
        if not any(used.values()):
            return "none yet"
        best = max(used, key=lambda name: used[name])
        return f"{best} ({used[best]:,} of {sum(used.values()):,} turns)"

    def cmd_train(self, arg: str) -> None:
        try:
            minutes = float(arg) if arg else 20.0
        except ValueError:
            self.note("how many minutes? try /train 20")
            return
        if minutes <= 0:
            self.note("how many minutes? try /train 20")
            return

        self.note(f"training for {minutes:g} minutes -- Ctrl-C stops early "
                  f"and keeps what it learned")
        if not train(minutes, self.compiler):
            self.note("training didn't produce a model -- see the output above")
            return
        self.replay()          # pick the new weights up straight away
        self.note("back with the newly trained model")
        self.cmd_model()

    def cmd_model(self) -> None:
        lines = self.engine.control("info")
        if not lines:
            self.note("no model loaded")
            return
        self.screen.boxed(lines, title="the model")

    def cmd_who(self) -> None:
        lines = self.chat.turns
        yours = sum(1 for t in lines if t["who"] == "you")
        print()
        self.note(f"chat     {self.chat.name or 'unsaved (nothing on disk yet)'}")
        self.note(f"you      {self.chat.user or 'unnamed -- try /name Cam'}")
        self.note(f"mode     {self.chat.mode} -- {MODES[self.chat.mode][1]}")
        self.note(f"swearing {'on' if self.chat.swearing else 'off'}")
        self.note(f"lines    {len(lines)} ({yours} from you)")
        self.note(f"started  {self.chat.created}")
        info = self.engine.control("info")
        if info:
            self.note(f"model    {info[0]}")
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
        # .name keeps only the final path segment, so neither an absolute
        # path nor a "../" can point this outside ROOT -- /export always
        # writes into the repo, never over an arbitrary file.
        name = Path(arg).name if arg else default
        target = ROOT / (name or default)
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
        self.remember_settings()
        print(f"{self.screen.bright}{PROMPT_BOT}> "
              f"See you{', ' + self.chat.user if self.chat.user else ''}! "
              f"Come back whenever.{self.screen.off}")
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
        elif key == "swear":
            self.cmd_swear(arg)
        elif key == "train":
            self.cmd_train(arg)
        elif key == "model":
            self.cmd_model()
        elif key in ("summary", "stats"):
            self.cmd_summary()
        elif key == "who":
            self.cmd_who()
        elif key == "history":
            self.show_history(int(arg) if arg.isdigit() else 20)
        elif key == "export":
            self.cmd_export(arg)
        elif key == "clear":
            self.screen.boot(self.status_line())
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
        self.screen.boot(self.status_line())

        if self.chat.turns:          # opened with --open: pick up where we left off
            self.show_history(6)
            self.speak(self.engine.control("back"), remember=False)
        else:
            self.speak(self.engine.control("hello"), remember=False)

        while self.running:
            try:
                # the prompt carries the live state, so it is always right
                print(f"{self.screen.dim}{self.chat.name or 'unsaved'}·"
                      f"{self.chat.mode} {self.screen.off}"
                      f"{self.screen.you}you> {self.screen.off}",
                      end="", flush=True)
                asked_at = time.time()
                text = input()
                typing_seconds = (time.time() - asked_at
                                  if sys.stdin.isatty() else 0.0)
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
            elif text.strip().lower() in ("bye", "goodbye", "bye!", "cya",
                                          "see you", "good night", "goodnight"):
                self.user_says(text, typing_seconds)
                self.cmd_bye()
            else:
                self.user_says(text, typing_seconds)

        self.engine.close()
        return 0


# --------------------------------------------------------------------- main


def main() -> int:
    parser = argparse.ArgumentParser(
        description=f"{BOT} -- a local chat bot. The engine is C, this runner is Python."
    )
    parser.add_argument("--open", metavar="CHAT", help="open a saved chat on startup")
    parser.add_argument("--mode", choices=sorted(MODES),
                        help="start in this mode, instead of the saved one")
    parser.add_argument("--train", metavar="MINUTES", type=float,
                        help="train the model for this many minutes, then chat")
    parser.add_argument("--prepare", action="store_true",
                        help="just build the training data and exit")
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

    if args.prepare:
        return 0 if prepare_data() else 1

    if args.train:
        train(args.train, compiler)

    screen = Screen(enabled=not args.no_color and sys.stdout.isatty())
    config = Config.load()

    try:
        engine = Engine()
    except OSError as err:
        print(f"Could not start the engine: {err}", file=sys.stderr)
        return 1

    session = Session(engine, screen, compiler,
                      delays=not args.no_delay and sys.stdout.isatty(),
                      config=config)

    if args.open:
        if Chat.path_for(args.open).exists():
            session.chat = Chat.load(args.open)
            session.replay()
        else:
            print(f"No saved chat called {args.open}.", file=sys.stderr)

    if args.mode:
        session.chat.mode = args.mode

    session.push_state()
    session.remember_settings()

    if not MODEL.exists():
        # The base model ships in the repo, so this only happens if
        # model/vespra.lm went missing -- not the normal first run.
        print()
        print(f"  {screen.dim}The base model is missing -- nothing to think "
              f"with.{screen.off}")
        print(f"  {screen.dim}Rebuild one with:  "
              f"{screen.off}{screen.bright}python3 run.py --train 30{screen.off}")
        print(f"  {screen.dim}Twenty minutes gets you something that talks; "
              f"an hour or two is better.{screen.off}")
        print()

    return session.run()


if __name__ == "__main__":
    sys.exit(main())
