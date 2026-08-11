#!/usr/bin/env python3

"""Capture the canonical CLAP GUI audit inventory as PDF and 3x PNG assets."""

from __future__ import annotations

import argparse
from array import array
import csv
from dataclasses import dataclass
import math
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable
import wave


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CMAKE_FILE = ROOT / "CMakeLists.txt"
DEFAULT_ACTIVE_MANIFEST = ROOT / "scripts" / "clap-bundles.tsv"
DEFAULT_BUILD_DIR = ROOT / "build-clap"
DEFAULT_OUTPUT_DIR = ROOT / "docs" / "assets" / "plugin-guis"
PLUGIN_ID_PREFIX = "org.s3g.s3g-dsp."
DOCUMENTATION_SAMPLE_RECIPES = {
    f"{PLUGIN_ID_PREFIX}loop-processor-8ch": (
        ("loop-field-8ch.wav", 8, 3.6, 1),
    ),
    f"{PLUGIN_ID_PREFIX}multi-loop-processor-8ch": (
        ("source-pulse-1ch.wav", 1, 1.2, 2),
        ("source-ribbon-2ch.wav", 2, 1.5, 3),
        ("source-cluster-4ch.wav", 4, 1.8, 4),
        ("source-field-8ch.wav", 8, 2.1, 5),
    ),
    f"{PLUGIN_ID_PREFIX}ambi-grain-processor": (
        ("ambi-field-16ch.wav", 16, 3.2, 6),
    ),
}

TARGET_FILE_RE = re.compile(r"^\$<TARGET_FILE:([A-Za-z0-9_]+)>$")
OUTPUT_STEM_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
GUI_COMMAND_START_RE = re.compile(
    r"^[ \t]*COMMAND[ \t]+"
    r"\$<TARGET_FILE:s3g_(?:(?:encoder|decoder)_family_gui_smoke|tracker_clap_smoke)>(?=[ \t])"
)
GUI_COMMAND_RE = re.compile(
    r"""
    ^[ \t]*COMMAND[ \t]+
    (?P<harness>\$<TARGET_FILE:s3g_(?:(?:encoder|decoder)_family_gui_smoke|tracker_clap_smoke)>)[ \t]+
    (?P<plugin>\$<TARGET_FILE:[A-Za-z0-9_]+>)[ \t]+
    (?P<plugin_id>org\.s3g\.s3g-dsp\.[A-Za-z0-9._-]+)[ \t]+
    (?P<width>[0-9]+)[ \t]+
    (?P<height>[0-9]+)
    (?:[ \t]+"(?P<prefix>[^"\r\n]+)"[ \t]+
       (?P<mode>responsive-wide|responsive|dynamic|fixed))?
    [ \t]*(?:\#[^\r\n]*)?$
    """,
    re.VERBOSE,
)


@dataclass(frozen=True)
class Bundle:
    build_path: PurePosixPath
    installed_name: str
    plugin_id: str
    host_name: str


@dataclass(frozen=True)
class Capture:
    harness_target: str
    plugin_target: str
    plugin_id: str
    width: int
    height: int
    extra_arguments: tuple[str, ...]
    bundle: Bundle


@dataclass(frozen=True)
class StagedArtifact:
    plugin_id: str
    stem: str
    master_pdf: Path
    png_path: Path


class UsageError(RuntimeError):
    """A deterministic inventory or command-line validation error."""


def display_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path.resolve())


def target_name(token: str, *, context: str) -> str:
    match = TARGET_FILE_RE.fullmatch(token)
    if not match:
        raise UsageError(f"{context}: invalid CMake target expression {token!r}")
    return match.group(1)


