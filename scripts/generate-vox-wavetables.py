#!/usr/bin/env python3
import argparse
import json
import math
import wave
from pathlib import Path

SAMPLE_RATE = 48000
GRID_SIZE = 4
TABLE_SIZE = 256
TABLE_COUNT = GRID_SIZE * GRID_SIZE
TAU = 2.0 * math.pi


def clamp(value, lo, hi):
    return max(lo, min(hi, value))


def fract(value):
    return value - math.floor(value)


def softsat(value):
    return math.tanh(value)


def pulse(phase, width):
    return 1.0 if fract(phase) < clamp(width, 0.04, 0.96) else -1.0


def triangle(phase):
    phase = fract(phase)
    return 4.0 * abs(phase - 0.5) - 1.0


def hash_noise(seed):
    seed &= 0xFFFFFFFF
    seed ^= seed >> 16
    seed = (seed * 0x7FEB352D) & 0xFFFFFFFF
    seed ^= seed >> 15
    seed = (seed * 0x846CA68B) & 0xFFFFFFFF
    seed ^= seed >> 16
    return (seed & 0x00FFFFFF) / 0x00800000 - 1.0


def blackmetal(u, v, phase):
    closure = clamp(u, 0.0, 1.0)
    darkness = clamp(v, 0.0, 1.0)
    sine = math.sin(TAU * phase)
    throat = math.sin(TAU * phase * (2.0 + 5.0 * darkness)
                      + 0.55 * math.sin(TAU * phase * (3.0 + 9.0 * closure)))
    shriek = math.sin(TAU * phase * (9.0 + 22.0 * closure) + darkness * 2.1)
    scrape = hash_noise(int(phase * 8192.0) + int(closure * 101.0) + int(darkness * 509.0))
    body = 0.36 * sine + 0.28 * pulse(phase, 0.18 + 0.34 * closure) + 0.22 * throat
    return softsat((body + (0.10 + 0.22 * closure) * shriek + (0.06 + 0.18 * darkness) * scrape)
                   * (1.35 + 3.25 * closure))


def throat(u, v, phase):
    closure = clamp(u, 0.0, 1.0)
    darkness = clamp(v, 0.0, 1.0)
    sub = math.sin(TAU * phase * 0.5 + darkness * 1.7)
    tone = 0.42 * math.sin(TAU * phase) + 0.28 * pulse(phase, 0.28 + 0.32 * closure)
    form = 0.26 * sub + 0.18 * math.sin(TAU * phase * (3.0 + 5.0 * darkness))
    return softsat((tone + form) * (1.0 + 2.2 * closure))


def choir(u, v, phase):
    closure = clamp(u, 0.0, 1.0)
    darkness = clamp(v, 0.0, 1.0)
    f1 = 2.0 + 5.0 * darkness
    f2 = 5.0 + 10.0 * (1.0 - darkness)
    sample = (0.62 * math.sin(TAU * phase)
              + 0.22 * math.sin(TAU * phase * f1 + closure * 0.4)
              + 0.14 * math.sin(TAU * phase * f2 + darkness * 1.3))
    return sample * (1.0 - 0.20 * closure) + triangle(phase) * 0.20 * closure


def animal(u, v, phase):
    closure = clamp(u, 0.0, 1.0)
    darkness = clamp(v, 0.0, 1.0)
    fold = math.sin(TAU * (phase + 0.13 * math.sin(TAU * phase * (2.0 + 6.0 * closure))))
    growl = math.sin(TAU * phase * (1.0 + 0.5 * darkness)) * pulse(phase * (2.0 + 3.0 * closure), 0.34)
    return softsat((0.48 * fold + 0.44 * growl + 0.16 * math.sin(TAU * phase)) * (1.2 + 1.8 * closure))


ATLASES = (
    ("blackmetal", "Black Metal", "throat closure and distortion edge", "vowel darkness and rasp depth", blackmetal),
    ("throat", "Throat", "closure", "subharmonic body", throat),
    ("choir", "Choir", "strain", "vowel family", choir),
    ("animal", "Animal", "snarl", "body size", animal),
)


