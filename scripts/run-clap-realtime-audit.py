#!/usr/bin/env python3
"""Run the CLAP realtime audit for bundles listed in the canonical manifest."""

from __future__ import annotations

import argparse
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
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "scripts" / "clap-bundles.tsv"
SCHEMA = "org.s3g.s3g-dsp.clap-realtime-audit.aggregate/v1"
AUDIT_SCHEMA = "org.s3g.s3g-dsp.clap-realtime-audit/v1"
FULL_SAMPLE_RATES = "48000,96000"
FULL_BLOCK_SIZES = "32,64,128,256"
DEFAULT_EVENT_BURST = 64
MINIMUM_RELEASE_GATE_ITERATIONS = 10_000
DEFAULT_RELEASE_P99_LIMIT = 0.75
# p99 is the robust timing boundary: the one-percent tail remains visible in
# every report, but an offline userspace timer cannot distinguish a process()
# overrun from an operating-system preemption within that tail.  Keep a
# matching per-scenario miss-rate bound so malformed or unexpectedly broad
# tails cannot pass merely because a reported percentile looks healthy.
DEFAULT_RELEASE_MAX_DEADLINE_MISS_RATE = 0.01
AFFECTED_PLUGIN_IDS = frozenset(
    {
        "org.s3g.s3g-dsp.delay-processor-24ch",
        "org.s3g.s3g-dsp.ambisonic-rotate-64",
        "org.s3g.s3g-dsp.ambi-group-rotate-64",
        "org.s3g.s3g-dsp.ambi-group-rotate-128",
        "org.s3g.s3g-dsp.node-bus-mixer",
        "org.s3g.s3g-dsp.spectral-spray",
        "org.s3g.s3g-dsp.8ch-spectral-spray",
        "org.s3g.s3g-dsp.spectral-topology-processor",
        "org.s3g.s3g-dsp.spectral-topology-processor-24ch",
        "org.s3g.s3g-dsp.ambi-water-encoder-64",
        "org.s3g.s3g-dsp.ambi-insect-encoder-64",
    }
)


@dataclass(frozen=True)
class Bundle:
    build_path: str
    installed_name: str
    plugin_id: str
    host_name: str

    def searchable_text(self) -> str:
        return "\n".join(
            (self.build_path, self.installed_name, self.plugin_id, self.host_name)
        )

    def manifest_fields(self) -> dict[str, str]:
        return {
            "build_path": self.build_path,
            "installed_name": self.installed_name,
            "plugin_id": self.plugin_id,
            "host_name": self.host_name,
        }


