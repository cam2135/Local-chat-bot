# Vespra   

A small chat bot you run in your own terminal. She is friendly — nosy, warm,
quick with an opinion and a bad joke — not one of those bots that only ever asks
how that makes you feel. Written in C, with an old green screen to match.

```
  v     v  eeeeeee   sssss   pppppp   rrrrrr     aaa
  v     v  e        s        p     p  r     r   a   a
  v     v  e        s        p     p  r     r  a     a
  v     v  eeeee     sssss   pppppp   rrrrrr   aaaaaaa
   v   v   e              s  p        r   r    a     a
    v v    e              s  p        r    r   a     a
     v     eeeeeee   sssss   p        r     r  a     a

  ────────────────────────────────────────────────────────────
  local chat terminal   v1.0   engine: c   runner: python 3.11
  chat: unsaved   mode: smart   you: -   lines: 0
  ────────────────────────────────────────────────────────────
```

Every big letter is drawn out of little copies of itself. The bar under it, and
the `unsaved·smart` in front of the prompt, are rebuilt from the live state every
time they are drawn, so they always show the chat and mode you are actually in.

```
vespra> Hey! I am Vespra. What is going on with you today?
unsaved·smart you> my name is Sam
vespra> Nice to meet you properly!
unsaved·smart you> i am tired
vespra> Sam, long day? Or just one of those weeks?
unsaved·smart you> my laptop is dying
vespra> Your laptop is dying, eh. Tell me about that.
unsaved·smart you> tell me a joke
vespra> Alright: why do programmers prefer dark mode?
        Because light attracts bugs.
```

She saves conversations, remembers them when you reopen them, picks up your name
from the conversation, swears back if you swear at her, and writes small pieces
of **Python, JavaScript, HTML or CSS** when you ask. There is no AI model, no API
key and no internet connection anywhere in it — a few hundred lines of C and
Python, entirely offline.

## What you need

- **Python 3.8 or newer** — to start it
- **A C compiler** — the chat engine is written in C, and gets compiled for you
  the first time you run it