def read_active_manifest(path: Path) -> dict[str, Bundle]:
    bundles: dict[str, Bundle] = {}
    try:
        stream = path.open("r", encoding="utf-8", newline="")
    except OSError as exc:
        raise UsageError(f"cannot read active CLAP manifest {path}: {exc}") from exc

    with stream:
        reader = csv.reader(stream, delimiter="\t", strict=True)
        try:
            for row in reader:
                if not row or row[0].lstrip().startswith("#"):
                    continue
                if len(row) != 4 or any(not value for value in row):
                    raise UsageError(
                        f"{display_path(path)}:{reader.line_num}: expected four "
                        "non-empty tab-separated columns"
                    )
                build_path, installed_name, plugin_id, host_name = row
                relative_path = PurePosixPath(build_path)
                if (
                    relative_path.is_absolute()
                    or len(relative_path.parts) != 2
                    or any(part in {".", ".."} for part in relative_path.parts)
                    or relative_path.as_posix() != build_path
                    or not build_path.endswith(".clap")
                ):
                    raise UsageError(
                        f"{display_path(path)}:{reader.line_num}: unsafe build path "
                        f"{build_path!r}"
                    )
                if not plugin_id.startswith(PLUGIN_ID_PREFIX):
                    raise UsageError(
                        f"{display_path(path)}:{reader.line_num}: unexpected CLAP ID "
                        f"{plugin_id!r}"
                    )
                if plugin_id in bundles:
                    raise UsageError(
                        f"{display_path(path)}:{reader.line_num}: duplicate CLAP ID "
                        f"{plugin_id!r}"
                    )
                bundles[plugin_id] = Bundle(
                    relative_path, installed_name, plugin_id, host_name
                )
        except csv.Error as exc:
            raise UsageError(
                f"{display_path(path)}:{reader.line_num}: invalid TSV syntax: {exc}"
            ) from exc

    if not bundles:
        raise UsageError(f"active CLAP manifest is empty: {path}")
    return bundles


def read_gui_inventory(
    path: Path, bundles: dict[str, Bundle], manifest_path: Path
) -> list[Capture]:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise UsageError(f"cannot read CMake GUI audit inventory {path}: {exc}") from exc

    captures: list[Capture] = []
    seen_ids: set[str] = set()
    for line, command in enumerate(source.splitlines(), start=1):
        if not GUI_COMMAND_START_RE.match(command):
            continue
        context = f"{display_path(path)}:{line}"
        match = GUI_COMMAND_RE.fullmatch(command)
        if match is None:
            raise UsageError(
                f"{context}: unsupported GUI smoke command syntax or trailing "
                "arguments"
            )
        harness = target_name(match.group("harness"), context=context)
        plugin_target = target_name(match.group("plugin"), context=context)
        plugin_id = match.group("plugin_id")
        if plugin_id in seen_ids:
            raise UsageError(f"{context}: duplicate GUI audit CLAP ID {plugin_id!r}")
        bundle = bundles.get(plugin_id)
        if bundle is None:
            raise UsageError(
                f"{context}: GUI audit CLAP ID {plugin_id!r} is absent from "
                f"{display_path(manifest_path)}"
            )
        prefix = match.group("prefix")
        mode = match.group("mode")
        extra_arguments = (prefix, mode) if prefix is not None and mode is not None else ()
        captures.append(
            Capture(
                harness,
                plugin_target,
                plugin_id,
                int(match.group("width")),
                int(match.group("height")),
                extra_arguments,
                bundle,
            )
        )
        seen_ids.add(plugin_id)

    if not captures:
        raise UsageError(f"no family GUI smoke commands found in {path}")
    return captures


def short_plugin_id(plugin_id: str) -> str:
    if not plugin_id.startswith(PLUGIN_ID_PREFIX):
        raise UsageError(f"cannot shorten unexpected CLAP ID {plugin_id!r}")
    return plugin_id[len(PLUGIN_ID_PREFIX) :]


def canonical_plugin_id(value: str, inventory: dict[str, Capture]) -> str:
    if value in inventory:
        return value
    expanded = f"{PLUGIN_ID_PREFIX}{value}"
    if expanded in inventory:
        return expanded
    raise UsageError(
        f"unknown GUI plugin ID {value!r}; use --list to see the audited inventory"
    )


