# Vespra 
# Vespra   

A small language model you train yourself, and then chat with in your terminal.

Not a wrapper around somebody else's model — there is no Ollama here, nothing is
downloaded from Hugging Face, no API key, no network at chat time. The neural
network is about 1,300 lines of C in `engine/`, it starts from random numbers,
and it learns to talk by reading 250,000 real conversations on your own machine.

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

## What it actually is

A GPT — the same design as the models everyone talks about, just very small:

| | Vespra | TinyLlama, for scale |
| --- | --- | --- |
| parameters | 1.6 million | 1.1 billion (700× bigger) |
| layers | 4 | 22 |
| trained on | 2.5M words of film dialogue | 3 trillion tokens |
| training | an hour on your CPU | 90 days on 16 A100 GPUs |
| written in | C, from scratch, no libraries | PyTorch |

Token embeddings, learned positions, causal multi-head self-attention, feed
forward layers, layer norm, weight tying, backpropagation, AdamW, gradient
clipping, warmup and cosine decay — the real machinery, all of it in
`engine/tinylm.c`.

**Be realistic about what a model this size does.** It writes real English
sentences and picks up the tone of what you say, but it is not going to answer
questions, remember facts, or hold a long argument — there simply are not enough
weights in it for that. Train it longer and it gets noticeably better; it will
never be ChatGPT. What it *is* is a real language model that you trained, and
that you can read every line of.

## Getting it talking

```bash
python3 train/prepare.py      # fetch and clean the training data (once, ~40MB)
python3 run.py --train 30     # train for 30 minutes
python3 run.py                # chat
```

The first command is the only time anything is downloaded, and what it downloads
is **text to learn from**, not a model: the Cornell Movie-Dialogs Corpus, 300,000
lines of conversation from film scripts. The weights are always yours, trained
here.

Training prints its progress:

```
model    6000 words, 128 wide, 4 layers, 4 heads, 64 token memory
weights  1.57M
corpus   2.46M tokens
training for 30 minutes -- Ctrl-C stops early and keeps the model

step 4453   loss 3.890  (perplexity 48.9)  15m 26s elapsed, 14m 34s left, 4.4 steps/s
```

Loss is how surprised the model is by the next word. Guessing at random from a
6,000 word vocabulary scores 8.7. Under 4.0 it is writing proper sentences.
Every extra half hour helps, and `/train 30` inside the chat carries on from
where it left off — nothing is thrown away.

## Requirements

- **Python 3.8+** — for the runner and the data preparation
- **A C compiler** — `cc`, `gcc` or `clang`; OpenMP is used if it is there

## Chatting

```
vespra> Hey. What is going on with you today?
unsaved·smart you> i saw a good film last night
vespra> We're crazy about you, ma. Don't think about that?
unsaved·smart you> what did you think of it
vespra> I'm sorry, sir.
```

That is honest output from an hour of training. It is grammatical, it is in the
right register, and it is only loosely connected to what you said — that is what
1.6M parameters buys you. Train it for an evening and it holds a thread better.

## Commands

`/help` prints the short list; `/HELP` prints the long one with everything
explained.

| Command | What it does |
| --- | --- |
| `/train [minutes]` | train the model for longer, carrying on from where it is |
| `/model` | size, training steps, how much text it has read |
| `/save <chatname>` | save this conversation under a name |
| `/open <chatname>` | reopen a saved conversation and feed it back to the model |
| `/list` | list your saved chats |
| `/rm <chatname>` \| `/rm ALL` | delete a chat, or all of them — asks first |
| `/rename <newname>` | rename the chat that is open |
| `/new [chatname]` | start a fresh conversation |
| `/name <yourname>` | tell it what to call you |
| `/mode fast\|smart\|pro` | how many answers it draws (also `/fast`, `/smart`, `/pro`) |
| `/code <lang> <thing>` | ask for a code snippet outright |
| `/swear on\|off` | filter out the language it learned from film scripts |
| `/summary` | thinking time, characters typed, average wpm, and more |
| `/who` | the open chat, your name, the mode, the model |
| `/history [n]` | show the last n lines again |
| `/export [file.txt]` | write the conversation out as plain text |
| `/clear` | clear the screen |
| `/rebuild` | recompile the C |
| `/bye` | save and leave |

`/save_work` works the same as `/save work` — the same goes for `/open_work` and
`/rm_ALL`.

Options: `python3 run.py --train 30`, `--open work`, `--mode pro`, `--prepare`,
`--rebuild`, `--no-color`, `--cc /path/to/compiler`.

