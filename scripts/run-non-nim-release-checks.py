#!/usr/bin/env python3
"""Run the non-NIM release checks sequentially and retain every result."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from typing import Any


@dataclass(frozen=True)
class Check:
    name: str
    command: tuple[str, ...]
    timeout_seconds: float
    evidence: Path | None = None
    evidence_kind: str | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--ctest", type=Path, required=True)
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--realtime-audit", type=Path, required=True)
    parser.add_argument("--allocation-probe-library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


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


NIM_PLUGIN_IDS = {
    "org.s3g.s3g-dsp.no-input-mixer-8ch",
    "org.s3g.s3g-dsp.nim-gesture",
}
REALTIME_PROFILE_KEYS = {
    "core",
    "buffer-floor-48khz",
    "buffer-floor-96khz",
    "spectral-8ch-48khz",
    "spectral-8ch-96khz",
    "spectral-24ch-48khz",
    "spectral-24ch-96khz",
    "spectral-spray",
    "environmental-48khz",
    "environmental-96khz-advisory",
}


def read_non_nim_ids(manifest: Path) -> set[str]:
    all_ids: set[str] = set()
    with manifest.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream, delimiter="\t", strict=True)
        try:
            for row in reader:
                if not row or row[0].lstrip().startswith("#"):
                    continue
                if len(row) != 4 or any(not field.strip() for field in row):
                    raise ValueError(
                        f"{manifest}:{reader.line_num}: malformed manifest row"
                    )
                plugin_id = row[2]
                if plugin_id in all_ids:
                    raise ValueError(f"duplicate CLAP ID in manifest: {plugin_id}")
                all_ids.add(plugin_id)
        except csv.Error as error:
            raise ValueError(f"invalid manifest TSV: {error}") from error
    if not NIM_PLUGIN_IDS.issubset(all_ids):
        raise ValueError("canonical manifest is missing one or more NIM IDs")
    ids = all_ids - NIM_PLUGIN_IDS
    if not ids:
        raise ValueError("non-NIM manifest inventory is empty")
    return ids


def validate_evidence(
    path: Path, kind: str, expected_ids: set[str]
) -> str | None:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return f"could not read fresh evidence: {error}"
    if not isinstance(report, dict):
        return "evidence top level must be an object"

    expected_schema = {
        "validator": "org.s3g.s3g-dsp.clap-validator-manifest/v1",
        "allocation": "org.s3g.s3g-dsp.clap-realtime-audit.aggregate/v1",
        "realtime": "org.s3g.s3g-dsp.clap-realtime-release-gates/v1",
    }[kind]
    if report.get("schema") != expected_schema:
        return f"evidence schema must be {expected_schema!r}"
    if report.get("passed") is not True and kind != "allocation":
        return "evidence does not report passed=true"

    if kind == "validator":
        results = report.get("results")
        if not isinstance(results, list):
            return "validator evidence results must be an array"
        actual_ids = [item.get("plugin_id") for item in results if isinstance(item, dict)]
        if len(actual_ids) != len(expected_ids) or set(actual_ids) != expected_ids:
            return "validator evidence does not cover the exact non-NIM inventory"
        if any(item.get("status") != "passed" for item in results):
            return "validator evidence contains a non-passing bundle"
        if report.get("selected") != len(expected_ids):
            return "validator selected count does not match non-NIM inventory"
        return None

    if kind == "allocation":
        configuration = report.get("configuration")
        if (
            not isinstance(configuration, dict)
            or configuration.get("allocation_probe") is not True
            or configuration.get("allocation_gate") is not True
        ):
            return "allocation evidence was not produced with both probe and gate enabled"
        gate = report.get("allocation_gate")
        if (
            not isinstance(gate, dict)
            or gate.get("enabled") is not True
            or gate.get("passed") is not True
        ):
            return "allocation evidence does not contain a passing allocation gate"
        if report.get("selected") != len(expected_ids) or report.get("completed") != len(expected_ids):
            return "allocation evidence count does not match non-NIM inventory"
        if report.get("failures") != [] or report.get("skipped") != []:
            return "allocation evidence contains failed or skipped bundles"
        audits = report.get("audits")
        if not isinstance(audits, list):
            return "allocation evidence audits must be an array"
        actual_ids = [item.get("plugin_id") for item in audits if isinstance(item, dict)]
        if len(actual_ids) != len(expected_ids) or set(actual_ids) != expected_ids:
            return "allocation evidence does not cover the exact non-NIM inventory"
        return None

    profiles = report.get("profiles")
    if not isinstance(profiles, list):
        return "realtime evidence profiles must be an array"
    actual_profiles = [item.get("profile") for item in profiles if isinstance(item, dict)]
    if len(actual_profiles) != len(REALTIME_PROFILE_KEYS) or set(actual_profiles) != REALTIME_PROFILE_KEYS:
        return "realtime evidence does not cover the exact release profile inventory"
    if any(
        item.get("returncode") != 0 or item.get("report_valid") is not True
        for item in profiles
    ):
        return "realtime evidence contains a failed or invalid profile"
    return None


def run_check(
    check: Check, log: Path, cwd: Path, expected_ids: set[str]
) -> dict[str, Any]:
    started = time.monotonic()
    status = "error"
    returncode: int | None = None
    error: str | None = None
    try:
        with log.open("w", encoding="utf-8") as stream:
            stream.write("command: " + " ".join(check.command) + "\n\n")
            stream.flush()
            try:
                if check.evidence is not None:
                    try:
                        check.evidence.unlink()
                    except FileNotFoundError:
                        pass
                completed = subprocess.run(
                    check.command,
                    cwd=cwd,
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                    check=False,
                    timeout=check.timeout_seconds,
                )
                returncode = completed.returncode
                status = "passed" if returncode == 0 else "failed"
                if check.evidence is not None and check.evidence_kind is not None:
                    evidence_error = validate_evidence(
                        check.evidence, check.evidence_kind, expected_ids
                    )
                    if evidence_error is not None:
                        status = "failed"
                        error = evidence_error
                        stream.write(f"\nInvalid release evidence: {evidence_error}\n")
            except subprocess.TimeoutExpired:
                status = "timeout"
                stream.write(f"\nTimed out after {check.timeout_seconds:g} seconds.\n")
            except OSError as exception:
                error = str(exception)
                stream.write(f"\nCould not execute check: {exception}\n")
    except OSError as exception:
        error = str(exception)
    result: dict[str, Any] = {
        "name": check.name,
        "status": status,
        "returncode": returncode,
        "elapsed_seconds": round(time.monotonic() - started, 6),
        "log": str(log),
    }
    if error is not None:
        result["error"] = error
    if check.evidence is not None:
        result["evidence"] = str(check.evidence)
        result["evidence_kind"] = check.evidence_kind
    return result


def main() -> int:
    args = parse_args()
    source_root = args.source_root.expanduser().resolve()
    build_root = args.build_root.expanduser().resolve()
    ctest = args.ctest.expanduser().resolve()
    validator = args.validator.expanduser().resolve()
    realtime_audit = args.realtime_audit.expanduser().resolve()
    allocation_probe = args.allocation_probe_library.expanduser().resolve()
    output = args.output.expanduser().resolve()
    for label, path, directory in (
        ("source root", source_root, True),
        ("build root", build_root, True),
        ("ctest", ctest, False),
        ("clap-validator", validator, False),
        ("realtime audit", realtime_audit, False),
        ("allocation probe library", allocation_probe, False),
    ):
        exists = path.is_dir() if directory else path.is_file()
        if not exists:
            print(f"Missing {label}: {path}", file=sys.stderr)
            return 2
    manifest = source_root / "scripts" / "clap-bundles.tsv"
    try:
        expected_ids = read_non_nim_ids(manifest)
    except (OSError, ValueError) as error:
        print(f"Could not read release inventory: {error}", file=sys.stderr)
        return 2
    try:
        output.unlink()
    except FileNotFoundError:
        pass
    except OSError as error:
        print(f"Could not prepare release checks: {error}", file=sys.stderr)
        return 2
    logs = output.parent / f"{output.stem}-logs"
    try:
        logs.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        print(f"Could not create release-check log directory: {error}", file=sys.stderr)
        return 2
    python = sys.executable
    checks = [
        Check(
            "ctest-non-nim",
            (
                str(ctest), "--test-dir", str(build_root),
                "--output-on-failure", "-L", "non_nim", "--timeout", "300",
                "--no-tests=error",
            ),
            7200.0,
        ),
        Check(
            "bundle-manifest",
            (
                python, str(source_root / "scripts" / "check-clap-bundle-manifest.py"),
                "--build-root", str(build_root / "plugins"),
                "--defer-descriptor-version",
            ),
            600.0,
        ),
        Check(
            "objc-symbols",
            (
                python, str(source_root / "scripts" / "check-clap-objc-symbols.py"),
                "--manifest", str(manifest),
                "--build-root", str(build_root / "plugins"),
            ),
            600.0,
        ),
        Check(
            "clap-validator",
            (
                python, str(source_root / "scripts" / "run-clap-validator-manifest.py"),
                "--validator", str(validator),
                "--manifest", str(manifest),
                "--build-root", str(build_root / "plugins"),
                "--exclude-filter", "(no-input-mixer-8ch|nim-gesture)$",
                "--output", str(build_root / "clap-validator-non-nim.json"),
                "--timeout-seconds", "180", "--jobs", "4",
            ),
            14400.0,
            build_root / "clap-validator-non-nim.json",
            "validator",
        ),
        Check(
            "allocation-sweep-non-nim",
            (
                python, str(source_root / "scripts" / "run-clap-realtime-audit.py"),
                "--audit-executable", str(realtime_audit),
                "--manifest", str(manifest),
                "--build-root", str(build_root / "plugins"),
                "--exclude-filter", "(no-input-mixer-8ch|nim-gesture)$",
                "--allocation-probe-library", str(allocation_probe),
                "--allocation-gate",
                "--output", str(build_root / "clap-allocation-non-nim.json"),
                "--timeout-seconds", "600",
            ),
            14400.0,
            build_root / "clap-allocation-non-nim.json",
            "allocation",
        ),
        Check(
            "realtime-profiles",
            (
                python, str(source_root / "scripts" / "run-clap-realtime-release-gates.py"),
                "--audit-executable", str(realtime_audit),
                "--manifest", str(manifest),
                "--build-root", str(build_root / "plugins"),
                "--output-dir", str(build_root),
                "--allocation-probe-library", str(allocation_probe),
                "--include-advisory",
            ),
            14400.0,
            build_root / "clap-realtime-release-gates-summary.json",
            "realtime",
        ),
    ]

    results: list[dict[str, Any]] = []
    for index, check in enumerate(checks, start=1):
        print(f"[{index}/{len(checks)}] release check {check.name}", flush=True)
        result = run_check(
            check, logs / f"{check.name}.log", source_root, expected_ids
        )
        results.append(result)
        print(f"  {result['status']} in {result['elapsed_seconds']:.3f}s", flush=True)
    passed = all(result["status"] == "passed" for result in results)
    try:
        write_json_atomic(
            output,
            {
                "schema": "org.s3g.s3g-dsp.non-nim-release-checks/v1",
                "passed": passed,
                "expected_non_nim_plugin_ids": sorted(expected_ids),
                "allocation_probe_library": str(allocation_probe),
                "checks": results,
            },
        )
    except OSError as error:
        print(f"Could not write release-check summary: {error}", file=sys.stderr)
        return 1
    print(f"Non-NIM release checks {'passed' if passed else 'failed'}.")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