def select_captures(
    requested: Iterable[str], captures: list[Capture]
) -> list[Capture]:
    requested_list = list(requested)
    if not requested_list:
        return captures
    inventory = {capture.plugin_id: capture for capture in captures}
    selected: list[Capture] = []
    seen: set[str] = set()
    for value in requested_list:
        plugin_id = canonical_plugin_id(value, inventory)
        if plugin_id not in seen:
            selected.append(inventory[plugin_id])
            seen.add(plugin_id)
    return selected


def read_name_map(path: Path, inventory: dict[str, Capture]) -> dict[str, str]:
    names: dict[str, str] = {}
    try:
        stream = path.open("r", encoding="utf-8", newline="")
    except OSError as exc:
        raise UsageError(f"cannot read output-name map {path}: {exc}") from exc

    with stream:
        reader = csv.reader(stream, delimiter="\t", strict=True)
        try:
            for row in reader:
                if not row or row[0].lstrip().startswith("#"):
                    continue
                if len(row) != 2 or any(not value for value in row):
                    raise UsageError(
                        f"{display_path(path)}:{reader.line_num}: expected a CLAP ID "
                        "and output stem separated by one tab"
                    )
                plugin_id = canonical_plugin_id(row[0], inventory)
                stem = row[1]
                validate_output_stem(stem, context=f"{display_path(path)}:{reader.line_num}")
                if plugin_id in names:
                    raise UsageError(
                        f"{display_path(path)}:{reader.line_num}: duplicate name for "
                        f"{plugin_id!r}"
                    )
                names[plugin_id] = stem
        except csv.Error as exc:
            raise UsageError(
                f"{display_path(path)}:{reader.line_num}: invalid TSV syntax: {exc}"
            ) from exc
    return names


def validate_output_stem(stem: str, *, context: str) -> None:
    if not OUTPUT_STEM_RE.fullmatch(stem) or stem in {".", ".."}:
        raise UsageError(
            f"{context}: output name must be a filename stem containing only "
            "letters, digits, dots, underscores, and hyphens"
        )


def output_names(
    selected: list[Capture], name_map: dict[str, str], output_name: str | None
) -> dict[str, str]:
    if output_name is not None:
        if len(selected) != 1:
            raise UsageError("--output-name requires exactly one selected plugin ID")
        validate_output_stem(output_name, context="--output-name")
        name_map = {**name_map, selected[0].plugin_id: output_name}

    result = {
        capture.plugin_id: name_map.get(
            capture.plugin_id, short_plugin_id(capture.plugin_id)
        )
        for capture in selected
    }
    collisions: dict[str, str] = {}
    for plugin_id, stem in result.items():
        previous = collisions.get(stem.casefold())
        if previous is not None:
            raise UsageError(
                f"output stem {stem!r} is shared by {previous!r} and {plugin_id!r}"
            )
        collisions[stem.casefold()] = plugin_id
    return result


def output_owner(stem: str, names: dict[str, str]) -> tuple[str, str] | None:
    folded = stem.casefold()
    matches = [
        (len(base.casefold()), plugin_id, base)
        for plugin_id, base in names.items()
        if folded == base.casefold()
        or folded.startswith(f"{base.casefold()}.")
    ]
    if not matches:
        return None
    _, plugin_id, base = max(matches)
    return plugin_id, base


def claim_artifact_stem(
    plugin_id: str,
    stem: str,
    names: dict[str, str],
    claimed: dict[str, tuple[str, str]],
) -> None:
    validate_output_stem(stem, context=f"GUI output from {plugin_id}")
    owner = output_owner(stem, names)
    if owner is None or owner[0] != plugin_id:
        owner_text = owner[0] if owner is not None else "no known plugin"
        raise UsageError(
            f"output artifact {stem!r} from {plugin_id!r} belongs to the "
            f"filename namespace reserved for {owner_text!r}"
        )
    key = stem.casefold()
    previous = claimed.get(key)
    if previous is not None:
        raise UsageError(
            f"output artifact {stem!r} from {plugin_id!r} collides with "
            f"{previous[1]!r} from {previous[0]!r}"
        )
    claimed[key] = (plugin_id, stem)