## Modes

The model is the same in all three. What changes is how many answers it samples
before choosing one, so the wait is real work rather than a pause:

| Mode | What it does |
| --- | --- |
| `fast` | one draw, straight back to you |
| `smart` | draws 3, keeps the one it is most confident in |
| `pro` | draws 8 and keeps the best — slowest, and it shows |

Confidence is the model's own average log probability for the words it chose, so
"best" means the answer it was surest of, not the longest.

## Saved chats

Once a chat has a name it saves itself after every line:

```
unsaved·smart you> /save work
  saved as work (4 lines) -- it will keep saving itself now
  ──────────────────────────────────────────────
  chat: work   mode: smart   you: Sam   lines: 4
  ──────────────────────────────────────────────
```

`/open work` loads it and quietly feeds the whole conversation back through the
model first, so it carries on with what was said in its context window rather
than starting cold.

Chats are plain JSON in `chats/`, one file each. Settings — mode, your name, the
swearing filter — live in `config.json`, so `/pro` is still `/pro` tomorrow.

## Asking it for code

Code requests do **not** go to the model, and that is deliberate: a 1.6M
parameter network trained on film dialogue cannot write working CSS, and
pretending otherwise would just waste your time. Those come from a hand-written
snippet library in `engine/codegen.c`.

```
unsaved·smart you> make me a button in css
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
--- end ---
```

Python, JavaScript, HTML and CSS: loops, functions, classes, files, fizzbuzz,
click handlers, `fetch`, forms, tables, flexbox, grid, dark mode and more.

## How it works

```
   you type          run.py  ─────────►  build/vespra   the chat program
                     (Python) >one line       │
                                              ├── engine/tinylm.c  the network
                                              └── model/vespra.lm  the weights
   you read          run.py  ◄─────────  reply + --END--
```

Every turn:

1. your line is split into words the same way the training data was, and turned
   into token numbers;
2. it is appended to the conversation history, tagged `<user>`;
3. a `<bot>` marker is added, and the model is asked what comes next;
4. words are sampled one at a time — temperature, top-k, and a repetition
   penalty — until it produces a `<user>` or `<end>` marker, or hits the length
   limit;
5. in smart and pro mode several answers are drawn and the most confident kept;
6. the answer is turned back into text and added to the history.

The model sees the last 64 tokens, so it reads your whole message and the few
turns before it — there is no keyword matching anywhere in the path.

`train/prepare.py` builds the vocabulary and the token stream;
`engine/train_lm.c` is the training loop; `engine/tinylm.c` is the network
itself, forward and backward; `engine/vespra.c` is the chat program.

## Project layout

```
run.py             the runner: build, train, chat, commands, saved chats
train/prepare.py   corpus -> vocabulary + token stream
engine/
  tinylm.h/.c      the transformer: forward, backward, AdamW, sampling
  train_lm.c       the training loop
  vespra.c         the chat program
  codegen.c        the hand-written code snippets
tests/smoke.py     builds it, trains it, checks the loss falls, then chats
data/              corpus, vocabulary, token stream   (not in git)
model/vespra.lm    the trained weights                (not in git, yours)
chats/             your saved conversations           (not in git)
```

## Training it on something else

The model learns from whatever `data/corpus.bin` contains. To use your own text,
edit `train/prepare.py` — it wants alternating turns, tagged `<user>` and
`<bot>`, one long stream of token numbers. Your own chat logs, a book, a script:
anything with enough words in it. Then:

```bash
python3 run.py --train 60
```

Bigger model, if you have the patience:

```bash
./build/train --minutes 120 --dim 192 --layers 6
```

Delete `model/vespra.lm` first when you change the shape — the old weights will
not fit the new network.

## Troubleshooting

**"There is no trained model yet"** — `python3 run.py --train 30`.

**"No training data"** — `python3 train/prepare.py`.

**It says something odd** — that is a 1.6M parameter model. `/train 60` a few
times, and try `/pro`.

**No C compiler:** macOS `xcode-select --install`; Debian/Ubuntu
`sudo apt install build-essential`; Fedora `sudo dnf install gcc`; Windows
MinGW-w64 or WSL.

**Checking everything works**

```bash
python3 tests/smoke.py
```

It compiles both programs, trains for 36 seconds, checks the loss actually
falls, then holds a conversation, saves it, reopens it and deletes it again —
in a temporary folder, so your own model and chats are never touched.
