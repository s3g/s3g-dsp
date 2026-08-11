#!/usr/bin/env python3
"""Run clap-validator once per manifest bundle with isolation and timeouts."""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import time
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "scripts" / "clap-bundles.tsv"


@dataclass(frozen=True)
class Bundle:
    build_path: str
    installed_name: str
    plugin_id: str
    host_name: str


def finite_positive(value: str) -> float:
    try:
        result = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if not math.isfinite(result) or result <= 0.0:
        raise argparse.ArgumentTypeError("must be a finite number greater than zero")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate every selected CLAP bundle in a separate subprocess, "
            "retain its log, and report all failures after the sweep."
        )
    )
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--log-dir", type=Path)
    parser.add_argument("--timeout-seconds", type=finite_positive, default=180.0)
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    parser.add_argument(
        "--filter",
        help="Only validate plugin IDs matching this regular expression.",
    )
    parser.add_argument(
        "--exclude-filter",
        help="Skip plugin IDs matching this regular expression.",
    )
    parser.add_argument(
        "--test-filter",
        help="Pass this regular expression to clap-validator --test-filter.",
    )
    return parser.parse_args()


def validator_version(validator: Path) -> str | None:
    try:
        completed = subprocess.run(
            (str(validator), "--version"),
            check=False,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=10.0,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if completed.returncode != 0:
        return None
    match = re.search(r"\b(\d+\.\d+\.\d+)\b", completed.stdout)
    return match.group(1) if match else None


def read_manifest(path: Path) -> list[Bundle]:
    bundles: list[Bundle] = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream, delimiter="\t", strict=True)
        try:
            for row in reader:
                if not row or row[0].lstrip().startswith("#"):
                    continue
                if len(row) != 4 or any(not value or value != value.strip() for value in row):
                    raise ValueError(
                        f"{path}:{reader.line_num}: expected four non-empty trimmed columns"
                    )
                relative = PurePosixPath(row[0])
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError(
                        f"{path}:{reader.line_num}: build path must stay under --build-root"
                    )
                bundles.append(Bundle(*row))
        except csv.Error as error:
            raise ValueError(
                f"{path}:{reader.line_num}: invalid TSV syntax: {error}"
            ) from error
    if not bundles:
        raise ValueError(f"manifest is empty: {path}")
    return bundles


def write_json_atomic(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary_name = stream.name
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary_name, path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            try:
                Path(temporary_name).unlink()
            except FileNotFoundError:
                pass


def log_name(bundle: Bundle) -> str:
    return re.sub(r"[^a-zA-Z0-9_.-]+", "_", bundle.plugin_id) + ".log"


def validate_one(
    validator: Path,
    build_root: Path,
    bundle: Bundle,
    log_dir: Path,
    timeout_seconds: float,
    test_filter: str | None,
    work_around_validator_032_tracker_bug: bool,
) -> dict[str, Any]:
    bundle_path = (
        build_root / Path(*PurePosixPath(bundle.build_path).parts)
    ).resolve()
    result: dict[str, Any] = {
        "plugin_id": bundle.plugin_id,
        "host_name": bundle.host_name,
        "bundle": str(bundle_path),
    }
    log_path = log_dir / log_name(bundle)
    result["log"] = str(log_path)
    if not bundle_path.exists():
        result.update(status="missing", returncode=None, elapsed_seconds=0.0)
        log_path.write_text(f"Missing bundle: {bundle_path}\n", encoding="utf-8")
        return result

    command = [
        str(validator),
        "-v",
        "warn",
        "validate",
        "--no-parallel",
        "--only-failed",
        "--plugin-id",
        bundle.plugin_id,
    ]
    if test_filter:
        command.extend(("--test-filter", test_filter))
    elif work_around_validator_032_tracker_bug:
        # clap-validator 0.3.2 calls note_ports.get(..., true, ...) while
        # enumerating output ports. Tracker correctly exposes output-only MIDI
        # ports, so that validator version cannot run its two process-note
        # cases for this plug-in. All other 0.3.2 cases still run; newer
        # validators query the output direction correctly.
        command.extend(("--test-filter", "^process-note-", "--invert-filter"))
        result["compatibility_workaround"] = (
            "clap-validator-0.3.2-output-note-port-direction"
        )
    command.append(str(bundle_path))

    started = time.monotonic()
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=timeout_seconds,
        )
        elapsed = time.monotonic() - started
        combined = completed.stdout
        if completed.stderr:
            combined += ("\n" if combined else "") + completed.stderr
        log_path.write_text(combined, encoding="utf-8", errors="replace")
        result.update(
            status="passed" if completed.returncode == 0 else "failed",
            returncode=completed.returncode,
            elapsed_seconds=round(elapsed, 6),
        )
    except subprocess.TimeoutExpired as error:
        elapsed = time.monotonic() - started
        output = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        detail = output + (("\n" if output else "") + stderr if stderr else "")
        detail += f"\nTimed out after {timeout_seconds:g} seconds.\n"
        log_path.write_text(detail, encoding="utf-8", errors="replace")
        result.update(status="timeout", returncode=None, elapsed_seconds=round(elapsed, 6))
    except OSError as error:
        elapsed = time.monotonic() - started
        log_path.write_text(f"Could not execute validator: {error}\n", encoding="utf-8")
        result.update(
            status="error",
            returncode=None,
            elapsed_seconds=round(elapsed, 6),
            error=str(error),
        )
    return result