def find_renderer(*, dry_run: bool) -> str:
    renderer = shutil.which("pdftocairo") or shutil.which("pdftoppm")
    if renderer:
        return renderer
    if dry_run:
        return "pdftocairo"
    raise UsageError(
        "pdftocairo or pdftoppm is required (both are provided by Poppler)"
    )


def expected_bundle_path(build_dir: Path, capture: Capture) -> Path:
    return build_dir / "plugins" / Path(capture.bundle.build_path)


def bundle_executable(bundle_path: Path, capture: Capture, *, dry_run: bool) -> Path:
    plist_path = bundle_path / "Contents" / "Info.plist"
    if not plist_path.is_file():
        if dry_run:
            return bundle_path / "Contents" / "MacOS" / bundle_path.stem
        raise UsageError(
            f"canonical built bundle is missing Info.plist: {display_path(plist_path)}"
        )
    try:
        with plist_path.open("rb") as stream:
            metadata = plistlib.load(stream)
    except (OSError, plistlib.InvalidFileException) as exc:
        raise UsageError(f"cannot read {display_path(plist_path)}: {exc}") from exc

    actual_id = metadata.get("CFBundleIdentifier")
    if actual_id != capture.plugin_id:
        raise UsageError(
            f"{display_path(plist_path)} identifies {actual_id!r}, expected "
            f"{capture.plugin_id!r}"
        )
    executable_name = metadata.get("CFBundleExecutable")
    if (
        not isinstance(executable_name, str)
        or not executable_name
        or PurePosixPath(executable_name).name != executable_name
    ):
        raise UsageError(
            f"{display_path(plist_path)} has an unsafe or missing CFBundleExecutable"
        )
    executable = bundle_path / "Contents" / "MacOS" / executable_name
    if not dry_run and (not executable.is_file() or not os.access(executable, os.X_OK)):
        raise UsageError(
            f"canonical bundle executable is missing or not executable: "
            f"{display_path(executable)}"
        )
    return executable


def run(command: list[str], *, env: dict[str, str] | None = None) -> None:
    print(f"+ {shlex.join(command)}", flush=True)
    try:
        subprocess.run(command, check=True, env=env)
    except subprocess.CalledProcessError as exc:
        raise UsageError(
            f"command failed with exit status {exc.returncode}: {shlex.join(command)}"
        ) from exc
    except OSError as exc:
        raise UsageError(f"cannot run {command[0]!r}: {exc}") from exc


def write_documentation_wave(
    path: Path, channels: int, duration_seconds: float, scene: int
) -> None:
    """Write a compact deterministic PCM fixture with a legible waveform."""

    sample_rate = 24000
    frame_count = int(round(duration_seconds * sample_rate))
    try:
        with wave.open(str(path), "wb") as output:
            output.setnchannels(channels)
            output.setsampwidth(2)
            output.setframerate(sample_rate)
            for first_frame in range(0, frame_count, 1024):
                values = array("h")
                last_frame = min(first_frame + 1024, frame_count)
                for frame in range(first_frame, last_frame):
                    time = frame / sample_rate
                    contour = 0.62 + 0.24 * math.sin(
                        2.0 * math.pi * (0.31 + scene * 0.017) * time
                    )
                    transient_phase = (
                        time * (1.25 + 0.08 * scene)
                    ) % 1.0
                    transient = math.exp(-24.0 * transient_phase)
                    for channel in range(channels):
                        frequency = 73.0 + 19.0 * scene + 11.0 * channel
                        phase = 0.29 * scene + 0.41 * channel
                        signal = contour * (
                            0.53 * math.sin(
                                2.0 * math.pi * frequency * time + phase
                            )
                            + 0.23 * math.sin(
                                2.0 * math.pi * frequency * 2.017 * time
                                + 0.7 * phase
                            )
                        )
                        signal += 0.19 * transient * math.sin(
                            2.0 * math.pi
                            * (frequency * 3.1 + 17.0)
                            * time
                            + phase
                        )
                        signal *= 1.0 - 0.018 * channel
                        values.append(
                            max(-32767, min(32767, int(round(signal * 26000.0))))
                        )
                if sys.byteorder != "little":
                    values.byteswap()
                output.writeframesraw(values.tobytes())
    except (OSError, wave.Error) as exc:
        raise UsageError(
            f"cannot create documentation audio fixture {path}: {exc}"
        ) from exc