def level_match(tables, target_rms=0.30, peak_limit=0.90):
    out = []
    for table in tables:
        mean = sum(table) / len(table)
        centered = [x - mean for x in table]
        rms = math.sqrt(sum(x * x for x in centered) / len(centered))
        gain = target_rms / max(1.0e-9, rms)
        peak = max(abs(x * gain) for x in centered)
        if peak > peak_limit:
            gain *= peak_limit / peak
        out.append([x * gain for x in centered])
    return out


def make_atlas(generator):
    tables = []
    for row in range(GRID_SIZE):
        v = row / (GRID_SIZE - 1)
        for column in range(GRID_SIZE):
            u = column / (GRID_SIZE - 1)
            tables.append([generator(u, v, i / TABLE_SIZE) for i in range(TABLE_SIZE)])
    return level_match(tables)


def pcm24_bytes(samples):
    out = bytearray()
    for sample in samples:
        value = int(round(clamp(sample, -1.0, 1.0) * 8388607.0))
        if value < 0:
            value += 1 << 24
        out.extend((value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF))
    return bytes(out)


def write_wav(path, tables):
    samples = [sample for table in tables for sample in table]
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(3)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm24_bytes(samples))


def svg_path(table, x, y, width, height):
    points = []
    for sample in range(0, TABLE_SIZE, 2):
        px = x + sample / (TABLE_SIZE - 2) * width
        py = y + height * 0.5 - table[sample] * height * 0.42
        points.append(("M" if not points else "L") + f"{px:.2f},{py:.2f}")
    return " ".join(points)


def write_preview(path, title, u_label, v_label, tables):
    width = 760
    height = 760
    left = 76
    top = 72
    cell = 152
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="760" height="760" fill="#161616"/>',
        f'<text x="24" y="32" fill="#dedede" font-family="monospace" font-size="15">S3G VOX / {title.upper()}</text>',
        f'<text x="684" y="724" text-anchor="end" fill="#858585" font-family="monospace" font-size="11">U / {u_label.upper()}</text>',
        f'<text x="24" y="72" fill="#858585" font-family="monospace" font-size="11">V / {v_label.upper()}</text>',
    ]
    for row in range(GRID_SIZE):
        for column in range(GRID_SIZE):
            table_index = row * GRID_SIZE + column
            x = left + column * cell
            y = top + row * cell
            lines.append(f'<rect x="{x}" y="{y}" width="{cell}" height="{cell}" fill="#0b0b0b" stroke="#444"/>')
            lines.append(f'<line x1="{x + 8}" y1="{y + cell / 2:.2f}" x2="{x + cell - 8}" y2="{y + cell / 2:.2f}" stroke="#292929"/>')
            lines.append(f'<path d="{svg_path(tables[table_index], x + 8, y + 18, cell - 16, cell - 36)}" fill="none" stroke="#e0e0e0" stroke-width="1.4"/>')
            lines.append(f'<text x="{x + 7}" y="{y + 14}" fill="#666" font-family="monospace" font-size="9">{table_index + 1:02d}</text>')
    lines.append("</svg>")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def generate(output_directory):
    output_directory.mkdir(parents=True, exist_ok=True)
    preview_directory = output_directory / "previews"
    preview_directory.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": "s3g-vox-atlas-v1",
        "sample_rate": SAMPLE_RATE,
        "channels": 1,
        "sample_format": "PCM 24-bit",
        "grid": [GRID_SIZE, GRID_SIZE],
        "table_size": TABLE_SIZE,
        "table_order": "row-major, top-left to bottom-right",
        "atlases": [],
    }
    for slug, title, u_label, v_label, generator in ATLASES:
        tables = make_atlas(generator)
        wav_name = f"s3g-vox-{slug}.wav"
        preview_name = f"s3g-vox-{slug}.svg"
        write_wav(output_directory / wav_name, tables)
        write_preview(preview_directory / preview_name, title, u_label, v_label, tables)
        manifest["atlases"].append({
            "id": slug,
            "name": title,
            "u_axis": u_label,
            "v_axis": v_label,
            "energy_surface": "level matched",
            "wav": wav_name,
            "preview": f"previews/{preview_name}",
        })
        print(output_directory / wav_name)
    (output_directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description="Generate the s3g VOX 4 x 4 wavetable atlas library")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "wavetables" / "vox",
        help="output directory (default: repo wavetables/vox)",
    )
    args = parser.parse_args()
    generate(args.output)


if __name__ == "__main__":
    main()