Most Macs and Linux machines have both already. If the compiler is missing, the
runner tells you exactly what to install (see [Troubleshooting](#troubleshooting)).

## How to run it

```bash
python3 run.py
```

That's it. The first run compiles the engine (about a second), then you are
chatting. On Windows use `python run.py`.

Type `/bye` to leave, or press `Ctrl-D`, or just say "bye".

## Commands

`/help` prints the short list; `/HELP` prints the long one with everything
explained.

| Command | What it does |
| --- | --- |
| `/help` | the short list of commands |
| `/HELP` | the long list, with what everything does |
| `/save <chatname>` | save this conversation under a name |
| `/open <chatname>` | reopen a saved conversation, memory and all |
| `/list` | list your saved chats |
| `/rm <chatname>` | delete one chat — asks `y/n` first |
| `/rm ALL` | delete every saved chat — asks twice |
| `/rename <newname>` | rename the chat that is open |
| `/new [chatname]` | start a fresh conversation |
| `/name <yourname>` | tell it what to call you |
| `/mode fast\|smart\|pro` | how hard it thinks (also `/fast`, `/smart`, `/pro`) |
| `/code <lang> <thing>` | ask for a snippet outright |
| `/swear on\|off` | whether she swears back (on by default) |
| `/summary` | thinking time, characters typed, average wpm, and more |
| `/who` | the open chat, your name, the mode, the turn count |
| `/history [n]` | show the last n lines again |
| `/export [file.txt]` | write the conversation out as plain text |
| `/clear` | clear the screen and redraw the boot screen |
| `/rebuild` | recompile the C engine, keeping the conversation |
| `/bye` | save and leave |

`/save_work` works the same as `/save work`, if you prefer typing it that way —
the same goes for `/open_work` and `/rm_ALL`.

Options: `python3 run.py --open work`, `--mode pro`, `--rebuild`, `--no-color`,
`--no-delay`, `--cc /path/to/compiler`.

## Saved chats and memory

```
unsaved·smart you> my name is Sam
vespra> Nice to meet you properly!
unsaved·smart you> my boss has been unbearable
vespra> Ah. Good boss or one of the other kind?
unsaved·smart you> /save work
  saved as work (4 lines) -- it will keep saving itself now
  ──────────────────────────────────────────────
  chat: work   mode: smart   you: Sam   lines: 4
  ──────────────────────────────────────────────
```

She picks your name up out of the conversation — "my name is Sam" or "call me
Sam" is enough, no command needed.

Once a chat has a name it saves itself after every single line, so nothing is
lost if you close the window. Come back later:

```
work·pro you> /open work
  opened work
  ───────────────────────────────────────────
  chat: work   mode: pro   you: Sam   lines: 4
  ───────────────────────────────────────────

  you> my boss has been unbearable
  vespra> Ah. Good boss or one of the other kind?

vespra> Hey, you are back. I still have everything we talked about. Carry on.
work·pro you> anyway, where were we
vespra> Sam, ha, alright. Tell me more.
        Hang on, earlier you mentioned your boss has been unbearable. What is
        happening there?
```

It genuinely picks up where it left off. Opening a chat quietly replays every
earlier line through a fresh engine, so the things you told it about "my ..."
are back in its memory, its replies carry on rotating from where they were, and
your name and mode come back with it.

Chats are plain JSON in `chats/`, one file per conversation — yours to read,
copy, move or delete. They are not committed to git.

## Modes

| Mode | What you get |
| --- | --- |
| `fast` | one short sentence, no waiting |
| `smart` | the full answer, after a moment's thought |
| `pro` | an answer plus a second thought, after a longer pause |

```
work·fast you> i am tired of all of this
vespra> Long day?

work·pro you> i am tired of all of this
vespra> Sam, or just one of those weeks?
        Is that a new thing, or has it been brewing a while?
```

Being straight with you: the modes change **how much she says and how long she
waits**, not how clever she is underneath. It is the same pattern matcher in all
three — there is no bigger model to switch to. `pro` composes a longer reply,
uses your name and brings back things you said earlier; `fast` clips to one
sentence and answers instantly.

The mode is saved with the chat, and the prompt always shows which one you are
in.

## Swearing

Swear at her and she gives it back, a bit more each time — amused first, then
joining in, then properly matching you. Stop, and after a few clean turns she
settles down again.

```
unsaved·smart you> this is fucking ridiculous
vespra> Ha! Alright, strong words. What has got you worked up?
unsaved·smart you> the whole shitty project is late
vespra> Ah hell, no wonder you are annoyed.
unsaved·smart you> fuck this
vespra> Right, screw them. Tell me the whole bloody story.
```

She only ever swears after you do, it is ordinary profanity and nothing worse,
and `/swear off` turns it off entirely — kept per chat, so a work chat can stay
clean while another does not.

## Settings that stick

Whatever you were last using is what you get next time. Set `/pro`, close the
window, come back tomorrow — still `/pro`. Same for `/swear off` and your name.

```
$ python3 run.py           # yesterday you typed /pro
  chat: unsaved   mode: pro   you: Sam   lines: 0
```

It lives in `config.json` next to `run.py`: the mode, the swearing setting, your
name, the last chat you had open and the running totals `/summary` reports.
Saved chats keep their own copy too, so opening one puts you back in the mode
that chat was in. Delete `config.json` to start fresh — nothing else depends on
it. It is not committed to git.

## /summary

Who did the talking, and how long everybody took over it:

```
  ────────────────────────────────────────────────────────
  summary
  this session
      chatting for       18m 42s
      you said           37 lines, 1,204 characters, 233 words
      typing speed       61 wpm (317 characters a minute)
      she thought for    1m 51s   (mode pro)
      she said           74 lines, 3,918 characters

  this chat (work)
      started            2026-08-14 11:34
      lines              128 in total, 64 from you
      you have typed     4,301 characters, 812 words
      typing speed       58 wpm (301 characters a minute)
      she has thought    6m 12s

  all time
      saved chats        4
      you have typed     19,882 characters over 388 lines
      typing speed       59 wpm (308 characters a minute)
      she has thought    24m 07s
      she has typed      61,204 characters back
      favourite mode     smart (241 of 388 turns)
  ────────────────────────────────────────────────────────
```

Thinking time is the real measured pause she made you wait, and words a minute
is measured from when the prompt appears to when you press enter — so it only
counts when you are genuinely typing at a terminal, not when input is piped in.
The three blocks are this run, this conversation across all its sessions, and
everything you have ever typed at her.

## Asking it for code

Ask in plain English — say what you want and name the language:

```
you> make me a button in css
you> write hello world in py
you> show me a loop in javascript
you> give me a form in html
```

For example:

```
you> make me a button in css
vespra> Certainly. Here is a little CSS to style a button:

--- css ---
.btn {
  padding: 0.6rem 1.2rem;
  border: none;
  border-radius: 8px;
  background: #2f6fed;
  color: #fff;
  font: inherit;
  cursor: pointer;
  transition: background 0.15s ease;
}

.btn:hover  { background: #2559c4; }
.btn:active { transform: translateY(1px); }
.btn:focus-visible { outline: 3px solid #9bc0ff; outline-offset: 2px; }
--- end ---
```

Code prints flush against the left margin, so you can copy it straight into a
file. If it can't tell what you want, it shows a starting point and lists what
it knows. You can also be explicit: `/code css dark mode`.

| Language | Things it can write |
| --- | --- |
| Python | hello world, input, loops, functions, reverse a string, sort a list, dictionaries, read/write files, random numbers, fizzbuzz, classes, a runnable script |
| JavaScript | hello world, functions, loops, button click handlers, map/filter/reduce, sorting, timers, `fetch`, classes, reading an input field |
| HTML | a starter page, buttons, forms, tables, lists, images, links, nav bars, cards |
| CSS | centring a box, buttons, flexbox, responsive grid, hover effects, cards, dark mode, readable type, media queries, gradients |

Anything that isn't a code request is just conversation.

## How it works

Two pieces, on purpose:

```
   you type            run.py  ──────────►  build/vespra  (the model, in C)
                       (Python)  >one line
   you read            run.py  ◄──────────  reply lines + --END--
```

**`run.py`** is the front door. It finds a C compiler, compiles the engine when
a source file changes, starts it as a child process, and then handles the boot
screen, the green, the prompt, the commands and the saved chats.

**`engine/`** is the bot itself. For each line it:

1. normalises the text — lower case, punctuation split into clauses, and
   contractions expanded (`i'm` → `i am`, `wanna` → `want to`), so the script
   needs only one spelling;
2. offers the line to `codegen.c` first, in case you asked for code;
3. checks whether you swore, and if so answers in kind at the current level;
4. otherwise finds the highest ranked keyword in the sentence — topics like
   `pizza` and `boss` beat `my`, which beats the catch-all `i` — and matches
   that keyword's decomposition patterns, where `*` stands for any run of
   words;
5. flips the pronouns in whatever the `*` captured (`my job` → `your job`) and
   pastes it into a reply template (`Your %2, eh. Tell me about that.`);
6. if nothing matches, it either brings back something you said earlier about
   "my ..." or falls back to `Go on, I am listening.`;
7. shapes the answer for the current mode.

The script in `engine/script.h` is about a hundred keywords deep — feelings,
food, music, games, school, work, pets, code, weather, jokes, and the general
`i`/`you`/`my` rules underneath — and replies rotate through each rule's list
rather than repeating, which is why saying the same thing twice gets you two
different answers.

Lines going into the engine are tagged: `>text` is speech, `!command` is a
setting from the runner (`!mode pro`, `!name Sam`, `!swear off`,
`!replay <line>`). Replies
come back as one or more lines ending in a `--END--` sentinel — that sentinel is
what lets a multi-line code snippet arrive as a single reply. A line with no tag
counts as speech, so you can run the engine on its own without Python:

```bash
./build/vespra
```

## Project layout

```
run.py            the Python runner: build, boot screen, chat, commands, saving
engine/
  vespra.c        the model: normalising, keywords, matching, memory, modes
  script.h        the personality: keywords, patterns, reply templates
  codegen.c       the Python/JavaScript/HTML/CSS snippet library
  codegen.h
tests/smoke.py    builds it and checks a real conversation, end to end
chats/            your saved conversations (created when you save one)
build/            the compiled engine (created for you, not in git)
```

## Making it your own

**A new thing to say.** Open `engine/script.h` — that file is the whole
personality — and add to `KEYWORDS`:

```c
{ "money", 3, {
    { "* money *", { "Why does money worry you?",
                     "Is it really about the money?", NULL } },
    { NULL, { NULL } } } },
```

Higher `rank` wins when several keywords appear in one sentence. `*` matches any
number of words, and `%1`, `%2` … refer to what each `*` captured, in order.

**A new snippet.** Open `engine/codegen.c` and add an entry to `PY`, `JS`,
`HTML` or `CSS`:

```c
{ "email validate regex", "check an email address", {
    "import re",
    "",
    "def looks_like_email(text):",
    "    return re.fullmatch(r\"[^@\\s]+@[^@\\s]+\\.[a-z]{2,}\", text) is not None",
    NULL } },
```

The first field is the words that should select it.

**A different name.** The bot's name is the `BOT` constant at the top of
`run.py`, the greeting in `engine/script.h`, and the letters in `LETTERS` that
the boot screen is drawn from. Any letter it doesn't have art for is drawn as a
solid block, so add art for yours if you rename it.

Either way, run `python3 run.py` again — it notices the change and recompiles.

## Troubleshooting

**"I could not find a C compiler"**

| System | Install |
| --- | --- |
| macOS | `xcode-select --install` |
| Debian / Ubuntu | `sudo apt install build-essential` |
| Fedora | `sudo dnf install gcc` |
| Arch | `sudo pacman -S gcc` |
| Windows | [MinGW-w64](https://www.mingw-w64.org/), or run it inside WSL |

If your compiler is somewhere unusual: `python3 run.py --cc /path/to/gcc`.

**Odd characters like `←[1;32m` in the output** — your terminal doesn't do
colours. Run `python3 run.py --no-color`.

**The pauses annoy you** — `python3 run.py --no-delay`, or use `/fast`.

**It stopped noticing a change you made** — `python3 run.py --rebuild`.

**Checking everything still works**

```bash
python3 tests/smoke.py
```

Forty checks: it builds the engine, holds a conversation, tells a joke, saves
the chat, reopens it, confirms the memory survived, swears at her and checks she
swears back, checks `/swear off` keeps her clean, checks the status bar is never
stale, and deletes everything again — all inside a temporary folder, so your own
chats are never touched.

## A note on what this is

Vespra has no idea what you are talking about. Bots like this were written in
the mid-1960s to show how little it takes to *seem* understanding, and their
authors were unsettled by how readily people confided in them anyway. This one is
the same trick with a friendlier script — plus jokes, a box of code snippets,
somewhere to keep your conversations, and a mouth on her if you start it.

It's a toy, and a nice one to read: start at `respond()` in `engine/vespra.c`.