def documentation_sample_environment(
    capture_dir: Path, plugin_id: str
) -> dict[str, str]:
    recipes = DOCUMENTATION_SAMPLE_RECIPES.get(plugin_id, ())
    environment: dict[str, str] = {}
    for index, (name, channels, duration, scene) in enumerate(recipes):
        path = capture_dir / name
        write_documentation_wave(path, channels, duration, scene)
        variable = "S3G_GUI_DOCUMENTATION_SAMPLE_PATH"
        if index > 0:
            variable = f"{variable}_{index + 1}"
        environment[variable] = str(path)
    return environment


def build_selected(build_dir: Path, selected: list[Capture]) -> None:
    cmake = shutil.which("cmake")
    if not cmake:
        raise UsageError("cmake is required to build the selected GUI targets")
    targets = list(
        dict.fromkeys(
            [capture.harness_target for capture in selected]
            + [capture.plugin_target for capture in selected]
        )
    )
    run([cmake, "--build", str(build_dir), "--target", *targets])


def capture_command(
    build_dir: Path, capture: Capture, capture_dir: Path, *, dry_run: bool
) -> list[str]:
    harness = build_dir / capture.harness_target
    if not dry_run and (not harness.is_file() or not os.access(harness, os.X_OK)):
        raise UsageError(
            f"GUI smoke harness is missing or not executable: {display_path(harness)}"
        )
    plugin = bundle_executable(
        expected_bundle_path(build_dir, capture), capture, dry_run=dry_run
    )
    return [
        str(harness),
        str(plugin),
        capture.plugin_id,
        str(capture.width),
        str(capture.height),
        *capture.extra_arguments,
    ]


def render_capture(
    renderer: str,
    source_pdf: Path,
    master_pdf: Path,
    png_path: Path,
    dpi: int,
) -> None:
    command = [
        renderer,
        "-png",
        "-singlefile",
        "-r",
        str(dpi),
        str(source_pdf),
        str(png_path.with_suffix("")),
    ]
    run(command)
    if not png_path.is_file() or png_path.stat().st_size == 0:
        raise UsageError(f"renderer did not create {display_path(png_path)}")
    try:
        shutil.copy2(source_pdf, master_pdf)
    except OSError as exc:
        raise UsageError(
            f"cannot stage PDF master {display_path(master_pdf)}: {exc}"
        ) from exc


def generated_pdfs(capture_dir: Path, plugin_id: str) -> list[Path]:
    result = []
    for path in sorted(capture_dir.glob("*.pdf")):
        stem = path.stem
        if stem == plugin_id or stem.startswith(f"{plugin_id}."):
            result.append(path)
    main_pdf = capture_dir / f"{plugin_id}.pdf"
    if main_pdf not in result:
        raise UsageError(f"GUI smoke did not emit its main PDF for {plugin_id}")
    return result


def path_exists(path: Path) -> bool:
    return path.exists() or path.is_symlink()


