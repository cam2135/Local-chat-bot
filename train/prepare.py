#!/usr/bin/env python3
"""
prepare.py -- turn raw dialogue into something the model can learn from.

It takes the Cornell Movie-Dialogs corpus (300,000 lines of real conversation),
cleans it up, lays it out as turns, builds a vocabulary, and writes the whole
thing out as a stream of token numbers for engine/train_lm.c to train on.

    python3 train/prepare.py            # downloads the corpus if it is missing

Everything lands in data/:

    movie_lines.txt         raw corpus (downloaded once)
    movie_conversations.txt raw corpus (downloaded once)
    vocab.txt               one word per line, most common first
    corpus.bin              the training data: little endian uint16 token ids
    corpus.txt              the same thing readable, for when you are curious
"""

from __future__ import annotations

import re
import struct
import sys
import urllib.request
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"

MIRROR = "https://raw.githubusercontent.com/Conchylicultor/DeepQA/master/data/cornell"
FILES = ["movie_lines.txt", "movie_conversations.txt"]

VOCAB_SIZE = 6000          # words kept; everything else becomes <unk>
MIN_WORDS = 2              # skip one word lines, they teach very little
MAX_WORDS = 24             # skip speeches
MAX_UNK_RATE = 0.15        # skip lines that are mostly words we do not know

# Special tokens. Keep these first and in this order -- the C side assumes it.
UNK, USER, BOT, END = "<unk>", "<user>", "<bot>", "<end>"
SPECIALS = [UNK, USER, BOT, END]

WORD = re.compile(r"[a-z']+|[.,!?]")


def download() -> None:
    DATA.mkdir(exist_ok=True)
    for name in FILES:
        target = DATA / name
        if target.exists() and target.stat().st_size > 1000:
            continue
        url = f"{MIRROR}/{name}"
        print(f"downloading {name} ...", flush=True)
        try:
            with urllib.request.urlopen(url, timeout=300) as response:
                target.write_bytes(response.read())
        except Exception as err:
            sys.exit(f"could not download {name}: {err}\n"
                     f"Put it in {DATA} by hand and run this again.")
        print(f"  {target.stat().st_size // 1024} KB")


def tokenize(text: str) -> list[str]:
    """Lower case words and the punctuation that changes a sentence's tone."""
    text = text.replace("--", " ").replace("...", " . ")
    return WORD.findall(text.lower())


def load_lines() -> dict[str, list[str]]:
    """id -> tokens, for every line that is worth learning from."""
    lines: dict[str, list[str]] = {}
    raw = (DATA / "movie_lines.txt").read_text(encoding="iso-8859-1")

    for row in raw.split("\n"):
        parts = row.split(" +++$+++ ")
        if len(parts) != 5:
            continue
        words = tokenize(parts[4])
        if MIN_WORDS <= len(words) <= MAX_WORDS:
            lines[parts[0]] = words
    return lines


def load_conversations() -> list[list[str]]:
    """Each conversation, as a list of line ids in order."""
    conversations = []
    raw = (DATA / "movie_conversations.txt").read_text(encoding="iso-8859-1")

    for row in raw.split("\n"):
        parts = row.split(" +++$+++ ")
        if len(parts) != 4:
            continue
        ids = re.findall(r"L\d+", parts[3])
        if len(ids) >= 2:
            conversations.append(ids)
    return conversations


def main() -> int:
    download()

    print("reading the corpus ...", flush=True)
    lines = load_lines()
    conversations = load_conversations()
    print(f"  {len(lines):,} usable lines in {len(conversations):,} conversations")

    # Turns, alternating speakers, exactly as the model will see them at chat time.
    dialogues: list[list[list[str]]] = []
    for ids in conversations:
        turns = [lines[i] for i in ids if i in lines]
        if len(turns) >= 2:
            dialogues.append(turns)
    print(f"  {sum(len(d) for d in dialogues):,} turns kept")

    print("building the vocabulary ...", flush=True)
    counts = Counter(word for d in dialogues for turn in d for word in turn)
    common = [word for word, _ in counts.most_common(VOCAB_SIZE - len(SPECIALS))]
    vocab = SPECIALS + common
    index = {word: number for number, word in enumerate(vocab)}
    covered = sum(counts[w] for w in common) / max(1, sum(counts.values()))
    print(f"  {len(vocab):,} words, covering {covered:.1%} of the corpus")

    print("writing the training stream ...", flush=True)
    stream: list[int] = []
    readable: list[str] = []
    skipped = 0

    for turns in dialogues:
        emitted = 0
        for position, turn in enumerate(turns):
            numbers = [index.get(word, 0) for word in turn]
            if numbers.count(0) / len(numbers) > MAX_UNK_RATE:
                # Stop here rather than skipping: two turns either side of a
                # gap were never actually said one after the other, and the
                # model would learn that gap as if it were a real reply.
                skipped += 1
                break
            speaker = USER if position % 2 == 0 else BOT
            stream.append(index[speaker])
            stream.extend(numbers)
            readable.append(f"{speaker} {' '.join(turn)}")
            emitted += 1

        if emitted:
            stream.append(index[END])
            readable.append(END)

    (DATA / "vocab.txt").write_text("\n".join(vocab) + "\n", encoding="utf-8")
    (DATA / "corpus.bin").write_bytes(
        struct.pack(f"<{len(stream)}H", *stream))
    (DATA / "corpus.txt").write_text("\n".join(readable) + "\n", encoding="utf-8")

    print(f"  {len(stream):,} tokens written to data/corpus.bin")
    print(f"  {skipped:,} turns skipped as too unusual")
    print()
    print("Now train on it:  python3 run.py --train 30")
    return 0


if __name__ == "__main__":
    sys.exit(main())
