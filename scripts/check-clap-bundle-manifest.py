#!/usr/bin/env python3

"""Validate the CLAP packaging manifests against the source tree and builds."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
import json
from pathlib import Path, PurePosixPath
import os
import plistlib
import re
import subprocess
import sys
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ACTIVE_MANIFEST = ROOT / "scripts" / "clap-bundles.tsv"
DEFAULT_LEGACY_MANIFEST = ROOT / "scripts" / "clap-legacy-bundles.tsv"
PACKAGE_VERIFIER = ROOT / "scripts" / "verify-macos-clap-package.py"

SAFE_COMPONENT_RE = re.compile(r"^[a-z0-9][a-z0-9_]*$")
SAFE_BUNDLE_RE = re.compile(r"^[a-z0-9][a-z0-9_]*\.clap$")
SAFE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
SAFE_LEGACY_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*\*?$")
VARIABLE_RE = re.compile(r"\$\{[A-Za-z_][A-Za-z0-9_]*\}")
BARE_OUTPUT_SUFFIX_RE = re.compile(r" [1-9][0-9]*$")
NO_AUDIO_OUTPUT_HOST_NAMES = {
    "s3g Relay",
    "s3g Tracker",
    "s3g Utility NIM Gesture",
}

# Every configured CLAP directory belongs to the 0.7 package inventory. Keep
# these collections in place so a later source-only experiment must be added
# deliberately and remains visible to the manifest audit.
FUTURE_ONLY_CMAKE_DIRECTORIES: set[str] = set()
PREVIEW_ONLY_CMAKE_GATES: dict[str, str] = {}
SOURCE_ONLY_CMAKE_DIRECTORIES: set[str] = set()


@dataclass(frozen=True)
class ActiveBundle:
    line: int
    build_path: str
    installed_name: str
    plugin_id: str
    host_name: str


@dataclass(frozen=True)
class LegacyBundle:
    line: int
    installed_name: str
    expected_id: str
    replacement: str


class Audit:
    def __init__(self) -> None:
        self.errors: list[str] = []

    def error(self, location: str, message: str) -> None:
        self.errors.append(f"{location}: {message}")

    def finish(self) -> None:
        if not self.errors:
            return
        for error in self.errors:
            print(f"error: {error}", file=sys.stderr)
        noun = "error" if len(self.errors) == 1 else "errors"
        raise SystemExit(f"CLAP bundle manifest audit failed with {len(self.errors)} {noun}")


def canonical_bundle_name(host_name: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", host_name.casefold()).strip("_")
    return f"{slug}.clap"


def location(path: Path, line: int | None = None) -> str:
    try:
        display = path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        display = str(path)
    return f"{display}:{line}" if line is not None else display


def read_tsv(path: Path, columns: int, audit: Audit) -> list[tuple[int, list[str]]]:
    rows: list[tuple[int, list[str]]] = []
    try:
        stream = path.open("r", encoding="utf-8", newline="")
    except OSError as exc:
        audit.error(location(path), f"cannot read manifest: {exc}")
        return rows

    with stream:
        reader = csv.reader(stream, delimiter="\t", strict=True)
        try:
            for row in reader:
                line = reader.line_num
                if not row or (row[0].lstrip().startswith("#")):
                    continue
                row_location = location(path, line)
                if len(row) != columns:
                    audit.error(
                        row_location,
                        f"expected {columns} tab-separated columns, found {len(row)}",
                    )
                    continue
                if any(not value for value in row):
                    audit.error(row_location, "columns must not be empty")
                    continue
                for index, value in enumerate(row, start=1):
                    if value != value.strip():
                        audit.error(
                            row_location,
                            f"column {index} has leading or trailing whitespace",
                        )
                    if any(ord(character) < 0x20 for character in value):
                        audit.error(row_location, f"column {index} contains a control character")
                rows.append((line, row))
        except csv.Error as exc:
            audit.error(location(path, reader.line_num), f"invalid TSV syntax: {exc}")
    return rows


def validate_build_path(value: str) -> str | None:
    if "\\" in value or "//" in value:
        return "must use normalized forward-slash separators"
    path = PurePosixPath(value)
    if path.is_absolute() or len(path.parts) != 2:
        return "must be a two-component relative path: clap_directory/bundle.clap"
    directory, filename = path.parts
    if directory in {".", ".."} or filename in {".", ".."}:
        return "must not contain dot path components"
    if not directory.startswith("clap_") or not SAFE_COMPONENT_RE.fullmatch(directory):
        return f"has unsafe CLAP source directory {directory!r}"
    if not SAFE_BUNDLE_RE.fullmatch(filename):
        return f"has unsafe bundle filename {filename!r}"
    if path.as_posix() != value:
        return "must be a normalized POSIX path"
    return None


def validate_bundle_filename(value: str) -> str | None:
    if PurePosixPath(value).name != value or "\\" in value:
        return "must be a filename, not a path"
    if not SAFE_BUNDLE_RE.fullmatch(value):
        return "must contain only lowercase ASCII letters, digits, underscores, and .clap"
    return None


def report_duplicates(
    values: Iterable[tuple[str, int]],
    label: str,
    manifest: Path,
    audit: Audit,
    *,
    case_insensitive: bool = False,
) -> None:
    first_seen: dict[str, tuple[str, int]] = {}
    for value, line in values:
        key = value.casefold() if case_insensitive else value
        previous = first_seen.get(key)
        if previous is None:
            first_seen[key] = (value, line)
            continue
        previous_value, previous_line = previous
        audit.error(
            location(manifest, line),
            f"duplicate {label} {value!r}; first declared on line {previous_line} as {previous_value!r}",
        )


def parse_active(path: Path, audit: Audit) -> list[ActiveBundle]:
    bundles = [ActiveBundle(line, *row) for line, row in read_tsv(path, 4, audit)]
    for bundle in bundles:
        row_location = location(path, bundle.line)
        path_error = validate_build_path(bundle.build_path)
        if path_error:
            audit.error(row_location, f"invalid relative build path: {path_error}")
        filename_error = validate_bundle_filename(bundle.installed_name)
        if filename_error:
            audit.error(row_location, f"invalid canonical installed filename: {filename_error}")

        expected_name = canonical_bundle_name(bundle.host_name)
        if bundle.installed_name != expected_name:
            audit.error(
                row_location,
                f"canonical filename must be {expected_name!r} for host name {bundle.host_name!r}",
            )
        if not bundle.host_name.startswith("s3g "):
            audit.error(row_location, "host name must begin with 's3g '")
        if not bundle.host_name.isascii():
            audit.error(row_location, "host name must be ASCII so its package slug is portable")
        if re.search(r"[0-9]+ch$", bundle.host_name):
            audit.error(
                row_location,
                "host name output width must use a bare number without 'ch'",
            )
        elif (
            bundle.host_name not in NO_AUDIO_OUTPUT_HOST_NAMES
            and not BARE_OUTPUT_SUFFIX_RE.search(bundle.host_name)
        ):
            audit.error(
                row_location,
                "audio host name must end with its main output-bus channel count",
            )
        if not SAFE_ID_RE.fullmatch(bundle.plugin_id):
            audit.error(row_location, f"unsafe CLAP identifier {bundle.plugin_id!r}")
        elif not bundle.plugin_id.startswith("org.s3g.s3g-dsp."):
            audit.error(row_location, "CLAP identifier must use the org.s3g.s3g-dsp namespace")

    report_duplicates(
        ((bundle.build_path, bundle.line) for bundle in bundles),
        "relative build path",
        path,
        audit,
        case_insensitive=True,
    )
    report_duplicates(
        ((bundle.installed_name, bundle.line) for bundle in bundles),
        "installed filename",
        path,
        audit,
        case_insensitive=True,
    )
    report_duplicates(
        ((bundle.plugin_id, bundle.line) for bundle in bundles),
        "CLAP identifier",
        path,
        audit,
        case_insensitive=True,
    )
    report_duplicates(
        ((bundle.host_name, bundle.line) for bundle in bundles),
        "host name",
        path,
        audit,
        case_insensitive=True,
    )
    if not bundles:
        audit.error(location(path), "active manifest has no bundle entries")
    return bundles


def parse_legacy(
    path: Path, active: list[ActiveBundle], audit: Audit
) -> list[LegacyBundle]:
    bundles = [LegacyBundle(line, *row) for line, row in read_tsv(path, 3, audit)]
    active_names = {bundle.installed_name: bundle for bundle in active}
    active_names_folded = {name.casefold(): name for name in active_names}

    for bundle in bundles:
        row_location = location(path, bundle.line)
        filename_error = validate_bundle_filename(bundle.installed_name)
        if filename_error:
            audit.error(row_location, f"invalid retired installed filename: {filename_error}")
        if not SAFE_LEGACY_ID_RE.fullmatch(bundle.expected_id):
            audit.error(row_location, f"unsafe legacy identifier pattern {bundle.expected_id!r}")
        elif "*" in bundle.expected_id and not bundle.expected_id.endswith(".*"):
            audit.error(row_location, "legacy identifier wildcard must be a terminal '.*'")

        active_collision = active_names_folded.get(bundle.installed_name.casefold())
        if active_collision is not None:
            audit.error(
                row_location,
                f"legacy filename collides with active canonical filename {active_collision!r}",
            )

        if bundle.replacement != "retired":
            replacement_error = validate_bundle_filename(bundle.replacement)
            if replacement_error:
                audit.error(row_location, f"invalid replacement filename: {replacement_error}")
            elif bundle.replacement not in active_names:
                audit.error(
                    row_location,
                    f"replacement {bundle.replacement!r} is not in the active manifest",
                )
            if bundle.replacement == bundle.installed_name:
                audit.error(row_location, "legacy filename cannot replace itself")

    report_duplicates(
        ((bundle.installed_name, bundle.line) for bundle in bundles),
        "legacy installed filename",
        path,
        audit,
        case_insensitive=True,
    )
    if not bundles:
        audit.error(location(path), "legacy manifest has no bundle entries")
    return bundles


def cmake_property_expressions(text: str, property_name: str) -> list[str]:
    return re.findall(rf"\b{re.escape(property_name)}\s+\"([^\"]+)\"", text)


def expression_matches(expression: str, expected: str, cmake_text: str) -> bool:
    if not VARIABLE_RE.search(expression):
        return expression == expected
    if VARIABLE_RE.fullmatch(expression):
        return f'"{expected}"' in cmake_text

    pieces: list[str] = []
    offset = 0
    for match in VARIABLE_RE.finditer(expression):
        pieces.append(re.escape(expression[offset : match.start()]))
        pieces.append(r"[A-Za-z0-9_.+-]+")
        offset = match.end()
    pieces.append(re.escape(expression[offset:]))
    return re.fullmatch("".join(pieces), expected) is not None


def cmake_property_matches(
    text: str, property_name: str, expected: str
) -> tuple[bool, bool]:
    expressions = cmake_property_expressions(text, property_name)
    return bool(expressions), any(
        expression_matches(expression, expected, text) for expression in expressions
    )


def descriptor_block(cpp_text: str) -> str:
    match = re.search(r"\bclap_plugin_descriptor_t\s+\w+\s*\{", cpp_text)
    if match is None:
        return ""
    end = cpp_text.find("};", match.end())
    if end < 0:
        return cpp_text[match.start() : match.start() + 2048]
    return cpp_text[match.start() : end + 2]


def validate_source_metadata(
    source_root: Path, active_manifest: Path, bundles: list[ActiveBundle], audit: Audit
) -> None:
    plugins_root = source_root / "plugins"
    top_cmake = source_root / "CMakeLists.txt"
    try:
        top_text = top_cmake.read_text(encoding="utf-8")
    except OSError as exc:
        audit.error(location(top_cmake), f"cannot read top-level CMake file: {exc}")
        return

    configured_directories = set(
        re.findall(r"add_subdirectory\(plugins/(clap_[A-Za-z0-9_]+)\)", top_text)
    )
    active_directories = configured_directories - SOURCE_ONLY_CMAKE_DIRECTORIES
    manifest_directories = {
        PurePosixPath(bundle.build_path).parts[0]
        for bundle in bundles
        if validate_build_path(bundle.build_path) is None
    }
    for directory in sorted(active_directories - manifest_directories):
        audit.error(location(active_manifest), f"active CMake directory {directory!r} is absent")
    for directory in sorted(manifest_directories - active_directories):
        audit.error(
            location(active_manifest),
            f"manifest directory {directory!r} is not active in the top-level CMake file",
        )
    for directory in sorted(
        manifest_directories & SOURCE_ONLY_CMAKE_DIRECTORIES
    ):
        audit.error(
            location(active_manifest),
            f"source-only CMake directory {directory!r} must not be in the active manifest",
        )
    for directory in sorted(
        configured_directories & SOURCE_ONLY_CMAKE_DIRECTORIES
    ):
        gate = PREVIEW_ONLY_CMAKE_GATES.get(
            directory, "S3G_BUILD_FUTURE_COMPONENTS"
        )
        gated = re.search(
            rf"if\({re.escape(gate)}\)\s*"
            rf"add_subdirectory\(plugins/{re.escape(directory)}\)\s*endif\(\)",
            top_text,
        )
        if gated is None:
            audit.error(
                location(top_cmake),
                f"source-only CMake directory {directory!r} is not gated by {gate}",
            )

    cache: dict[str, tuple[Path, str, list[Path], str]] = {}
    for bundle in bundles:
        if validate_build_path(bundle.build_path) is not None:
            continue
        directory, source_bundle_name = PurePosixPath(bundle.build_path).parts
        row_location = location(active_manifest, bundle.line)
        if directory not in cache:
            plugin_dir = plugins_root / directory
            cmake_path = plugin_dir / "CMakeLists.txt"
            try:
                cmake_text = cmake_path.read_text(encoding="utf-8")
            except OSError as exc:
                audit.error(row_location, f"cannot read {cmake_path}: {exc}")
                cache[directory] = (cmake_path, "", [], "")
                continue
            cpp_paths = sorted(plugin_dir.glob("*_clap.cpp"))
            if not cpp_paths:
                audit.error(row_location, f"{plugin_dir} has no *_clap.cpp descriptor source")
            cpp_parts: list[str] = []
            for cpp_path in cpp_paths:
                try:
                    cpp_part = cpp_path.read_text(encoding="utf-8")
                    cpp_parts.append(cpp_part)
                    # Product variants may deliberately compile a shared
                    # implementation through a tiny *_clap.cpp wrapper. Read
                    # direct local C++ implementation includes as descriptor
                    # metadata too; ordinary headers remain outside this
                    # source-level manifest check.
                    for include in re.findall(
                        r'^\s*#include\s+"([^"]+\.cpp)"',
                        cpp_part,
                        flags=re.MULTILINE,
                    ):
                        included_path = (cpp_path.parent / include).resolve()
                        try:
                            included_path.relative_to(plugins_root.resolve())
                            cpp_parts.append(included_path.read_text(
                                encoding="utf-8"))
                        except (OSError, ValueError) as exc:
                            audit.error(row_location,
                                f"cannot read local descriptor implementation "
                                f"{included_path}: {exc}")
                except OSError as exc:
                    audit.error(row_location, f"cannot read {cpp_path}: {exc}")
            cache[directory] = (cmake_path, cmake_text, cpp_paths, "\n".join(cpp_parts))

        cmake_path, cmake_text, cpp_paths, cpp_text = cache[directory]
        if not cmake_text:
            continue
        source_stem = source_bundle_name.removesuffix(".clap")
        declared, matched = cmake_property_matches(cmake_text, "OUTPUT_NAME", source_stem)
        if not declared:
            audit.error(row_location, f"{cmake_path} has no OUTPUT_NAME property")
        elif not matched:
            audit.error(
                row_location,
                f"relative build bundle {source_bundle_name!r} is not declared by CMake OUTPUT_NAME",
            )

        extension_declared, extension_matched = cmake_property_matches(
            cmake_text, "BUNDLE_EXTENSION", "clap"
        )
        if not extension_declared or not extension_matched:
            audit.error(row_location, f"{cmake_path} does not declare BUNDLE_EXTENSION \"clap\"")

        for property_name, expected, label in (
            ("MACOSX_BUNDLE_GUI_IDENTIFIER", bundle.plugin_id, "CFBundle identifier"),
            ("MACOSX_BUNDLE_BUNDLE_NAME", bundle.host_name, "CFBundle name"),
        ):
            declared, matched = cmake_property_matches(cmake_text, property_name, expected)
            if not declared:
                audit.error(row_location, f"{cmake_path} has no {property_name} property")
            elif not matched:
                audit.error(
                    row_location,
                    f"manifest {label} {expected!r} is not declared by {property_name}",
                )

        block = descriptor_block(cpp_text)
        if not block:
            sources = ", ".join(str(path) for path in cpp_paths) or str(cmake_path.parent)
            audit.error(row_location, f"no clap_plugin_descriptor_t initializer found in {sources}")
            continue

        combined_source = f"{cmake_text}\n{cpp_text}"
        if bundle.plugin_id not in combined_source:
            audit.error(
                row_location,
                f"CLAP identifier {bundle.plugin_id!r} is absent from CMake and descriptor source",
            )
        if bundle.host_name not in combined_source:
            audit.error(
                row_location,
                f"host name {bundle.host_name!r} is absent from CMake and descriptor source",
            )

        if bundle.plugin_id not in cpp_text and not re.search(
            r"(?:PLUGIN_ID|kPluginId|pluginId\s*\()", block
        ):
            audit.error(row_location, "descriptor does not expose a source-configurable plugin ID")
        if bundle.host_name not in cpp_text and not re.search(
            r"(?:PLUGIN_NAME|kPluginName|kHostName|pluginName\s*\()", block
        ):
            audit.error(row_location, "descriptor does not expose a source-configurable host name")


def validate_built_bundles(
    build_root: Path,
    active_manifest: Path,
    bundles: list[ActiveBundle],
    audit: Audit,
    *,
    verify_descriptors: bool,
    verify_descriptor_versions: bool,
) -> None:
    for bundle in bundles:
        if validate_build_path(bundle.build_path) is not None:
            continue
        row_location = location(active_manifest, bundle.line)
        bundle_path = build_root / PurePosixPath(bundle.build_path)
        if not bundle_path.is_dir() or bundle_path.is_symlink():
            audit.error(row_location, f"built bundle is missing: {bundle_path}")
            continue

        plist_path = bundle_path / "Contents" / "Info.plist"
        if plist_path.is_symlink():
            audit.error(row_location, f"built bundle plist is a symlink: {plist_path}")
            continue
        try:
            with plist_path.open("rb") as stream:
                metadata = plistlib.load(stream)
        except (OSError, plistlib.InvalidFileException) as exc:
            audit.error(row_location, f"cannot read built bundle plist {plist_path}: {exc}")
            continue

        actual_id = metadata.get("CFBundleIdentifier")
        if actual_id != bundle.plugin_id:
            audit.error(
                row_location,
                f"built CFBundleIdentifier is {actual_id!r}, expected {bundle.plugin_id!r}",
            )
        actual_name = metadata.get("CFBundleName")
        if actual_name != bundle.host_name:
            audit.error(
                row_location,
                f"built CFBundleName is {actual_name!r}, expected {bundle.host_name!r}",
            )

        short_version = metadata.get("CFBundleShortVersionString")
        bundle_version = metadata.get("CFBundleVersion")
        if not isinstance(short_version, str) or not short_version:
            audit.error(row_location, "built plist has no valid CFBundleShortVersionString")
        if not isinstance(bundle_version, str) or not bundle_version:
            audit.error(row_location, "built plist has no valid CFBundleVersion")
        if (
            isinstance(short_version, str)
            and short_version
            and isinstance(bundle_version, str)
            and bundle_version
            and short_version != bundle_version
        ):
            audit.error(
                row_location,
                f"built CFBundleShortVersionString {short_version!r} does not match "
                f"CFBundleVersion {bundle_version!r}",
            )

        executable_name = metadata.get("CFBundleExecutable")
        if not isinstance(executable_name, str) or not executable_name:
            audit.error(row_location, "built plist has no valid CFBundleExecutable")
            continue
        if PurePosixPath(executable_name).name != executable_name:
            audit.error(row_location, f"unsafe CFBundleExecutable {executable_name!r}")
            continue
        executable = bundle_path / "Contents" / "MacOS" / executable_name
        if not executable.is_file() or executable.is_symlink():
            audit.error(row_location, f"bundle executable is missing: {executable}")
        elif not os.access(executable, os.X_OK):
            audit.error(row_location, f"bundle executable is not executable: {executable}")
        elif verify_descriptors:
            result = subprocess.run(
                [
                    sys.executable,
                    str(PACKAGE_VERIFIER),
                    "--inspect-descriptor",
                    str(executable),
                    bundle.plugin_id,
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                detail = result.stderr.strip() or result.stdout.strip() or "unknown loader error"
                audit.error(row_location, f"cannot inspect built CLAP descriptor: {detail}")
                continue
            try:
                descriptor = json.loads(result.stdout)
            except json.JSONDecodeError as exc:
                audit.error(row_location, f"descriptor inspector returned invalid JSON: {exc}")
                continue
            descriptor_name = descriptor.get("name")
            if descriptor_name != bundle.host_name:
                audit.error(
                    row_location,
                    f"built CLAP descriptor name is {descriptor_name!r}, "
                    f"expected {bundle.host_name!r}",
                )
            descriptor_version = descriptor.get("version")
            if verify_descriptor_versions and descriptor_version != short_version:
                audit.error(
                    row_location,
                    f"built CLAP descriptor version {descriptor_version!r} does not match "
                    f"CFBundleShortVersionString {short_version!r}",
                )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate canonical CLAP package names, legacy cleanup entries, source "
            "metadata, and optionally built macOS bundles."
        )
    )
    parser.add_argument(
        "--active-manifest",
        type=Path,
        default=DEFAULT_ACTIVE_MANIFEST,
        help=f"active four-column TSV (default: {DEFAULT_ACTIVE_MANIFEST})",
    )
    parser.add_argument(
        "--skip-descriptor-check",
        action="store_true",
        help=(
            "skip loading built CLAP descriptors when parity is deferred to the "
            "package staging synchronizer or during a cross-architecture audit"
        ),
    )
    parser.add_argument(
        "--defer-descriptor-version",
        action="store_true",
        help=(
            "verify runtime descriptor IDs and names but defer descriptor/plist "
            "version parity to the package staging synchronizer"
        ),
    )
    parser.add_argument(
        "--legacy-manifest",
        type=Path,
        default=DEFAULT_LEGACY_MANIFEST,
        help=f"legacy three-column TSV (default: {DEFAULT_LEGACY_MANIFEST})",
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=ROOT,
        help=f"repository root containing CMakeLists.txt and plugins/ (default: {ROOT})",
    )
    parser.add_argument(
        "--build-root",
        type=Path,
        help=(
            "optional directory containing manifest-relative bundles, normally "
            "build-clap-release/plugins"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    audit = Audit()
    active_manifest = args.active_manifest.resolve()
    legacy_manifest = args.legacy_manifest.resolve()
    source_root = args.source_root.resolve()
    active = parse_active(active_manifest, audit)
    legacy = parse_legacy(legacy_manifest, active, audit)
    validate_source_metadata(source_root, active_manifest, active, audit)
    if args.build_root is not None:
        validate_built_bundles(
            args.build_root.resolve(),
            active_manifest,
            active,
            audit,
            verify_descriptors=not args.skip_descriptor_check,
            verify_descriptor_versions=not args.defer_descriptor_version,
        )
    audit.finish()

    message = (
        f"CLAP bundle manifest audit passed: {len(active)} active bundles, "
        f"{len(legacy)} legacy names; source metadata verified"
    )
    if args.build_root is not None:
        message += f"; built bundles verified under {args.build_root.resolve()}"
        if args.skip_descriptor_check:
            message += "; runtime descriptor parity deferred"
        elif args.defer_descriptor_version:
            message += "; runtime descriptor versions deferred"
    print(message)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
