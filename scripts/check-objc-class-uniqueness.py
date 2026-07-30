#!/usr/bin/env python3
"""Reject process-global Objective-C class names shared by CLAP bundles."""

from __future__ import annotations

import argparse
import re
from collections import defaultdict
from pathlib import Path


INTERFACE_RE = re.compile(r"^\s*@interface\s+([A-Za-z_][A-Za-z0-9_]*)\b")
SOURCE_SUFFIXES = {".cpp", ".inc", ".m", ".mm"}


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(
        description="Check that every CLAP bundle uses unique Objective-C class names."
    )
    parser.add_argument("root", nargs="?", type=Path, default=root / "plugins")
    return parser.parse_args()


def main() -> int:
    source_root = parse_args().root.resolve()
    declarations: dict[str, list[tuple[Path, int]]] = defaultdict(list)
    for path in sorted(source_root.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
        ):
            match = INTERFACE_RE.match(line)
            if match:
                declarations[match.group(1)].append((path, line_number))

    collisions = {
        name: entries
        for name, entries in declarations.items()
        if len({path for path, _ in entries}) > 1
    }
    if collisions:
        print("Objective-C class names shared by multiple CLAP source files:")
        for name in sorted(collisions):
            print(f"  {name}")
            for path, line_number in collisions[name]:
                print(f"    {path}:{line_number}")
        return 1

    print(
        f"Objective-C class audit passed: {len(declarations)} unique class names "
        f"under {source_root}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