def stale_assets(
    output_dir: Path,
    selected: list[Capture],
    names: dict[str, str],
    expected: dict[str, set[str]],
) -> list[Path]:
    selected_ids = {capture.plugin_id for capture in selected}
    result: list[Path] = []
    locations = ((output_dir, ".png"), (output_dir / "masters", ".pdf"))
    for directory, suffix in locations:
        if not directory.is_dir():
            continue
        for path in directory.glob(f"*{suffix}"):
            if not path.is_file() and not path.is_symlink():
                continue
            stem = path.name[: -len(suffix)]
            owner = output_owner(stem, names)
            if owner is None or owner[0] not in selected_ids:
                continue
            if stem.casefold() not in expected.get(owner[0], set()):
                result.append(path)
    return result


def publish_staged(
    output_dir: Path,
    staging_dir: Path,
    artifacts: list[StagedArtifact],
    selected: list[Capture],
    names: dict[str, str],
) -> None:
    masters_dir = output_dir / "masters"
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
        masters_dir.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise UsageError(
            f"cannot create output directories below {display_path(output_dir)}: {exc}"
        ) from exc

    replacements: list[tuple[Path, Path]] = []
    expected: dict[str, set[str]] = {}
    for artifact in artifacts:
        replacements.extend(
            (
                (artifact.png_path, output_dir / f"{artifact.stem}.png"),
                (artifact.master_pdf, masters_dir / f"{artifact.stem}.pdf"),
            )
        )
        expected.setdefault(artifact.plugin_id, set()).add(
            artifact.stem.casefold()
        )

    stale = stale_assets(output_dir, selected, names, expected)
    for source, destination in replacements:
        if not source.is_file() or source.stat().st_size == 0:
            raise UsageError(
                f"staged asset is missing or empty: {display_path(source)}"
            )
        if path_exists(destination) and not (
            destination.is_file() or destination.is_symlink()
        ):
            raise UsageError(
                f"refusing to replace non-file output path: "
                f"{display_path(destination)}"
            )

    targets = {destination for _, destination in replacements}
    targets.update(stale)
    rollback_dir = staging_dir / "rollback"
    backups: list[tuple[Path, Path]] = []
    published: list[Path] = []
    try:
        for destination in sorted(targets, key=lambda path: path.as_posix()):
            if not path_exists(destination):
                continue
            backup = rollback_dir / destination.relative_to(output_dir)
            backup.parent.mkdir(parents=True, exist_ok=True)
            os.replace(destination, backup)
            backups.append((destination, backup))
        for source, destination in replacements:
            os.replace(source, destination)
            published.append(destination)
    except OSError as exc:
        rollback_errors: list[str] = []
        backed_up = {destination for destination, _ in backups}
        for destination in reversed(published):
            if destination in backed_up or not path_exists(destination):
                continue
            try:
                destination.unlink()
            except OSError as rollback_exc:
                rollback_errors.append(str(rollback_exc))
        for destination, backup in reversed(backups):
            try:
                os.replace(backup, destination)
            except OSError as rollback_exc:
                rollback_errors.append(str(rollback_exc))
        detail = (
            f"; rollback also failed: {'; '.join(rollback_errors)}"
            if rollback_errors
            else ""
        )
        raise UsageError(f"cannot publish staged GUI assets: {exc}{detail}") from exc


