#!/usr/bin/env python3
"""Generate synthetic Ambi Vox LPC loader files.

The plugin's current loader reads four bytes per internal LPC-style frame:
energy/pitch/coefficient A/coefficient B/coefficient C/flags. This tool creates
those byte streams from higher-level phoneme and voice recipes so voices can be
designed without recompiling the plugin.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Phoneme:
    energy: float
    pitch: float
    a: float
    b: float
    c: float
    flags: int = 0
    duration: int = 2


PHONEMES: dict[str, Phoneme] = {
    "rest": Phoneme(0.0, 0.0, 0.5, 0.5, 0.5, 0, 3),
    "a": Phoneme(0.78, 0.42, 0.34, 0.46, 0.22, 0, 4),
    "e": Phoneme(0.75, 0.48, 0.16, 0.78, 0.28, 0, 4),
    "i": Phoneme(0.72, 0.55, 0.10, 0.86, 0.34, 0, 4),
    "o": Phoneme(0.80, 0.38, 0.72, 0.26, 0.26, 0, 5),
    "u": Phoneme(0.76, 0.34, 0.82, 0.18, 0.22, 0, 5),
    "er": Phoneme(0.74, 0.36, 0.55, 0.42, 0.36, 0, 5),
    "ar": Phoneme(0.82, 0.32, 0.72, 0.36, 0.28, 0, 5),
    "oo": Phoneme(0.78, 0.30, 0.90, 0.16, 0.22, 0, 6),
    "ee": Phoneme(0.70, 0.58, 0.08, 0.92, 0.38, 0, 6),
    "ai": Phoneme(0.76, 0.52, 0.22, 0.82, 0.30, 0, 5),
    "ow": Phoneme(0.80, 0.36, 0.64, 0.24, 0.24, 0, 5),
    "oi": Phoneme(0.76, 0.50, 0.48, 0.62, 0.34, 0, 5),
    "p": Phoneme(0.46, 0.18, 0.80, 0.20, 0.84, 2, 1),
    "t": Phoneme(0.42, 0.24, 0.62, 0.72, 0.76, 2, 1),
    "k": Phoneme(0.44, 0.20, 0.82, 0.42, 0.80, 2, 1),
    "s": Phoneme(0.34, 0.04, 0.26, 0.90, 0.96, 1, 2),
    "sh": Phoneme(0.38, 0.04, 0.34, 0.72, 0.92, 1, 2),
    "f": Phoneme(0.32, 0.05, 0.44, 0.62, 0.84, 1, 2),
    "h": Phoneme(0.36, 0.08, 0.70, 0.32, 0.70, 1, 2),
    "th": Phoneme(0.34, 0.06, 0.46, 0.62, 0.76, 1, 2),
    "r": Phoneme(0.55, 0.30, 0.62, 0.36, 0.40, 0, 2),
    "l": Phoneme(0.58, 0.34, 0.50, 0.50, 0.36, 0, 2),
    "m": Phoneme(0.54, 0.28, 0.76, 0.26, 0.32, 0, 3),
    "n": Phoneme(0.50, 0.30, 0.60, 0.46, 0.36, 0, 2),
    "ng": Phoneme(0.50, 0.28, 0.82, 0.28, 0.34, 0, 3),
    "w": Phoneme(0.55, 0.30, 0.78, 0.22, 0.28, 0, 2),
}


WORDS: dict[str, list[str]] = {
    "black": ["p", "l", "a", "k"],
    "metal": ["m", "e", "t", "a", "l"],
    "voice": ["f", "oi", "s"],
    "speak": ["s", "p", "ee", "k"],
    "spell": ["s", "p", "e", "l"],
    "sorrow": ["s", "o", "r", "ow"],
    "ash": ["a", "sh"],
    "roar": ["r", "o", "r"],
    "dark": ["t", "ar", "k"],
    "fire": ["f", "ai", "er"],
    "throat": ["th", "r", "ow", "t"],
    "night": ["n", "ai", "t"],
    "blood": ["p", "l", "u", "t"],
    "ra": ["r", "a"],
    "ka": ["k", "a"],
    "ta": ["t", "a"],
    "sa": ["s", "a"],
    "sha": ["sh", "a"],
}

WORDS["or"] = ["o", "r"]


@dataclass(frozen=True)
class VoiceRecipe:
    name: str
    energy: float = 1.0
    pitch: float = 1.0
    pitch_bias: float = 0.0
    formant_a: float = 1.0
    formant_b: float = 1.0
    formant_c: float = 1.0
    noise: float = 1.0
    plosive: float = 1.0
    duration: float = 1.0
    flutter: float = 0.0


RECIPES: dict[str, VoiceRecipe] = {
    "clear": VoiceRecipe("clear", energy=0.86, pitch=1.08, formant_b=1.10, noise=0.65, plosive=0.70),
    "speakspell": VoiceRecipe("speakspell", energy=0.95, pitch=1.35, formant_a=0.88, formant_b=1.24, formant_c=1.12, noise=0.88, duration=0.85, flutter=0.10),
    "blackmetal": VoiceRecipe("blackmetal", energy=1.10, pitch=0.72, formant_a=1.14, formant_b=0.84, formant_c=1.18, noise=1.30, plosive=1.25, duration=1.10, flutter=0.16),
    "demon": VoiceRecipe("demon", energy=1.15, pitch=0.48, formant_a=1.32, formant_b=0.64, formant_c=0.88, noise=1.05, plosive=1.10, duration=1.25, flutter=0.08),
    "choir": VoiceRecipe("choir", energy=0.78, pitch=0.95, formant_a=0.92, formant_b=1.05, formant_c=0.82, noise=0.35, plosive=0.45, duration=1.45),
    "whisper": VoiceRecipe("whisper", energy=0.55, pitch=0.35, formant_a=1.05, formant_b=1.15, formant_c=1.30, noise=1.75, plosive=0.80, duration=1.10),
    "robot": VoiceRecipe("robot", energy=0.92, pitch=1.0, formant_a=1.0, formant_b=1.0, formant_c=1.0, noise=0.55, plosive=0.90, duration=0.70),
}


def clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return max(low, min(high, value))


def tokenise(text: str) -> list[str]:
    tokens: list[str] = []
    for raw_word in text.lower().replace("-", " ").split():
        word = "".join(ch for ch in raw_word if ch.isalpha())
        if not word:
            continue
        if word in WORDS:
            tokens.extend(WORDS[word])
        else:
            tokens.extend(grapheme_word(word))
        tokens.append("rest")
    return tokens[:-1] if tokens and tokens[-1] == "rest" else tokens


def grapheme_word(word: str) -> list[str]:
    out: list[str] = []
    i = 0
    while i < len(word):
        pair = word[i : i + 2]
        if pair in PHONEMES:
            out.append(pair)
            i += 2
            continue
        ch = word[i]
        out.append({
            "b": "p",
            "c": "s" if i + 1 < len(word) and word[i + 1] in "eiy" else "k",
            "d": "t",
            "g": "k",
            "j": "sh",
            "q": "k",
            "v": "f",
            "x": "s",
            "z": "s",
        }.get(ch, ch if ch in PHONEMES else "a"))
        i += 1
    return out


def encode_frame(phoneme: Phoneme, recipe: VoiceRecipe, index: int) -> list[int]:
    flutter = recipe.flutter * (((index * 37) % 17) / 8.0 - 1.0)
    energy = clamp(phoneme.energy * recipe.energy, 0.0, 1.0)
    pitch = clamp(phoneme.pitch * recipe.pitch + recipe.pitch_bias + flutter, 0.0, 1.0)
    a = clamp(0.5 + (phoneme.a - 0.5) * recipe.formant_a, 0.0, 1.0)
    b = clamp(0.5 + (phoneme.b - 0.5) * recipe.formant_b, 0.0, 1.0)
    c = clamp(0.5 + (phoneme.c - 0.5) * recipe.formant_c, 0.0, 1.0)
    flags = phoneme.flags
    if flags & 0x01:
        c = clamp(c * recipe.noise, 0.0, 1.0)
    if flags & 0x02 and recipe.plosive < 0.5:
        flags &= ~0x02
    e = round(energy * 15)
    p = round(pitch * 63)
    ka = round(a * 63)
    kb = round(b * 63)
    kc = round(c * 63)
    return [
        ((e & 0x0F) << 4) | ((p >> 2) & 0x0F),
        ((p & 0x03) << 6) | (ka & 0x3F),
        ((kb & 0x3F) << 2) | ((kc >> 4) & 0x03),
        ((kc & 0x0F) << 4) | (flags & 0x0F),
    ]


def render_hex(text: str, recipe: VoiceRecipe) -> str:
    bytes_out: list[int] = []
    for frame_index, token in enumerate(tokenise(text)):
        phoneme = PHONEMES.get(token, PHONEMES["a"])
        repeats = max(1, round(phoneme.duration * recipe.duration))
        for repeat in range(repeats):
            bytes_out.extend(encode_frame(phoneme, recipe, frame_index + repeat))
    return " ".join(f"{byte:02X}" for byte in bytes_out) + "\n"


def generate(output: Path, phrases: Iterable[str], recipes: Iterable[str]) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for phrase in phrases:
        stem = "_".join("".join(ch for ch in word.lower() if ch.isalnum()) for word in phrase.split())
        for recipe_name in recipes:
            recipe = RECIPES[recipe_name]
            path = output / f"{stem}_{recipe.name}.hex"
            path.write_text(render_hex(phrase, recipe), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("examples/ambi-vox-lpc/generated"))
    parser.add_argument("--phrase", action="append", default=[
        "black metal voice",
        "speak spell",
        "dark fire throat",
        "sorrow ash roar",
    ])
    parser.add_argument("--recipe", action="append", choices=sorted(RECIPES), default=[])
    args = parser.parse_args()
    recipes = args.recipe or ["clear", "speakspell", "blackmetal", "demon", "choir", "whisper", "robot"]
    generate(args.output, args.phrase, recipes)


if __name__ == "__main__":
    main()
