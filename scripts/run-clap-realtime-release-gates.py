#!/usr/bin/env python3
"""Run every supported CLAP realtime profile before returning gate status."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RUNNER = ROOT / "scripts" / "run-clap-realtime-audit.py"
DEFAULT_MANIFEST = ROOT / "scripts" / "clap-bundles.tsv"
AGGREGATE_SCHEMA = "org.s3g.s3g-dsp.clap-realtime-audit.aggregate/v1"
STRICT_MINIMUM_ITERATIONS = 10_000
STRICT_MAXIMUM_P99_DEADLINE_LOAD = 0.75
STRICT_MAXIMUM_DEADLINE_MISS_RATE = 0.01


@dataclass(frozen=True)
class Profile:
    key: str
    plugin_filter: str
    sample_rates: str
    blocks: str
    plugin_ids: tuple[str, ...]
    release_gate: bool = True
    iterations: int | None = None


def plugin_ids(*suffixes: str) -> tuple[str, ...]:
    return tuple(f"org.s3g.s3g-dsp.{suffix}" for suffix in suffixes)


PROFILES = {
    profile.key: profile
    for profile in (
        Profile(
            "core",
            r"^org\.s3g\.s3g-dsp\.(ambi-group-rotate-64|node-bus-mixer)$",
            "48000,96000",
            "32,64,128,256",
            plugin_ids("ambi-group-rotate-64", "node-bus-mixer"),
        ),
        Profile(
            "buffer-floor-48khz",
            r"^org\.s3g\.s3g-dsp\.(delay-processor-24ch|ambisonic-rotate-64|ambi-group-rotate-128)$",
            "48000",
            "32,64,128,256",
            plugin_ids(
                "delay-processor-24ch",
                "ambisonic-rotate-64",
                "ambi-group-rotate-128",
            ),
        ),
        Profile(
            "buffer-floor-96khz",
            r"^org\.s3g\.s3g-dsp\.(delay-processor-24ch|ambisonic-rotate-64|ambi-group-rotate-128)$",
            "96000",
            "64,128,256",
            plugin_ids(
                "delay-processor-24ch",
                "ambisonic-rotate-64",
                "ambi-group-rotate-128",
            ),
        ),
        Profile(
            "spectral-8ch-48khz",
            r"^org\.s3g\.s3g-dsp\.spectral-topology-processor$",
            "48000",
            "64,128,256",
            plugin_ids("spectral-topology-processor"),
        ),
        Profile(
            "spectral-8ch-96khz",
            r"^org\.s3g\.s3g-dsp\.spectral-topology-processor$",
            "96000",
            "128,256,512",
            plugin_ids("spectral-topology-processor"),
        ),
        Profile(
            "spectral-24ch-48khz",
            r"^org\.s3g\.s3g-dsp\.spectral-topology-processor-24ch$",
            "48000",
            "256",
            plugin_ids("spectral-topology-processor-24ch"),
        ),
        Profile(
            "spectral-24ch-96khz",
            r"^org\.s3g\.s3g-dsp\.spectral-topology-processor-24ch$",
            "96000",
            "512",
            plugin_ids("spectral-topology-processor-24ch"),
        ),
        Profile(
            "spectral-spray",
            r"^org\.s3g\.s3g-dsp\.(spectral-spray|8ch-spectral-spray)$",
            "48000,96000",
            "32,64,128,256",
            plugin_ids("spectral-spray", "8ch-spectral-spray"),
        ),
        Profile(
            "environmental-48khz",
            r"^org\.s3g\.s3g-dsp\.ambi-(water|insect)-encoder-64$",
            "48000",
            "32,64,128,256",
            plugin_ids("ambi-water-encoder-64", "ambi-insect-encoder-64"),
        ),
        Profile(
            "environmental-96khz-advisory",
            r"^org\.s3g\.s3g-dsp\.ambi-(water|insect)-encoder-64$",
            "96000",
            "32,64,128,256",
            plugin_ids("ambi-water-encoder-64", "ambi-insect-encoder-64"),
            release_gate=False,
            iterations=1000,
        ),
    )
}

DEFAULT_PROFILE_KEYS = tuple(
    key for key, profile in PROFILES.items() if profile.release_gate
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run the supported non-NIM realtime profiles sequentially, retain "
            "every JSON report, and return failure only after all profiles finish."
        )
    )
    parser.add_argument("--runner", type=Path, default=DEFAULT_RUNNER)
    parser.add_argument("--audit-executable", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--summary", type=Path)
    parser.add_argument(
        "--allocation-probe-library",
        type=Path,
        help=(
            "macOS realtime allocation probe dylib; when supplied every "
            "profile must include valid zero-allocation evidence."
        ),
    )
    parser.add_argument(
        "--profile",
        action="append",
        choices=tuple(PROFILES),
        dest="profiles",
        help="Run only this profile; repeat for more than one.",
    )
    parser.add_argument(
        "--include-advisory",
        action="store_true",
        help="Also measure report-only profiles that are not claimed release floors.",
    )
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


def read_report_summary(
    path: Path, profile: Profile, allocation_probe: bool
) -> dict[str, Any]:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return {"report_valid": False, "report_errors": [str(error)]}
    errors: list[str] = []
    if not isinstance(report, dict):
        return {
            "report_valid": False,
            "report_errors": ["report top level must be an object"],
        }
    if report.get("schema") != AGGREGATE_SCHEMA:
        errors.append(
            f"schema must be {AGGREGATE_SCHEMA!r}, got {report.get('schema')!r}"
        )
    expected_ids = set(profile.plugin_ids)
    expected_count = len(expected_ids)
    selected = report.get("selected")
    completed = report.get("completed")
    if selected != expected_count:
        errors.append(f"selected must be {expected_count}, got {selected!r}")
    if completed != expected_count:
        errors.append(f"completed must be {expected_count}, got {completed!r}")

    failures = report.get("failures")
    skipped = report.get("skipped")
    if failures != []:
        errors.append("failures must be an empty array")
    if skipped != []:
        errors.append("skipped must be an empty array")

    configuration = report.get("configuration")
    if not isinstance(configuration, dict):
        errors.append("configuration must be an object")
        configuration = {}
    if configuration.get("sample_rates") != profile.sample_rates:
        errors.append("configuration sample-rate profile does not match request")
    if configuration.get("blocks") != profile.blocks:
        errors.append("configuration block profile does not match request")
    if configuration.get("automation_ladder") is not True:
        errors.append("configuration must enable the automation ladder")
    if configuration.get("allocation_probe") is not allocation_probe:
        errors.append("configuration allocation-probe status does not match request")
    expected_iterations = profile.iterations or STRICT_MINIMUM_ITERATIONS
    if configuration.get("iterations") != expected_iterations:
        errors.append(
            f"configuration iterations must be {expected_iterations}, "
            f"got {configuration.get('iterations')!r}"
        )

    audits = report.get("audits")
    actual_ids: list[str] = []
    baseline_pairs: list[tuple[str, int, int]] = []
    if not isinstance(audits, list):
        errors.append("audits must be an array")
        audits = []
    for audit in audits:
        if not isinstance(audit, dict):
            errors.append("every audit entry must be an object")
            continue
        plugin_id = audit.get("plugin_id")
        if not isinstance(plugin_id, str):
            errors.append("every audit entry must contain a plugin_id")
            continue
        actual_ids.append(plugin_id)
        child = audit.get("report")
        scenarios = child.get("scenarios") if isinstance(child, dict) else None
        if not isinstance(scenarios, list):
            errors.append(f"{plugin_id}: scenarios must be an array")
            continue
        for scenario in scenarios:
            if not isinstance(scenario, dict) or scenario.get("name") != "baseline":
                continue
            rate = scenario.get("sample_rate")
            block = scenario.get("block_size")
            if (
                isinstance(rate, bool)
                or not isinstance(rate, (int, float))
                or isinstance(block, bool)
                or not isinstance(block, int)
            ):
                errors.append(f"{plugin_id}: malformed baseline rate/block")
                continue
            baseline_pairs.append((plugin_id, int(rate), block))
    if len(actual_ids) != expected_count or set(actual_ids) != expected_ids:
        errors.append(
            "audited plugin IDs must exactly match profile inventory; "
            f"expected {sorted(expected_ids)!r}, got {sorted(actual_ids)!r}"
        )

    requested_rates = [int(value) for value in profile.sample_rates.split(",")]
    requested_blocks = [int(value) for value in profile.blocks.split(",")]
    expected_baselines = {
        (plugin_id, rate, block)
        for plugin_id in expected_ids
        for rate in requested_rates
        for block in requested_blocks
    }
    if set(baseline_pairs) != expected_baselines or len(baseline_pairs) != len(expected_baselines):
        errors.append("baseline coverage does not exactly match requested rate/block grid")

    release_gate = report.get("release_gate")
    if not isinstance(release_gate, dict):
        errors.append("release_gate must be an object")
        release_gate = {}
    if profile.release_gate:
        if (
            release_gate.get("enabled") is not True
            or release_gate.get("passed") is not True
        ):
            errors.append("strict profile must contain a passing enabled release gate")
        if (
            release_gate.get("minimum_measured_iterations")
            != STRICT_MINIMUM_ITERATIONS
        ):
            errors.append(
                "strict profile release-gate iteration floor does not match policy"
            )
        if (
            release_gate.get("maximum_p99_deadline_load")
            != STRICT_MAXIMUM_P99_DEADLINE_LOAD
        ):
            errors.append("strict profile p99 deadline-load limit does not match policy")
        if (
            release_gate.get("maximum_deadline_miss_rate")
            != STRICT_MAXIMUM_DEADLINE_MISS_RATE
        ):
            errors.append("strict profile deadline-miss rate limit does not match policy")
    elif release_gate.get("enabled") is not False:
        errors.append("advisory profile must not enable the timing release gate")
    allocation_gate = report.get("allocation_gate")
    if allocation_probe and not profile.release_gate:
        if (
            not isinstance(allocation_gate, dict)
            or allocation_gate.get("enabled") is not True
            or allocation_gate.get("passed") is not True
        ):
            errors.append("probed advisory profile must contain a passing allocation gate")

    return {
        "completed": completed,
        "failures": len(failures) if isinstance(failures, list) else None,
        "skipped": len(skipped) if isinstance(skipped, list) else None,
        "release_gate_passed": (
            release_gate.get("passed") if release_gate.get("enabled") else None
        ),
        "report_valid": not errors,
        "report_errors": errors,
    }


def main() -> int:
    args = parse_args()
    runner = args.runner.expanduser().resolve()
    audit_executable = args.audit_executable.expanduser().resolve()
    manifest = args.manifest.expanduser().resolve()
    build_root = args.build_root.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    allocation_probe = (
        args.allocation_probe_library.expanduser().resolve()
        if args.allocation_probe_library is not None
        else None
    )
    summary_path = (
        args.summary.expanduser().resolve()
        if args.summary
        else output_dir / "clap-realtime-release-gates-summary.json"
    )

    for label, path in (
        ("runner", runner),
        ("audit executable", audit_executable),
        ("manifest", manifest),
    ):
        if not path.is_file():
            print(f"Missing {label}: {path}", file=sys.stderr)
            return 2
    if not os.access(runner, os.X_OK) or not os.access(audit_executable, os.X_OK):
        print("Runner and audit executable must be executable", file=sys.stderr)
        return 2
    if not build_root.is_dir():
        print(f"Missing build root: {build_root}", file=sys.stderr)
        return 2
    if allocation_probe is not None and not allocation_probe.is_file():
        print(f"Missing allocation probe library: {allocation_probe}", file=sys.stderr)
        return 2

    selected = list(args.profiles or DEFAULT_PROFILE_KEYS)
    if args.include_advisory:
        for key, profile in PROFILES.items():
            if not profile.release_gate and key not in selected:
                selected.append(key)

    output_dir.mkdir(parents=True, exist_ok=True)
    results: list[dict[str, Any]] = []
    failed = False
    for index, key in enumerate(selected, start=1):
        profile = PROFILES[key]
        output = output_dir / f"clap-realtime-release-gate-{key}.json"
        command = [
            sys.executable,
            str(runner),
            "--audit-executable",
            str(audit_executable),
            "--manifest",
            str(manifest),
            "--build-root",
            str(build_root),
            "--filter",
            profile.plugin_filter,
            "--sample-rates",
            profile.sample_rates,
            "--blocks",
            profile.blocks,
            "--automation-ladder",
            "--output",
            str(output),
        ]
        if profile.release_gate:
            command.append("--release-gate")
        if profile.iterations is not None:
            command.extend(("--iterations", str(profile.iterations)))
        if allocation_probe is not None:
            command.extend(("--allocation-probe-library", str(allocation_probe)))
            if not profile.release_gate:
                command.append("--allocation-gate")

        print(f"[{index}/{len(selected)}] realtime profile {key}", flush=True)
        launch_error: str | None = None
        returncode: int | None = None
        try:
            output.unlink()
        except FileNotFoundError:
            pass
        except OSError as error:
            launch_error = f"could not remove stale report: {error}"
        if launch_error is None:
            try:
                completed = subprocess.run(command, check=False)
                returncode = completed.returncode
            except OSError as error:
                launch_error = f"could not launch profile: {error}"
        result: dict[str, Any] = {
            "profile": key,
            "release_gate": profile.release_gate,
            "returncode": returncode,
            "report": str(output),
        }
        if launch_error is not None:
            result.update(
                report_valid=False,
                report_errors=[launch_error],
            )
        else:
            result.update(
                read_report_summary(
                    output, profile, allocation_probe is not None
                )
            )
        results.append(result)
        if returncode != 0 or not result.get("report_valid", False):
            failed = True

    summary = {
        "schema": "org.s3g.s3g-dsp.clap-realtime-release-gates/v1",
        "allocation_probe_library": (
            str(allocation_probe) if allocation_probe is not None else None
        ),
        "profiles": results,
        "passed": not failed,
    }
    write_json_atomic(summary_path, summary)
    print(
        f"Realtime release profiles: {len(results)} completed; "
        f"overall {'failed' if failed else 'passed'}."
    )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