def capture_selected(
    build_dir: Path,
    output_dir: Path,
    renderer: str,
    selected: list[Capture],
    names: dict[str, str],
    dpi: int,
) -> int:
    try:
        output_dir.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        raise UsageError(
            f"cannot create output parent {display_path(output_dir.parent)}: {exc}"
        ) from exc

    with tempfile.TemporaryDirectory(
        prefix=".s3g-gui-publication-", dir=output_dir.parent
    ) as publication:
        staging_dir = Path(publication)
        staged_output = staging_dir / "assets"
        staged_masters = staged_output / "masters"
        staged_masters.mkdir(parents=True)
        claimed: dict[str, tuple[str, str]] = {}
        artifacts: list[StagedArtifact] = []

        for index, capture in enumerate(selected, start=1):
            print(
                f"[{index}/{len(selected)}] {capture.plugin_id} "
                f"({capture.width}x{capture.height})",
                flush=True,
            )
            with tempfile.TemporaryDirectory(prefix="s3g-gui-capture-") as temporary:
                capture_dir = Path(temporary)
                environment = os.environ.copy()
                environment["S3G_GUI_SMOKE_PDF_DIR"] = str(capture_dir)
                environment["S3G_GUI_DOCUMENTATION_CAPTURE"] = "1"
                environment.update(
                    documentation_sample_environment(
                        capture_dir, capture.plugin_id
                    )
                )
                run(
                    capture_command(build_dir, capture, capture_dir, dry_run=False),
                    env=environment,
                )
                plans: list[tuple[Path, str]] = []
                for source_pdf in generated_pdfs(capture_dir, capture.plugin_id):
                    variant = source_pdf.stem[len(capture.plugin_id) :]
                    destination_stem = f"{names[capture.plugin_id]}{variant}"
                    claim_artifact_stem(
                        capture.plugin_id,
                        destination_stem,
                        names,
                        claimed,
                    )
                    plans.append((source_pdf, destination_stem))
                for source_pdf, destination_stem in plans:
                    master_pdf = staged_masters / f"{destination_stem}.pdf"
                    png_path = staged_output / f"{destination_stem}.png"
                    render_capture(
                        renderer, source_pdf, master_pdf, png_path, dpi
                    )
                    artifacts.append(
                        StagedArtifact(
                            capture.plugin_id,
                            destination_stem,
                            master_pdf,
                            png_path,
                        )
                    )

        publish_staged(
            output_dir, staging_dir, artifacts, selected, names
        )
        return len(artifacts)


def print_inventory(selected: list[Capture], names: dict[str, str]) -> None:
    print("CLAP ID\tOUTPUT STEM\tNATIVE SIZE\tHOST NAME")
    for capture in selected:
        print(
            f"{capture.plugin_id}\t{names[capture.plugin_id]}\t"
            f"{capture.width}x{capture.height}\t{capture.bundle.host_name}"
        )


def print_dry_run(
    build_dir: Path,
    output_dir: Path,
    renderer: str,
    selected: list[Capture],
    names: dict[str, str],
    dpi: int,
    no_build: bool,
) -> None:
    if not no_build:
        targets = list(
            dict.fromkeys(
                [capture.harness_target for capture in selected]
                + [capture.plugin_target for capture in selected]
            )
        )
        print(shlex.join(["cmake", "--build", str(build_dir), "--target", *targets]))
    capture_placeholder = Path("<temporary-pdf-directory>")
    staged_output = Path("<temporary-publication-directory>") / "assets"
    for capture in selected:
        command = capture_command(
            build_dir, capture, capture_placeholder, dry_run=True
        )
        fixture_environment = []
        for index, recipe in enumerate(
            DOCUMENTATION_SAMPLE_RECIPES.get(capture.plugin_id, ())
        ):
            variable = "S3G_GUI_DOCUMENTATION_SAMPLE_PATH"
            if index > 0:
                variable = f"{variable}_{index + 1}"
            fixture_environment.append(
                f"{variable}="
                f"{shlex.quote(str(capture_placeholder / recipe[0]))}"
            )
        fixture_prefix = " ".join(fixture_environment)
        if fixture_prefix:
            fixture_prefix += " "
        print(
            f"S3G_GUI_SMOKE_PDF_DIR={shlex.quote(str(capture_placeholder))} "
            "S3G_GUI_DOCUMENTATION_CAPTURE=1 "
            f"{fixture_prefix}"
            f"{shlex.join(command)}"
        )
        source = capture_placeholder / f"{capture.plugin_id}.pdf"
        master = (
            staged_output / "masters" / f"{names[capture.plugin_id]}.pdf"
        )
        png_stem = staged_output / names[capture.plugin_id]
        print(
            shlex.join(
                [
                    renderer,
                    "-png",
                    "-singlefile",
                    "-r",
                    str(dpi),
                    str(source),
                    str(png_stem),
                ]
            )
        )
        print(f"stage PDF master: {source} -> {master}")
    print(
        "Additional PDFs emitted by a smoke scenario retain their variant "
        "suffixes. The complete staged set is then published to "
        f"{display_path(output_dir)}."
    )