def positive_integer(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def nonnegative_integer(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be zero or greater")
    return parsed


def positive_number(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be a finite number greater than zero")
    return parsed


def unit_interval(value: str) -> float:
    parsed = positive_number(value)
    if parsed > 1.0:
        raise argparse.ArgumentTypeError("must be no greater than 1.0")
    return parsed


def nonnegative_unit_interval(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if not math.isfinite(parsed) or parsed < 0.0 or parsed > 1.0:
        raise argparse.ArgumentTypeError(
            "must be a finite number between 0.0 and 1.0"
        )
    return parsed


def release_gate_iterations(value: str) -> int:
    parsed = positive_integer(value)
    if parsed < MINIMUM_RELEASE_GATE_ITERATIONS:
        raise argparse.ArgumentTypeError(
            f"must be at least {MINIMUM_RELEASE_GATE_ITERATIONS}"
        )
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run s3g_clap_realtime_audit for selected entries in "
            "scripts/clap-bundles.tsv and combine its JSON reports."
        )
    )
    parser.add_argument(
        "--audit-executable",
        type=Path,
        required=True,
        help="Path to the built s3g_clap_realtime_audit executable.",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help=f"Four-column bundle manifest (default: {DEFAULT_MANIFEST}).",
    )
    parser.add_argument(
        "--build-root",
        type=Path,
        default=ROOT / "build-clap-release" / "plugins",
        help="Directory against which manifest build paths are resolved.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Aggregate JSON path. Omit to write the report to stdout.",
    )
    parser.add_argument(
        "--filter",
        metavar="REGEX",
        help="Audit manifest rows matching this regex in any of their four fields.",
    )
    parser.add_argument(
        "--exclude-filter",
        metavar="REGEX",
        help="Skip plugin IDs matching this regular expression.",
    )
    parser.add_argument(
        "--affected-only",
        action="store_true",
        help=(
            "Select the current eleven documented realtime weak points; "
            "combines with --filter."
        ),
    )
    parser.add_argument(
        "--skip-missing",
        action="store_true",
        help="Record and skip bundles absent from the build instead of failing.",
    )
    parser.add_argument(
        "--sample-rates",
        help="Comma-separated sample rates passed unchanged to the audit executable.",
    )
    parser.add_argument(
        "--full-sweep",
        action="store_true",
        help=(
            "Default unspecified rates to 48000,96000 and blocks to "
            "32,64,128,256. Explicit --sample-rates/--blocks still win."
        ),
    )
    parser.add_argument(
        "--blocks",
        help="Comma-separated block sizes passed unchanged to the audit executable.",
    )
    parser.add_argument(
        "--warmup",
        type=nonnegative_integer,
        help="Warmup block count passed to the audit executable.",
    )
    parser.add_argument(
        "--iterations",
        type=positive_integer,
        help="Measured block count passed to the audit executable.",
    )
    event_group = parser.add_mutually_exclusive_group()
    event_group.add_argument(
        "--event-burst",
        type=nonnegative_integer,
        help="Automation events per measured burst; zero disables that scenario.",
    )
    event_group.add_argument(
        "--event-bursts",
        help=(
            "Comma-separated automation burst ladder passed to the audit "
            "executable. Values must be positive integers."
        ),
    )
    parser.add_argument(
        "--automation-ladder",
        action="store_true",
        help=(
            "Run 1,4,8,16,max event bursts at both identical and distributed "
            "sample offsets. Max is --event-burst when supplied, otherwise 64."
        ),
    )
    parser.add_argument(
        "--distributed-events",
        action="store_true",
        help="Also run each automation burst distributed across the block.",
    )
    parser.add_argument(
        "--control-publication-stress",
        action="store_true",
        help=(
            "Add the independent host-side atomic control-publication stress "
            "scenario; no CLAP API is called from its worker thread."
        ),
    )
    parser.add_argument(
        "--nim-midi-flood",
        action="store_true",
        help="Add E16/BU16 MIDI 1 flooding when the selected plug-in is NIM.",
    )
    parser.add_argument(
        "--allocation-probe",
        action="store_true",
        help=(
            "Pass --allocation-probe to each audit child. The probe dylib must "
            "already be present in this process's DYLD_INSERT_LIBRARIES."
        ),
    )
    parser.add_argument(
        "--allocation-probe-library",
        type=Path,
        help=(
            "macOS probe dylib to inject into each audit child; implies "
            "--allocation-probe."
        ),
    )
    parser.add_argument(
        "--allocation-gate",
        action="store_true",
        help=(
            "Fail if any measured process() block performs an allocation or "
            "deallocation. Requires --allocation-probe."
        ),
    )
    parser.add_argument(
        "--timeout-seconds",
        type=positive_number,
        help=(
            "Maximum time allowed for each plugin audit (default: 120, or "
            "3600 with --release-gate)."
        ),
    )
    parser.add_argument(
        "--release-gate",
        action="store_true",
        help=(
            "Fail unless every measured scenario has enough blocks, p99 "
            "deadline load at or below the threshold, and a deadline-miss "
            "rate within the bounded scheduler-sensitive tail."
        ),
    )
    parser.add_argument(
        "--release-min-iterations",
        type=release_gate_iterations,
        default=MINIMUM_RELEASE_GATE_ITERATIONS,
        help=(
            "Measured blocks required per release-gate scenario "
            f"(default: {MINIMUM_RELEASE_GATE_ITERATIONS}; minimum: "
            f"{MINIMUM_RELEASE_GATE_ITERATIONS})."
        ),
    )
    parser.add_argument(
        "--release-p99-limit",
        type=unit_interval,
        default=DEFAULT_RELEASE_P99_LIMIT,
        help="Maximum p99/deadline ratio for the release gate (default: 0.75).",
    )
    parser.add_argument(
        "--release-max-deadline-miss-rate",
        type=nonnegative_unit_interval,
        default=DEFAULT_RELEASE_MAX_DEADLINE_MISS_RATE,
        help=(
            "Maximum wall-clock deadline-miss fraction per release-gate "
            "scenario (default: 0.01). The p99 limit remains the primary "
            "timing criterion."
        ),
    )
    return parser.parse_args()


def read_manifest(path: Path) -> list[Bundle]:
    bundles: list[Bundle] = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream, delimiter="\t", strict=True)
        try:
            for row in reader:
                line_number = reader.line_num
                if not row or row[0].lstrip().startswith("#"):
                    continue
                if len(row) != 4:
                    raise ValueError(
                        f"{path}:{line_number}: expected four tab-separated fields"
                    )
                if any(not field.strip() for field in row):
                    raise ValueError(f"{path}:{line_number}: fields must not be empty")
                if any(field != field.strip() for field in row):
                    raise ValueError(
                        f"{path}:{line_number}: fields must not have surrounding whitespace"
                    )
                relative = PurePosixPath(row[0])
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError(
                        f"{path}:{line_number}: build path must stay under --build-root"
                    )
                bundles.append(Bundle(*row))
        except csv.Error as error:
            raise ValueError(
                f"{path}:{reader.line_num}: invalid TSV syntax: {error}"
            ) from error
    return bundles


def compile_filter(
    expression: str | None, option: str = "--filter"
) -> re.Pattern[str] | None:
    if expression is None:
        return None
    try:
        return re.compile(expression)
    except re.error as error:
        raise ValueError(f"invalid {option} regex: {error}") from error


def parse_event_bursts(value: str) -> list[int]:
    fields = value.split(",")
    if not fields or any(not field for field in fields):
        raise ValueError("--event-bursts contains an empty item")
    bursts: list[int] = []
    for field in fields:
        try:
            burst = int(field)
        except ValueError as error:
            raise ValueError(
                f"--event-bursts contains a non-integer value: {field!r}"
            ) from error
        if burst <= 0:
            raise ValueError("--event-bursts values must be greater than zero")
        if burst not in bursts:
            bursts.append(burst)
    return bursts


def normalize_options(args: argparse.Namespace) -> None:
    if args.allocation_probe_library is not None:
        args.allocation_probe = True
    if args.allocation_gate and not args.allocation_probe:
        raise ValueError("--allocation-gate requires --allocation-probe")
    if args.full_sweep:
        if args.sample_rates is None:
            args.sample_rates = FULL_SAMPLE_RATES
        if args.blocks is None:
            args.blocks = FULL_BLOCK_SIZES

    if args.automation_ladder:
        if args.event_bursts is not None:
            raise ValueError(
                "--automation-ladder cannot be combined with --event-bursts"
            )
        maximum = (
            args.event_burst
            if args.event_burst is not None
            else DEFAULT_EVENT_BURST
        )
        if maximum == 0:
            raise ValueError("--automation-ladder requires a nonzero maximum burst")
        ladder = [value for value in (1, 4, 8, 16) if value <= maximum]
        ladder.append(maximum)
        args.event_bursts = ",".join(
            str(value) for value in dict.fromkeys(ladder)
        )
        args.event_burst = None
        args.distributed_events = True

    if args.event_bursts is not None:
        args.event_bursts = ",".join(
            str(value) for value in parse_event_bursts(args.event_bursts)
        )

    if args.release_gate:
        if args.iterations is None:
            args.iterations = args.release_min_iterations
        elif args.iterations < args.release_min_iterations:
            raise ValueError(
                "--release-gate requires --iterations to be at least "
                f"{args.release_min_iterations}"
            )
    if args.timeout_seconds is None:
        args.timeout_seconds = 3600.0 if args.release_gate else 120.0


def bundle_path(build_root: Path, bundle: Bundle) -> Path:
    candidate = (build_root / Path(*PurePosixPath(bundle.build_path).parts)).resolve()
    try:
        candidate.relative_to(build_root)
    except ValueError as error:
        raise ValueError(
            f"manifest build path escapes --build-root: {bundle.build_path}"
        ) from error
    return candidate


def forwarded_options(args: argparse.Namespace) -> list[str]:
    options: list[str] = []
    for name in (
        "sample_rates",
        "blocks",
        "warmup",
        "iterations",
        "event_burst",
        "event_bursts",
    ):
        value = getattr(args, name)
        if value is not None:
            options.extend((f"--{name.replace('_', '-')}", str(value)))
    if args.allocation_probe:
        options.append("--allocation-probe")
    if args.distributed_events:
        options.append("--distributed-events")
    if args.control_publication_stress:
        options.append("--control-publication-stress")
    if args.nim_midi_flood:
        options.append("--nim-midi-flood")
    return options


def emit_child_diagnostics(
    bundle: Bundle, result: subprocess.CompletedProcess[str]
) -> None:
    print(
        f"Realtime audit failed for {bundle.host_name} "
        f"({bundle.plugin_id}) with exit code {result.returncode}.",
        file=sys.stderr,
    )
    for label, content in (("stdout", result.stdout), ("stderr", result.stderr)):
        content = content.strip()
        if content:
            print(f"  {label}:", file=sys.stderr)
            for line in content.splitlines():
                print(f"    {line}", file=sys.stderr)


def allocation_probe_shape_error(scenario: dict[str, Any]) -> str | None:
    probe = scenario.get("allocation_probe")
    if not isinstance(probe, dict):
        return "scenario allocation_probe must be an object"
    totals = probe.get("totals")
    if not isinstance(totals, dict):
        return "scenario allocation_probe.totals must be an object"
    measured_blocks = probe.get("measured_blocks")
    measured_iterations = scenario.get("measured_iterations")
    if (
        isinstance(measured_blocks, bool)
        or not isinstance(measured_blocks, int)
        or measured_blocks < 0
    ):
        return (
            "scenario allocation_probe.measured_blocks must be a nonnegative "
            f"integer, got {measured_blocks!r}"
        )
    if (
        isinstance(measured_iterations, bool)
        or not isinstance(measured_iterations, int)
        or measured_iterations < 0
    ):
        return (
            "scenario measured_iterations must be a nonnegative integer when "
            f"the allocation probe is enabled, got {measured_iterations!r}"
        )
    if measured_blocks != measured_iterations:
        return (
            "scenario allocation_probe.measured_blocks must equal "
            f"measured_iterations, got {measured_blocks} and "
            f"{measured_iterations}"
        )
    for field in ("operations", "allocation_failures", "invalid_alignment_calls"):
        value = totals.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            return (
                f"scenario allocation_probe.totals.{field} must be a "
                f"nonnegative integer, got {value!r}"
            )
    return None


def allocation_violation_reasons(
    scenario: dict[str, Any], required: bool
) -> list[str]:
    probe = scenario.get("allocation_probe")
    if probe is None and not required:
        return []
    shape_error = allocation_probe_shape_error(scenario)
    if shape_error is not None:
        return [shape_error]
    totals = probe["totals"]
    reasons: list[str] = []
    for field in ("operations", "allocation_failures", "invalid_alignment_calls"):
        value = totals[field]
        if value != 0:
            reasons.append(
                f"allocation_probe.totals.{field} must be zero, got {value}"
            )
    return reasons


def validate_audit_report(
    report: Any, bundle: Bundle, allocation_probe: bool
) -> str | None:
    if not isinstance(report, dict):
        return "top level must be an object"

    schema = report.get("schema")
    if schema != AUDIT_SCHEMA:
        return f"schema must be {AUDIT_SCHEMA!r}, got {schema!r}"

    plugin = report.get("plugin")
    if not isinstance(plugin, dict):
        return 'field "plugin" must be an object'
    plugin_id = plugin.get("id")
    if plugin_id != bundle.plugin_id:
        return (
            f"plugin.id must be {bundle.plugin_id!r}, "
            f"got {plugin_id!r}"
        )

    configuration = report.get("configuration")
    if not isinstance(configuration, dict):
        return 'field "configuration" must be an object'
    reported_probe = configuration.get("allocation_probe")
    if not isinstance(reported_probe, bool):
        return 'configuration.allocation_probe must be a boolean'
    if reported_probe != allocation_probe:
        return (
            "configuration.allocation_probe must match the requested "
            f"value {allocation_probe!r}, got {reported_probe!r}"
        )

    scenarios = report.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        return 'field "scenarios" must be a nonempty array'
    for index, scenario in enumerate(scenarios):
        if not isinstance(scenario, dict):
            return f"scenario {index} must be an object"
        if allocation_probe:
            error = allocation_probe_shape_error(scenario)
            if error is not None:
                return f"scenario {index}: {error}"

    return None


def write_report(report: dict[str, Any], output: Path | None) -> None:
    serialized = json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    if output is None:
        sys.stdout.write(serialized)
        return

    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=output.parent,
            prefix=f".{output.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary_name = stream.name
            stream.write(serialized)
        os.replace(temporary_name, output)
        temporary_name = None
    finally:
        if temporary_name is not None:
            try:
                Path(temporary_name).unlink()
            except FileNotFoundError:
                pass


def evaluate_release_gate(
    report: dict[str, Any], minimum_iterations: int, p99_limit: float,
    maximum_deadline_miss_rate: float,
) -> dict[str, Any]:
    violations: list[dict[str, Any]] = []
    evaluated = 0
    measured_total = 0
    deadline_miss_total = 0
    for category in ("failures", "skipped"):
        for item in report[category]:
            violations.append(
                {
                    "plugin_id": item.get("plugin_id"),
                    "host_name": item.get("host_name"),
                    "scenario": None,
                    "reasons": [
                        f"plug-in audit was "
                        f"{'failed' if category == 'failures' else 'skipped'}: "
                        f"{item.get('reason', 'no reason reported')}"
                    ],
                }
            )
    for audit in report["audits"]:
        plugin_id = audit["plugin_id"]
        host_name = audit["host_name"]
        scenarios = audit["report"].get("scenarios", [])
        for scenario in scenarios:
            if not isinstance(scenario, dict):
                violations.append(
                    {
                        "plugin_id": plugin_id,
                        "host_name": host_name,
                        "scenario": None,
                        "reasons": ["scenario report is not an object"],
                    }
                )
                continue
            evaluated += 1
            name = scenario.get("name")
            reasons: list[str] = []
            error = scenario.get("error")
            if error not in (None, ""):
                reasons.append(f"scenario error: {error}")

            measured = scenario.get("measured_iterations")
            measured_valid = not (
                isinstance(measured, bool)
                or not isinstance(measured, int)
                or measured <= 0
            )
            if not measured_valid or measured < minimum_iterations:
                reasons.append(
                    f"measured_iterations must be at least {minimum_iterations}, "
                    f"got {measured!r}"
                )
            if measured_valid:
                measured_total += measured

            misses = scenario.get("deadline_misses")
            misses_valid = not (
                isinstance(misses, bool)
                or not isinstance(misses, int)
                or misses < 0
            )
            if not misses_valid:
                reasons.append(
                    "deadline_misses must be a nonnegative integer, "
                    f"got {misses!r}"
                )
            else:
                deadline_miss_total += misses
                if measured_valid:
                    miss_rate = misses / measured
                    if miss_rate > maximum_deadline_miss_rate:
                        reasons.append(
                            f"deadline miss rate {miss_rate:.6g} exceeds "
                            f"{maximum_deadline_miss_rate:.6g} "
                            f"({misses}/{measured})"
                        )

            deadline_load = scenario.get("deadline_load")
            p99 = deadline_load.get("p99") if isinstance(deadline_load, dict) else None
            if (
                isinstance(p99, bool)
                or not isinstance(p99, (int, float))
                or not math.isfinite(float(p99))
            ):
                reasons.append(f"deadline_load.p99 must be finite, got {p99!r}")
            elif float(p99) > p99_limit:
                reasons.append(
                    f"deadline_load.p99 {float(p99):.6g} exceeds {p99_limit:.6g}"
                )

            reasons.extend(
                allocation_violation_reasons(
                    scenario,
                    bool(report["configuration"].get("allocation_probe")),
                )
            )

            if reasons:
                violations.append(
                    {
                        "plugin_id": plugin_id,
                        "host_name": host_name,
                        "scenario": name,
                        "sample_rate": scenario.get("sample_rate"),
                        "block_size": scenario.get("block_size"),
                        "reasons": reasons,
                    }
                )

    return {
        "enabled": True,
        "passed": not violations and evaluated > 0,
        "minimum_measured_iterations": minimum_iterations,
        "maximum_p99_deadline_load": p99_limit,
        "maximum_deadline_miss_rate": maximum_deadline_miss_rate,
        "observed_measured_iterations": measured_total,
        "observed_deadline_misses": deadline_miss_total,
        "observed_deadline_miss_rate": (
            deadline_miss_total / measured_total if measured_total else None
        ),
        "maximum_realtime_allocation_operations": (
            0 if report["configuration"].get("allocation_probe") else None
        ),
        "evaluated_scenarios": evaluated,
        "violations": violations,
        "limitations": [
            "Wall-clock timing is machine- and scheduler-dependent; compare like-for-like Release builds on a quiet reference machine.",
            "The p99 limit is the robust timing criterion; deadline misses in the excluded one-percent tail remain diagnostic and are rate-bounded per scenario.",
            "The gate measures offline process() calls, not hardware or host xruns.",
            "The allocation probe, when enabled, observes only the thread that calls process().",
        ],
    }


def evaluate_allocation_gate(report: dict[str, Any]) -> dict[str, Any]:
    violations: list[dict[str, Any]] = []
    evaluated = 0
    for category in ("failures", "skipped"):
        for item in report[category]:
            violations.append(
                {
                    "plugin_id": item.get("plugin_id"),
                    "host_name": item.get("host_name"),
                    "scenario": None,
                    "reasons": [
                        f"plug-in audit was "
                        f"{'failed' if category == 'failures' else 'skipped'}: "
                        f"{item.get('reason', 'no reason reported')}"
                    ],
                }
            )
    for audit in report["audits"]:
        for scenario in audit["report"].get("scenarios", []):
            evaluated += 1
            reasons = allocation_violation_reasons(scenario, required=True)
            if reasons:
                violations.append(
                    {
                        "plugin_id": audit["plugin_id"],
                        "host_name": audit["host_name"],
                        "scenario": scenario.get("name"),
                        "sample_rate": scenario.get("sample_rate"),
                        "block_size": scenario.get("block_size"),
                        "reasons": reasons,
                    }
                )
    return {
        "enabled": True,
        "passed": not violations and evaluated > 0,
        "maximum_realtime_allocation_operations": 0,
        "evaluated_scenarios": evaluated,
        "violations": violations,
        "limitations": [
            "The allocation probe observes only the thread that calls process().",
            "Worker-thread and direct malloc_zone_* activity are outside its scope.",
        ],
    }


def main() -> int:
    args = parse_args()
    audit_executable = args.audit_executable.expanduser().resolve()
    manifest = args.manifest.expanduser().resolve()
    build_root = args.build_root.expanduser().resolve()

    try:
        normalize_options(args)
        if args.allocation_probe_library is not None:
            if sys.platform != "darwin":
                raise ValueError(
                    "--allocation-probe-library is supported only on macOS"
                )
            args.allocation_probe_library = (
                args.allocation_probe_library.expanduser().resolve()
            )
            if not args.allocation_probe_library.is_file():
                raise FileNotFoundError(
                    "missing allocation probe library: "
                    f"{args.allocation_probe_library}"
                )
        if not audit_executable.is_file():
            raise FileNotFoundError(f"missing audit executable: {audit_executable}")
        if not os.access(audit_executable, os.X_OK):
            raise PermissionError(f"audit executable is not executable: {audit_executable}")
        bundles = read_manifest(manifest)
        matcher = compile_filter(args.filter)
        exclude_matcher = compile_filter(args.exclude_filter, "--exclude-filter")
    except (OSError, ValueError) as error:
        print(f"CLAP realtime manifest audit failed: {error}", file=sys.stderr)
        return 1

    selected = [
        bundle
        for bundle in bundles
        if (not args.affected_only or bundle.plugin_id in AFFECTED_PLUGIN_IDS)
        and (exclude_matcher is None or not exclude_matcher.search(bundle.plugin_id))
        and (
            matcher is None
            or any(
                matcher.search(field)
                for field in (
                    bundle.build_path,
                    bundle.installed_name,
                    bundle.plugin_id,
                    bundle.host_name,
                )
            )
        )
    ]
    if not selected:
        print("CLAP realtime manifest audit failed: no manifest rows selected", file=sys.stderr)
        return 1

    report: dict[str, Any] = {
        "schema": SCHEMA,
        "manifest": str(manifest),
        "build_root": str(build_root),
        "configuration": {
            "sample_rates": args.sample_rates,
            "blocks": args.blocks,
            "warmup": args.warmup,
            "iterations": args.iterations,
            "event_burst": args.event_burst,
            "event_bursts": args.event_bursts,
            "automation_ladder": args.automation_ladder,
            "distributed_events": args.distributed_events,
            "full_sweep": args.full_sweep,
            "affected_only": args.affected_only,
            "exclude_filter": args.exclude_filter,
            "affected_plugin_ids": (
                sorted(AFFECTED_PLUGIN_IDS) if args.affected_only else None
            ),
            "control_publication_stress": args.control_publication_stress,
            "nim_midi_flood": args.nim_midi_flood,
            "allocation_probe": args.allocation_probe,
            "allocation_probe_library": (
                str(args.allocation_probe_library)
                if args.allocation_probe_library is not None
                else None
            ),
            "allocation_gate": args.allocation_gate,
            "timeout_seconds": args.timeout_seconds,
            "release_gate": args.release_gate,
            "release_min_iterations": args.release_min_iterations,
            "release_p99_limit": args.release_p99_limit,
            "release_max_deadline_miss_rate": (
                args.release_max_deadline_miss_rate
            ),
        },
        "selected": len(selected),
        "completed": 0,
        "audits": [],
        "skipped": [],
        "failures": [],
    }
    options = forwarded_options(args)

    with tempfile.TemporaryDirectory(prefix="s3g-clap-realtime-audit-") as temp:
        json_path = Path(temp) / "plugin-report.json"
        for index, bundle in enumerate(selected, 1):
            try:
                resolved_bundle = bundle_path(build_root, bundle)
            except ValueError as error:
                report["failures"].append(
                    {**bundle.manifest_fields(), "reason": str(error)}
                )
                continue

            if not resolved_bundle.exists():
                missing = {
                    **bundle.manifest_fields(),
                    "bundle": str(resolved_bundle),
                    "reason": "bundle is missing",
                }
                destination = "skipped" if args.skip_missing else "failures"
                report[destination].append(missing)
                if not args.skip_missing:
                    print(f"Missing CLAP bundle: {resolved_bundle}", file=sys.stderr)
                continue

            print(
                f"[{index}/{len(selected)}] {bundle.host_name}",
                file=sys.stderr,
            )
            try:
                json_path.unlink()
            except FileNotFoundError:
                pass
            command = [
                str(audit_executable),
                *options,
                "--json",
                str(json_path),
                str(resolved_bundle),
                bundle.plugin_id,
            ]
            try:
                child_environment = None
                if args.allocation_probe_library is not None:
                    child_environment = os.environ.copy()
                    existing = child_environment.get("DYLD_INSERT_LIBRARIES", "")
                    injected = str(args.allocation_probe_library)
                    child_environment["DYLD_INSERT_LIBRARIES"] = (
                        injected + (os.pathsep + existing if existing else "")
                    )
                result = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=args.timeout_seconds,
                    env=child_environment,
                )
            except subprocess.TimeoutExpired:
                report["failures"].append(
                    {
                        **bundle.manifest_fields(),
                        "bundle": str(resolved_bundle),
                        "reason": (
                            "audit exceeded timeout of "
                            f"{args.timeout_seconds:g} seconds"
                        ),
                    }
                )
                print(
                    f"Realtime audit timed out for {bundle.host_name} "
                    f"after {args.timeout_seconds:g} seconds.",
                    file=sys.stderr,
                )
                continue
            except OSError as error:
                report["failures"].append(
                    {
                        **bundle.manifest_fields(),
                        "bundle": str(resolved_bundle),
                        "reason": f"could not launch audit: {error}",
                    }
                )
                continue

            plugin_report: Any | None = None
            report_error: str | None = None
            try:
                plugin_report = json.loads(json_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                report_error = str(error)

            if result.returncode != 0:
                emit_child_diagnostics(bundle, result)
                failure: dict[str, Any] = {
                    **bundle.manifest_fields(),
                    "bundle": str(resolved_bundle),
                    "reason": f"audit exited with code {result.returncode}",
                }
                if plugin_report is not None:
                    failure["report"] = plugin_report
                elif report_error is not None:
                    failure["report_error"] = report_error
                report["failures"].append(failure)
                continue

            if report_error is not None:
                report["failures"].append(
                    {
                        **bundle.manifest_fields(),
                        "bundle": str(resolved_bundle),
                        "reason": f"invalid audit JSON: {report_error}",
                    }
                )
                continue

            validation_error = validate_audit_report(
                plugin_report, bundle, args.allocation_probe
            )
            if validation_error is not None:
                report["failures"].append(
                    {
                        **bundle.manifest_fields(),
                        "bundle": str(resolved_bundle),
                        "reason": f"invalid audit report: {validation_error}",
                        "report": plugin_report,
                    }
                )
                continue

            report["audits"].append(
                {
                    **bundle.manifest_fields(),
                    "bundle": str(resolved_bundle),
                    "report": plugin_report,
                }
            )

    report["completed"] = len(report["audits"])
    gate_failed = False
    if args.release_gate:
        gate = evaluate_release_gate(
            report, args.release_min_iterations, args.release_p99_limit,
            args.release_max_deadline_miss_rate,
        )
        report["release_gate"] = gate
        gate_failed = not gate["passed"]
    else:
        report["release_gate"] = {"enabled": False}
    allocation_gate_failed = False
    if args.allocation_gate:
        allocation_gate = evaluate_allocation_gate(report)
        report["allocation_gate"] = allocation_gate
        allocation_gate_failed = not allocation_gate["passed"]
    else:
        report["allocation_gate"] = {"enabled": False}
    try:
        write_report(report, args.output)
    except OSError as error:
        print(f"Could not write aggregate report: {error}", file=sys.stderr)
        return 1

    failures = len(report["failures"])
    message = (
        f"CLAP realtime manifest audit: {report['completed']} completed, "
        f"{len(report['skipped'])} skipped, {failures} failed"
    )
    if args.release_gate:
        message += f", release gate {'failed' if gate_failed else 'passed'}"
    if args.allocation_gate:
        message += (
            f", allocation gate "
            f"{'failed' if allocation_gate_failed else 'passed'}"
        )
    print(message + ".", file=sys.stderr)
    return 1 if failures or gate_failed or allocation_gate_failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
