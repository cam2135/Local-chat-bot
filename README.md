# Local Chat Bot

A tiny chat bot you run on your own machine, in the style of **ELIZA** — the
first chatbot, written by Joseph Weizenbaum at MIT in 1966. It listens, picks
out keywords, and turns your sentences back at you:

```
you> I am worried about my mother
eliza> Your mother?
you> she never listens to me
eliza> Really, never?
```

It also does one thing the 1966 original could not: if you ask it for a small
piece of **Python, JavaScript, HTML or CSS**, it will write it out for you.

There is no AI model, no API key and no internet connection involved. The whole
thing is a few hundred lines of C and Python, and it runs entirely offline.

## What you need

- **Python 3.8 or newer** — to start it
- **A C compiler** — the chat engine is written in C, and gets compiled for you
  the first time you run it

Most Macs and Linux machines already have both. If the compiler is missing, the
runner tells you exactly what to install (see [Troubleshooting](#troubleshooting)).

## How to run it

```bash
python3 run.py
```

That's it. The first run compiles the engine (about a second), then you are
chatting. On Windows use `python run.py`.

Leave the conversation by typing `bye`, `/quit`, or pressing `Ctrl-D`.

## Asking it for code

Ask in plain English — mention what you want and which language:

```
you> make me a button in css
you> write hello world in py
you> show me a loop in javascript
you> give me a form in html
```

For example:

```
you> make me a button in css
eliza> Certainly. Here is a little CSS to style a button:

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

       Now, where were we? Tell me more about what you were saying.
```

Code is printed flush against the left margin, so you can copy it straight into
a file.

If it can't tell what you want, it shows you a starting point and lists what it
knows. You can also be explicit:

```
/code css dark mode
/code python fizzbuzz
```

Roughly what is in the snippet library:

| Language | Things it can write |
| --- | --- |
| Python | hello world, input, loops, functions, reverse a string, sort a list, dictionaries, read/write files, random numbers, fizzbuzz, classes, a runnable script |
| JavaScript | hello world, functions, loops, button click handlers, map/filter/reduce, sorting, timers, `fetch`, classes, reading an input field |
| HTML | a starter page, buttons, forms, tables, lists, images, links, nav bars, cards |
| CSS | centring a box, buttons, flexbox, responsive grid, hover effects, cards, dark mode, readable type, media queries, gradients |

Anything that isn't a code request is just conversation.

## Commands

| Command | What it does |
| --- | --- |
| `/help` | reminder of all of this |
| `/code <lang> <thing>` | ask for a snippet explicitly |
| `/save` | write the conversation to `chat-log-<date>.txt` |
| `/rebuild` | recompile the C engine and restart it |
| `/quit` | leave (so do `bye` and `Ctrl-D`) |

Options: `python3 run.py --rebuild`, `--no-color`, `--cc /path/to/compiler`.

## How it works

Two pieces, on purpose:

```
   you type            run.py  ──────────►  build/eliza   (the model, in C)
                       (Python)  one line
   you read            run.py  ◄──────────  reply lines + --END--
```

**`run.py`** is only the front door. It finds a C compiler, compiles the engine
if any source file changed, starts it as a child process, and then handles the
prompt, colours, transcript and slash commands.

**`engine/`** is the bot itself. For each line it:

1. normalises the text — lower case, punctuation split into clauses, and
   contractions expanded (`i'm` → `i am`), so the script needs only one spelling;
2. offers the line to `codegen.c` first, in case you asked for code;
3. otherwise finds the highest ranked keyword in the sentence — `computer` beats
   `my` beats `i` — and matches that keyword's decomposition patterns, where
   `*` stands for any run of words;
4. flips the pronouns in whatever the `*` captured (`my job` → `your job`) and
   pastes it into a reply template (`How long have you been %2?`);
5. if nothing matches, it either brings back something you said earlier about
   "my ..." — the original ELIZA did this too — or falls back to `Please go on.`

Replies rotate through each rule's list rather than repeating, which is why
saying the same thing twice gets you two different answers.

The engine talks over stdin/stdout, one reply block per turn, ending with a
`--END--` sentinel line (`--END--QUIT--` when the conversation is over). That
sentinel is what lets a multi-line code snippet come back as a single reply.

You can run the engine on its own, without Python, if you want to see the raw
protocol:

```bash
./build/eliza
```

## Project layout

```
run.py            the Python runner: build, then chat
engine/
  eliza.c         the model: normalising, keywords, pattern matching, memory
  script.h        the personality: keywords, patterns, reply templates
  codegen.c       the Python/JavaScript/HTML/CSS snippet library
  codegen.h
tests/smoke.py    builds it and checks a real conversation
build/            the compiled engine (created for you, not in git)
```

## Making it your own

**A new thing to say.** Open `engine/script.h` and add to `KEYWORDS`:

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

The first field is the words that should select it. Then run `python3 run.py`
again — it notices the file changed and recompiles.

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

**Odd characters like `←[36m` in the output** — your terminal doesn't do
colours. Run `python3 run.py --no-color`.

**It stopped recognising a change you made** — force it: `python3 run.py --rebuild`.

**Checking everything still works**

```bash
python3 tests/smoke.py
```

## A note on what this is

ELIZA has no idea what you are talking about. Weizenbaum wrote it to show how
little it takes to *seem* understanding — and was unsettled by how readily
people confided in it anyway. This one is the same trick, plus a box of code
snippets. It's a toy, and a nice one to read: start at `respond()` in
`engine/eliza.c`.