def positive_int(value: str) -> int:
    try:
        number = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Build and run the canonical macOS CLAP GUI audit entries, retain "
            "their vector PDF masters, and render high-resolution PNGs. With no "
            "PLUGIN_ID arguments, all audited GUI plugins are selected."
        ),
        epilog=(
            "PLUGIN_ID accepts a full ID or its short suffix. Examples:\n"
            "  scripts/generate-plugin-screenshots.py --list\n"
            "  scripts/generate-plugin-screenshots.py no-input-mixer-8ch\n"
            "  scripts/generate-plugin-screenshots.py --dry-run ambi-point-encoder-64\n"
            "A --name-map file contains: CLAP_ID<TAB>OUTPUT_STEM (comments begin with #). "
            "When no PLUGIN_ID is given, a name map also selects its listed plugins."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("plugin_ids", nargs="*", metavar="PLUGIN_ID")
    parser.add_argument(
        "--list", action="store_true", help="list the selected audited GUI inventory and exit"
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print build, capture, and render commands only"
    )
    parser.add_argument(
        "--no-build", action="store_true", help="use existing canonical build artifacts"
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help=f"configured CLAP build directory (default: {display_path(DEFAULT_BUILD_DIR)})",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"PNG destination; PDF masters go below masters/ (default: {display_path(DEFAULT_OUTPUT_DIR)})",
    )
    parser.add_argument(
        "--scale",
        type=positive_int,
        default=3,
        help="raster scale relative to PDF's 72 dpi logical size (default: 3)",
    )
    parser.add_argument(
        "--output-name",
        metavar="STEM",
        help="override the output filename stem for one selected plugin",
    )
    parser.add_argument(
        "--name-map",
        type=Path,
        metavar="TSV",
        help="optional two-column TSV of per-plugin output filename stems",
    )
    parser.add_argument(
        "--active-manifest",
        type=Path,
        default=DEFAULT_ACTIVE_MANIFEST,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--cmake-file", type=Path, default=DEFAULT_CMAKE_FILE, help=argparse.SUPPRESS
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest_path = args.active_manifest.resolve()
        bundles = read_active_manifest(manifest_path)
        captures = read_gui_inventory(
            args.cmake_file.resolve(), bundles, manifest_path
        )
        inventory = {capture.plugin_id: capture for capture in captures}
        name_map = (
            read_name_map(args.name_map.resolve(), inventory) if args.name_map else {}
        )
        if args.name_map is not None and not name_map:
            raise UsageError(
                f"output-name map contains no entries: {args.name_map.resolve()}"
            )
        selected = select_captures(args.plugin_ids, captures)
        if not args.plugin_ids and args.name_map is not None:
            selected = [
                capture for capture in captures if capture.plugin_id in name_map
            ]
        names = output_names(selected, name_map, args.output_name)

        if args.list:
            print_inventory(selected, names)
            return 0

        build_dir = args.build_dir.resolve()
        output_dir = args.output_dir.resolve()
        dpi = 72 * args.scale
        renderer = find_renderer(dry_run=args.dry_run)
        if args.dry_run:
            print_dry_run(
                build_dir,
                output_dir,
                renderer,
                selected,
                names,
                dpi,
                args.no_build,
            )
            return 0

        if sys.platform != "darwin":
            raise UsageError("GUI capture requires macOS/AppKit")
        if not args.no_build:
            build_selected(build_dir, selected)
        written = capture_selected(
            build_dir,
            output_dir,
            renderer,
            selected,
            names,
            dpi,
        )
        print(
            f"Wrote {written} PNG asset(s) to {display_path(output_dir)} and "
            f"PDF master(s) to {display_path(output_dir / 'masters')}"
        )
        return 0
    except UsageError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
