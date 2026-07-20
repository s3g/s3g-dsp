#!/usr/bin/env python3
"""Build a small UTAU-style voicebank from one labeled recording.

The input is a mono or stereo WAV plus an ordered phoneme list. The first pass
uses envelope slicing, then writes per-phoneme WAV files, voicebank.json, and
oto.ini. A marker CSV can override automatic slicing when hand correction is
needed.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import wave
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


DEFAULT_PHONEMES = (
    "a e i o u "
    "ka ke ki ko ku "
    "sa se si so su "
    "ta te ti to tu "
    "na ne ni no nu "
    "ma me mi mo mu "
    "ra re ri ro ru"
)


@dataclass
class Segment:
    alias: str
    start: int
    end: int
    offset_ms: float
    consonant_ms: float
    loop_start_ms: float
    loop_end_ms: float
    cutoff_ms: float
    base_midi: int


def read_wav(path: Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())

    if sample_width == 1:
        values = [(byte - 128) / 128.0 for byte in frames]
    elif sample_width == 2:
        count = len(frames) // 2
        values = [sample / 32768.0 for sample in struct.unpack(f"<{count}h", frames)]
    elif sample_width == 3:
        values = []
        for index in range(0, len(frames), 3):
            raw = frames[index] | (frames[index + 1] << 8) | (frames[index + 2] << 16)
            if raw & 0x800000:
                raw -= 0x1000000
            values.append(raw / 8388608.0)
    elif sample_width == 4:
        count = len(frames) // 4
        values = [sample / 2147483648.0 for sample in struct.unpack(f"<{count}i", frames)]
    else:
        raise ValueError(f"Unsupported WAV sample width: {sample_width}")

    if channels > 1:
        mono = []
        for index in range(0, len(values), channels):
            mono.append(sum(values[index:index + channels]) / channels)
        values = mono

    peak = max((abs(value) for value in values), default=0.0)
    if peak > 1.0:
        values = [value / peak for value in values]
    return sample_rate, values


def write_wav(path: Path, sample_rate: int, samples: Iterable[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    for sample in samples:
        value = max(-1.0, min(1.0, sample))
        data.extend(struct.pack("<h", int(round(value * 32767.0))))
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(bytes(data))


def read_phonemes(path: Path | None, text: str | None) -> list[str]:
    if path:
        source = path.read_text(encoding="utf-8")
    else:
        source = text or DEFAULT_PHONEMES
    tokens = []
    for token in source.replace(",", " ").split():
        if token.strip():
            tokens.append(token.strip())
    if not tokens:
        raise ValueError("No phoneme tokens supplied")
    return tokens


def rms_envelope(samples: list[float], window: int, hop: int) -> list[float]:
    if not samples:
        return []
    envelope = []
    square_sum = sum(sample * sample for sample in samples[:window])
    for start in range(0, max(1, len(samples) - window + 1), hop):
        if start > 0:
            old_start = start - hop
            old_end = min(old_start + hop, len(samples))
            new_start = min(start + window - hop, len(samples))
            new_end = min(start + window, len(samples))
            square_sum -= sum(sample * sample for sample in samples[old_start:old_end])
            square_sum += sum(sample * sample for sample in samples[new_start:new_end])
        envelope.append(math.sqrt(max(0.0, square_sum) / max(1, window)))
    return envelope


def active_bounds(samples: list[float], sample_rate: int, threshold_db: float) -> tuple[int, int]:
    window = max(64, int(sample_rate * 0.020))
    hop = max(16, int(sample_rate * 0.005))
    envelope = rms_envelope(samples, window, hop)
    peak = max(envelope, default=0.0)
    if peak <= 0.000001:
        return 0, len(samples)
    threshold = peak * math.pow(10.0, threshold_db / 20.0)
    active = [index for index, value in enumerate(envelope) if value >= threshold]
    if not active:
        return 0, len(samples)
    pad = int(sample_rate * 0.050)
    start = max(0, active[0] * hop - pad)
    end = min(len(samples), active[-1] * hop + window + pad)
    return start, max(start + 1, end)


def active_regions(samples: list[float], sample_rate: int, threshold_db: float) -> list[tuple[int, int]]:
    window = max(64, int(sample_rate * 0.020))
    hop = max(16, int(sample_rate * 0.005))
    envelope = rms_envelope(samples, window, hop)
    peak = max(envelope, default=0.0)
    if peak <= 0.000001:
        return []
    threshold = peak * math.pow(10.0, threshold_db / 20.0)
    min_gap_frames = max(1, int(0.060 * sample_rate / hop))
    pad = int(sample_rate * 0.030)
    regions = []
    start_frame = None
    last_active = None
    for frame, value in enumerate(envelope):
        if value >= threshold:
            if start_frame is None:
                start_frame = frame
            last_active = frame
        elif start_frame is not None and last_active is not None and frame - last_active >= min_gap_frames:
            start = max(0, start_frame * hop - pad)
            end = min(len(samples), last_active * hop + window + pad)
            if end > start:
                regions.append((start, end))
            start_frame = None
            last_active = None
    if start_frame is not None and last_active is not None:
        start = max(0, start_frame * hop - pad)
        end = min(len(samples), last_active * hop + window + pad)
        if end > start:
            regions.append((start, end))
    return regions


def detect_boundaries(samples: list[float], sample_rate: int, count: int, threshold_db: float) -> list[tuple[int, int]]:
    regions = active_regions(samples, sample_rate, threshold_db)
    if len(regions) == count:
        return regions
    start, end = active_bounds(samples, sample_rate, threshold_db)
    if count == 1:
        return [(start, end)]
    duration = end - start
    return [
        (
            start + int(round(duration * index / count)),
            start + int(round(duration * (index + 1) / count)),
        )
        for index in range(count)
    ]


def read_markers(path: Path, phonemes: list[str], sample_rate: int) -> list[tuple[int, int]]:
    rows = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [part.strip() for part in line.split(",")]
        if len(parts) != 3:
            raise ValueError(f"{path}:{line_number}: expected alias,start_ms,end_ms")
        alias, start_ms, end_ms = parts
        rows.append((alias, int(round(float(start_ms) * sample_rate / 1000.0)), int(round(float(end_ms) * sample_rate / 1000.0))))
    aliases = [row[0] for row in rows]
    if aliases != phonemes:
        raise ValueError("Marker aliases must match phoneme order exactly")
    return [(start, end) for _, start, end in rows]


def estimate_pitch_midi(samples: list[float], sample_rate: int) -> int:
    max_lag = max(1, int(sample_rate / 70.0))
    min_lag = max(1, int(sample_rate / 420.0))
    if len(samples) < max_lag * 2:
        return 60
    window = samples[: min(len(samples), sample_rate)]
    best_lag = min_lag
    best_score = 0.0
    for lag in range(min_lag, max_lag):
        score = 0.0
        for index in range(0, len(window) - lag, 4):
            score += window[index] * window[index + lag]
        if score > best_score:
            best_score = score
            best_lag = lag
    if best_score <= 0.0:
        return 60
    hz = sample_rate / best_lag
    return int(round(69.0 + 12.0 * math.log2(hz / 440.0)))


def make_segment(alias: str, start: int, end: int, sample_rate: int, samples: list[float], base_midi: int | None) -> Segment:
    length = max(1, end - start)
    offset_ms = 0.0
    consonant_ms = min(120.0, length * 1000.0 / sample_rate * 0.28)
    loop_start_ms = min(length * 1000.0 / sample_rate * 0.45, consonant_ms + 35.0)
    loop_end_ms = max(loop_start_ms + 40.0, length * 1000.0 / sample_rate * 0.78)
    cutoff_ms = length * 1000.0 / sample_rate
    pitch = base_midi if base_midi is not None else estimate_pitch_midi(samples[start:end], sample_rate)
    return Segment(alias, start, end, offset_ms, consonant_ms, loop_start_ms, loop_end_ms, cutoff_ms, pitch)


def build_voicebank(args: argparse.Namespace) -> None:
    source = Path(args.input)
    output = Path(args.output)
    phonemes = read_phonemes(Path(args.phonemes) if args.phonemes else None, args.phoneme_text)
    sample_rate, samples = read_wav(source)
    if args.markers:
        boundaries = read_markers(Path(args.markers), phonemes, sample_rate)
    else:
        boundaries = detect_boundaries(samples, sample_rate, len(phonemes), args.threshold_db)

    output.mkdir(parents=True, exist_ok=True)
    segments = []
    for alias, (start, end) in zip(phonemes, boundaries):
        start = max(0, min(len(samples) - 1, start))
        end = max(start + 1, min(len(samples), end))
        segment = make_segment(alias, start, end, sample_rate, samples, args.base_midi)
        segments.append(segment)
        write_wav(output / f"{alias}.wav", sample_rate, samples[start:end])

    metadata = {
        "name": args.name,
        "source": str(source),
        "sampleRate": sample_rate,
        "format": "s3g-voicebank-v1",
        "entries": [
            {
                "alias": segment.alias,
                "file": f"{segment.alias}.wav",
                "baseMidi": segment.base_midi,
                "offsetMs": round(segment.offset_ms, 3),
                "consonantMs": round(segment.consonant_ms, 3),
                "loopStartMs": round(segment.loop_start_ms, 3),
                "loopEndMs": round(segment.loop_end_ms, 3),
                "cutoffMs": round(segment.cutoff_ms, 3),
            }
            for segment in segments
        ],
    }
    (output / "voicebank.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    oto_lines = []
    for segment in segments:
        oto_lines.append(
            f"{segment.alias}.wav={segment.alias},"
            f"{segment.offset_ms:.3f},{segment.consonant_ms:.3f},"
            f"{segment.cutoff_ms:.3f},{segment.loop_start_ms:.3f},"
            f"{segment.loop_end_ms:.3f}"
        )
    (output / "oto.ini").write_text("\n".join(oto_lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="Source WAV containing phonemes in order")
    parser.add_argument("-o", "--output", default="voicebank-out", help="Output voicebank directory")
    parser.add_argument("--name", default="s3g_voicebank", help="Voicebank name")
    parser.add_argument("--phonemes", help="Text file with phoneme tokens in source order")
    parser.add_argument("--phoneme-text", help="Inline phoneme token list")
    parser.add_argument("--markers", help="CSV with alias,start_ms,end_ms rows")
    parser.add_argument("--threshold-db", type=float, default=-36.0, help="Active-region threshold below peak for auto slicing")
    parser.add_argument("--base-midi", type=int, help="Override detected base MIDI note")
    return parser.parse_args()


def main() -> None:
    build_voicebank(parse_args())


if __name__ == "__main__":
    main()
