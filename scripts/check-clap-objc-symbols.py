#!/usr/bin/env python3
"""Reject Objective-C class symbols exported by more than one active CLAP bundle."""

from __future__ import annotations

import argparse
import re
import subprocess
from collections import defaultdict
from pathlib import Path


CLASS_RE = re.compile(r"_OBJC_CLASS_\$_([A-Za-z_][A-Za-z0-9_]*)$")


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(
        description="Check compiled active CLAP bundles for Objective-C class collisions."
    )
    parser.add_argument(
        "--manifest", type=Path, default=repo_root / "scripts/clap-bundles.tsv"
    )
    parser.add_argument(
        "--build-root", type=Path, default=repo_root / "build-clap/plugins"
    )
    return parser.parse_args()


def manifest_bundle_paths(manifest: Path, build_root: Path) -> list[Path]:
    paths: list[Path] = []
    for line_number, line in enumerate(
        manifest.read_text(encoding="utf-8").splitlines(), 1
    ):
        if not line or line.startswith("#"):
            continue
        columns = line.split("\t")
        if len(columns) != 4:
            raise ValueError(f"{manifest}:{line_number}: expected four tab-separated fields")
        paths.append(build_root / columns[0])
    return paths


def bundle_executable(bundle: Path) -> Path:
    executable_dir = bundle / "Contents/MacOS"
    if not executable_dir.is_dir():
        raise FileNotFoundError(f"missing CLAP executable directory: {executable_dir}")
    executables = sorted(path for path in executable_dir.iterdir() if path.is_file())
    if len(executables) != 1:
        raise ValueError(
            f"expected one executable under {executable_dir}, found {len(executables)}"
        )
    return executables[0]


def objc_classes(executable: Path) -> set[str]:
    result = subprocess.run(
        ["nm", "-gU", str(executable)],
        check=True,
        capture_output=True,
        text=True,
    )
    classes: set[str] = set()
    for line in result.stdout.splitlines():
        match = CLASS_RE.search(line)
        if match:
            classes.add(match.group(1))
    return classes


def main() -> int:
    args = parse_args()
    manifest = args.manifest.resolve()
    build_root = args.build_root.resolve()
    try:
        bundles = manifest_bundle_paths(manifest, build_root)
        owners: dict[str, list[Path]] = defaultdict(list)
        for bundle in bundles:
            executable = bundle_executable(bundle)
            for class_name in objc_classes(executable):
                owners[class_name].append(bundle)
    except (FileNotFoundError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"Objective-C symbol audit failed: {error}")
        return 1

    collisions = {
        class_name: class_bundles
        for class_name, class_bundles in owners.items()
        if len(class_bundles) > 1
    }
    if collisions:
        print("Objective-C class symbols shared by active CLAP bundles:")
        for class_name in sorted(collisions):
            print(f"  {class_name}")
            for bundle in collisions[class_name]:
                print(f"    {bundle}")
        return 1

    print(
        f"Objective-C symbol audit passed: {len(owners)} unique classes across "
        f"{len(bundles)} active CLAP bundles"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