def main() -> int:
    args = parse_args()
    validator = args.validator.expanduser().resolve()
    manifest = args.manifest.expanduser().resolve()
    build_root = args.build_root.expanduser().resolve()
    output = args.output.expanduser().resolve()
    log_dir = (
        args.log_dir.expanduser().resolve()
        if args.log_dir
        else output.parent / f"{output.stem}-logs"
    )
    try:
        output.unlink()
    except FileNotFoundError:
        pass
    except OSError as error:
        print(f"Could not remove stale output {output}: {error}", file=sys.stderr)
        return 2
    if not validator.is_file() or not os.access(validator, os.X_OK):
        print(f"Missing executable clap-validator: {validator}", file=sys.stderr)
        return 2
    detected_validator_version = validator_version(validator)
    if not manifest.is_file():
        print(f"Missing manifest: {manifest}", file=sys.stderr)
        return 2
    if not build_root.is_dir():
        print(f"Missing build root: {build_root}", file=sys.stderr)
        return 2
    if args.jobs <= 0:
        print("--timeout-seconds and --jobs must be positive", file=sys.stderr)
        return 2
    try:
        include = re.compile(args.filter) if args.filter else None
        exclude = re.compile(args.exclude_filter) if args.exclude_filter else None
        bundles = read_manifest(manifest)
    except (OSError, ValueError, re.error) as error:
        print(error, file=sys.stderr)
        return 2
    selected = [
        bundle
        for bundle in bundles
        if (include is None or include.search(bundle.plugin_id))
        and (exclude is None or not exclude.search(bundle.plugin_id))
    ]
    if not selected:
        print("No manifest bundles matched the requested filters", file=sys.stderr)
        return 2

    try:
        log_dir.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        print(f"Could not create validator log directory: {error}", file=sys.stderr)
        return 2
    results: list[dict[str, Any]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(
                validate_one,
                validator,
                build_root,
                bundle,
                log_dir,
                args.timeout_seconds,
                args.test_filter,
                detected_validator_version == "0.3.2"
                and bundle.plugin_id == "org.s3g.s3g-dsp.tracker",
            ): bundle
            for bundle in selected
        }
        for index, future in enumerate(concurrent.futures.as_completed(futures), start=1):
            bundle = futures[future]
            try:
                result = future.result()
            except Exception as error:  # keep the remaining isolated jobs visible
                result = {
                    "plugin_id": bundle.plugin_id,
                    "host_name": bundle.host_name,
                    "bundle": str(build_root / bundle.build_path),
                    "log": str(log_dir / log_name(bundle)),
                    "status": "error",
                    "returncode": None,
                    "elapsed_seconds": 0.0,
                    "error": f"unexpected validator worker error: {error}",
                }
            results.append(result)
            print(
                f"[{index}/{len(selected)}] {result['plugin_id']}: {result['status']}",
                flush=True,
            )

    results.sort(key=lambda item: item["plugin_id"])
    counts = {
        status: sum(result["status"] == status for result in results)
        for status in ("passed", "failed", "timeout", "missing", "error")
    }
    passed = counts["passed"] == len(results)
    report = {
        "schema": "org.s3g.s3g-dsp.clap-validator-manifest/v1",
        "validator": str(validator),
        "validator_version": detected_validator_version,
        "manifest": str(manifest),
        "build_root": str(build_root),
        "filter": args.filter,
        "exclude_filter": args.exclude_filter,
        "test_filter": args.test_filter,
        "timeout_seconds": args.timeout_seconds,
        "jobs": args.jobs,
        "selected": len(results),
        "counts": counts,
        "passed": passed,
        "results": results,
    }
    try:
        write_json_atomic(output, report)
    except OSError as error:
        print(f"Could not write validator report: {error}", file=sys.stderr)
        return 1
    print(f"CLAP validation: {counts}; overall {'passed' if passed else 'failed'}.")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
