#!/usr/bin/env python3

"""Verify a staged or zipped s3g No Input Mixer macOS app package."""

from __future__ import annotations

import argparse
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import stat
import subprocess
import sys
import tempfile
import zipfile


PACKAGE_PREFIX = "s3g-no-input-mixer-app-macos-arm64-"
APP_NAME = "s3g No Input Mixer.app"
APP_BUNDLE_NAME = "s3g No Input Mixer"
APP_BUNDLE_ID = "org.s3g.s3g-dsp.no-input-mixer-standalone"
APP_EXECUTABLE = "s3g No Input Mixer"
APP_ICON = "no_input_mixer.icns"
INSTALLER_NAME = "Install s3g No Input Mixer.command"
PROVENANCE_RELATIVE_PATH = Path("Installer Data") / "build-provenance.txt"
TOP_LEVEL_ALLOWLIST = {
    APP_NAME,
    INSTALLER_NAME,
    "README.txt",
    "LICENSE.txt",
    "THIRD_PARTY_NOTICES.md",
    "RELEASE_NOTES.md",
    "Installer Data",
}
INSTALLER_DATA_ALLOWLIST = {"build-provenance.txt"}
SAFE_RELEASE_VERSION_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z.+_-]*$")
NUMERIC_VERSION_RE = re.compile(r"^[0-9]+(?:\.[0-9]+){1,3}$")
GIT_REVISION_RE = re.compile(r"^[0-9a-fA-F]{40,64}$")
MAX_ARCHIVE_MEMBERS = 4096
MAX_ARCHIVE_UNCOMPRESSED_BYTES = 256 * 1024 * 1024


class VerificationError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise VerificationError(message)


def run_checked(command: list[str], label: str) -> str:
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        fail(f"cannot run {label}: {exc}")
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        fail(f"{label} failed: {detail}")
    return result.stdout


def safe_release_version(value: str) -> bool:
    return bool(SAFE_RELEASE_VERSION_RE.fullmatch(value))


def release_version_from_root(root_name: str) -> str:
    if not root_name.startswith(PACKAGE_PREFIX):
        fail(
            f"package root must begin with {PACKAGE_PREFIX!r}, found {root_name!r}"
        )
    version = root_name[len(PACKAGE_PREFIX) :]
    if not safe_release_version(version):
        fail(f"package root has an unsafe or empty release version: {root_name!r}")
    return version


def bundle_version_matches_release(bundle_version: str, release_version: str) -> bool:
    if not NUMERIC_VERSION_RE.fullmatch(bundle_version):
        return False
    return release_version == bundle_version or release_version.startswith(
        f"{bundle_version}-"
    )


def validate_member_name(filename: str) -> tuple[str, ...]:
    if not filename or "\x00" in filename or "\\" in filename:
        fail(f"unsafe archive member path: {filename!r}")
    if filename.startswith("/"):
        fail(f"unsafe absolute archive member path: {filename!r}")
    raw_parts = filename.split("/")
    if raw_parts[-1] == "":
        raw_parts = raw_parts[:-1]
    if not raw_parts or any(part in {"", ".", ".."} for part in raw_parts):
        fail(f"unsafe archive member path: {filename!r}")
    if any(any(ord(character) < 32 for character in part) for part in raw_parts):
        fail(f"archive member contains a control character: {filename!r}")
    pure = PurePosixPath(*raw_parts)
    if pure.is_absolute() or pure.parts != tuple(raw_parts):
        fail(f"unsafe archive member path: {filename!r}")
    return tuple(raw_parts)


def validate_zip_members(path: Path, requested_release_version: str | None) -> str:
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        fail(f"cannot open package archive {path}: {exc}")

    roots: set[str] = set()
    member_names: set[str] = set()
    casefolded_names: set[str] = set()
    uncompressed_bytes = 0
    with archive:
        members = archive.infolist()
        if not members:
            fail(f"package archive is empty: {path}")
        if len(members) > MAX_ARCHIVE_MEMBERS:
            fail(
                f"package archive has {len(members)} members; "
                f"maximum is {MAX_ARCHIVE_MEMBERS}"
            )
        for member in members:
            parts = validate_member_name(member.filename)
            normalized = "/".join(parts)
            normalized_casefold = normalized.casefold()
            if normalized in member_names:
                fail(f"package archive has a duplicate member: {member.filename!r}")
            if normalized_casefold in casefolded_names:
                fail(
                    "package archive has case-colliding members, including "
                    f"{member.filename!r}"
                )
            member_names.add(normalized)
            casefolded_names.add(normalized_casefold)
            roots.add(parts[0])

            if member.flag_bits & 0x1:
                fail(f"package archive contains an encrypted member: {member.filename!r}")
            mode = member.external_attr >> 16
            file_type = stat.S_IFMT(mode)
            if stat.S_ISLNK(mode):
                fail(f"package archive contains a symlink: {member.filename!r}")
            if file_type not in {0, stat.S_IFREG, stat.S_IFDIR}:
                fail(f"package archive contains a special file: {member.filename!r}")
            uncompressed_bytes += member.file_size
            if uncompressed_bytes > MAX_ARCHIVE_UNCOMPRESSED_BYTES:
                fail(
                    "package archive expands beyond the allowed "
                    f"{MAX_ARCHIVE_UNCOMPRESSED_BYTES} bytes"
                )

    if len(roots) != 1:
        fail(
            "package archive must contain exactly one root directory, found "
            f"{sorted(roots)}"
        )
    root_name = next(iter(roots))
    archive_release_version = release_version_from_root(root_name)
    if (
        requested_release_version is not None
        and archive_release_version != requested_release_version
    ):
        fail(
            f"archive root release version {archive_release_version!r} does not "
            f"match requested {requested_release_version!r}"
        )
    return root_name


def prepare_package(
    path: Path,
    temporary_root: Path,
    requested_release_version: str | None,
) -> tuple[Path, str]:
    if path.is_symlink():
        fail(f"package path must not be a symlink: {path}")
    if path.is_dir():
        package_root = path.resolve()
        release_version = release_version_from_root(package_root.name)
        if (
            requested_release_version is not None
            and release_version != requested_release_version
        ):
            fail(
                f"staging root release version {release_version!r} does not "
                f"match requested {requested_release_version!r}"
            )
        return package_root, release_version
    if not path.is_file() or path.suffix.casefold() != ".zip":
        fail(f"package must be a staging directory or .zip archive: {path}")

    root_name = validate_zip_members(path, requested_release_version)
    ditto = Path("/usr/bin/ditto")
    if not ditto.is_file():
        fail("/usr/bin/ditto is required to preserve permissions while extracting")
    temporary_root.mkdir(parents=True)
    run_checked(
        [str(ditto), "-x", "-k", str(path), str(temporary_root)],
        "package archive extraction",
    )
    package_root = temporary_root / root_name
    if not package_root.is_dir() or package_root.is_symlink():
        fail(f"archive root was not extracted as a safe directory: {package_root}")
    release_version = release_version_from_root(root_name)
    return package_root, release_version


def validate_tree_entries(package_root: Path) -> None:
    if package_root.is_symlink() or not package_root.is_dir():
        fail(f"package root is not a safe directory: {package_root}")
    for entry in package_root.rglob("*"):
        try:
            metadata = entry.lstat()
        except OSError as exc:
            fail(f"cannot inspect package entry {entry}: {exc}")
        if stat.S_ISLNK(metadata.st_mode):
            fail(f"package contains a symlink: {entry}")
        if not (stat.S_ISREG(metadata.st_mode) or stat.S_ISDIR(metadata.st_mode)):
            fail(f"package contains a special filesystem entry: {entry}")
        try:
            relative = entry.relative_to(package_root)
        except ValueError:
            fail(f"package entry escaped its root: {entry}")
        for part in relative.parts:
            if (
                part in {"", ".", ".."}
                or "\\" in part
                or any(ord(character) < 32 for character in part)
            ):
                fail(f"package contains an unsafe path component: {relative}")


def require_exact_layout(package_root: Path) -> None:
    try:
        actual_top_level = {child.name for child in package_root.iterdir()}
    except OSError as exc:
        fail(f"cannot list package root {package_root}: {exc}")
    if actual_top_level != TOP_LEVEL_ALLOWLIST:
        missing = sorted(TOP_LEVEL_ALLOWLIST - actual_top_level)
        extra = sorted(actual_top_level - TOP_LEVEL_ALLOWLIST)
        fail(f"package top-level layout differs; missing={missing}, extra={extra}")

    installer_data = package_root / "Installer Data"
    if not installer_data.is_dir() or installer_data.is_symlink():
        fail(f"missing or unsafe Installer Data directory: {installer_data}")
    try:
        actual_installer_data = {child.name for child in installer_data.iterdir()}
    except OSError as exc:
        fail(f"cannot list {installer_data}: {exc}")
    if actual_installer_data != INSTALLER_DATA_ALLOWLIST:
        missing = sorted(INSTALLER_DATA_ALLOWLIST - actual_installer_data)
        extra = sorted(actual_installer_data - INSTALLER_DATA_ALLOWLIST)
        fail(f"Installer Data layout differs; missing={missing}, extra={extra}")

    for name in (
        INSTALLER_NAME,
        "README.txt",
        "LICENSE.txt",
        "THIRD_PARTY_NOTICES.md",
        "RELEASE_NOTES.md",
    ):
        path = package_root / name
        if not path.is_file() or path.is_symlink():
            fail(f"missing or unsafe package file: {path}")


def read_plist_string(
    metadata: dict[object, object], key: str, bundle_path: Path
) -> str:
    value = metadata.get(key)
    if not isinstance(value, str) or not value:
        fail(f"{bundle_path}: Info.plist has no valid {key}")
    return value


def executable_minimum_macos(executable: Path) -> str:
    vtool = Path("/usr/bin/vtool")
    if vtool.is_file():
        output = run_checked(
            [str(vtool), "-show-build", str(executable)],
            f"deployment-target check for {executable}",
        )
    else:
        output = run_checked(
            ["/usr/bin/otool", "-l", str(executable)],
            f"deployment-target check for {executable}",
        )
    versions = set(
        re.findall(r"(?m)^\s*minos\s+([0-9]+(?:\.[0-9]+){1,2})\s*$", output)
    )
    if len(versions) != 1:
        fail(
            f"could not resolve one macOS deployment target from {executable}: "
            f"{sorted(versions)}"
        )
    return next(iter(versions))


def verify_app(package_root: Path, release_version: str) -> tuple[str, str]:
    app_path = package_root / APP_NAME
    if not app_path.is_dir() or app_path.is_symlink():
        fail(f"missing or unsafe packaged application: {app_path}")

    plist_path = app_path / "Contents" / "Info.plist"
    if not plist_path.is_file() or plist_path.is_symlink():
        fail(f"missing or unsafe application Info.plist: {plist_path}")
    try:
        with plist_path.open("rb") as stream:
            metadata = plistlib.load(stream)
    except (OSError, plistlib.InvalidFileException) as exc:
        fail(f"cannot read application Info.plist {plist_path}: {exc}")
    if not isinstance(metadata, dict):
        fail(f"application Info.plist is not a dictionary: {plist_path}")

    expected_strings = {
        "CFBundleIdentifier": APP_BUNDLE_ID,
        "CFBundleName": APP_BUNDLE_NAME,
        "CFBundleExecutable": APP_EXECUTABLE,
        "CFBundlePackageType": "APPL",
        "CFBundleIconFile": APP_ICON,
    }
    for key, expected in expected_strings.items():
        actual = read_plist_string(metadata, key, app_path)
        if actual != expected:
            fail(f"{app_path}: {key} {actual!r} != {expected!r}")

    short_version = read_plist_string(
        metadata, "CFBundleShortVersionString", app_path
    )
    bundle_version = read_plist_string(metadata, "CFBundleVersion", app_path)
    if short_version != bundle_version:
        fail(
            f"{app_path}: CFBundleShortVersionString {short_version!r} != "
            f"CFBundleVersion {bundle_version!r}"
        )
    if not bundle_version_matches_release(bundle_version, release_version):
        fail(
            f"{app_path}: numeric bundle version {bundle_version!r} is not "
            f"compatible with release label {release_version!r}"
        )

    executable = app_path / "Contents" / "MacOS" / APP_EXECUTABLE
    if (
        not executable.is_file()
        or executable.is_symlink()
        or not os.access(executable, os.X_OK)
    ):
        fail(f"missing or non-executable application binary: {executable}")
    icon = app_path / "Contents" / "Resources" / APP_ICON
    if not icon.is_file() or icon.is_symlink():
        fail(f"missing or unsafe application icon: {icon}")

    architectures = run_checked(
        ["/usr/bin/lipo", "-archs", str(executable)],
        f"architecture check for {app_path}",
    ).strip()
    if architectures != "arm64":
        fail(f"{app_path}: expected arm64-only binary, found {architectures!r}")
    run_checked(
        [
            "/usr/bin/codesign",
            "--verify",
            "--deep",
            "--strict",
            "--verbose=2",
            str(app_path),
        ],
        f"signature check for {app_path}",
    )
    minimum_macos = executable_minimum_macos(executable)
    return bundle_version, minimum_macos


def read_required_text(path: Path, label: str) -> str:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        fail(f"cannot read packaged {label} {path}: {exc}")
    if not text.strip():
        fail(f"packaged {label} is empty: {path}")
    return text


def require_text_markers(text: str, markers: tuple[str, ...], label: str) -> None:
    missing = [marker for marker in markers if marker not in text]
    if missing:
        fail(f"packaged {label} is missing required text: {missing}")


def verify_readme(
    package_root: Path,
    release_version: str,
    minimum_macos: str,
) -> None:
    text = read_required_text(package_root / "README.txt", "README.txt")
    require_text_markers(
        text,
        (
            f"Version: {release_version}",
            f"Minimum macOS: {minimum_macos}",
            "Apple silicon",
            "arm64",
            INSTALLER_NAME,
            f"~/Applications/{APP_NAME}",
            "System Settings > Privacy & Security",
            "Open Anyway",
            "not Apple notarized",
            "com.apple.quarantine",
            "sudo",
            "--dry-run",
            "Safe Mute",
            "AUDIO ON",
            ".nimgesture",
            "https://s3g.github.io/s3g-dsp/no-input-mixer-standalone.html",
        ),
        "README.txt",
    )


def parse_provenance(path: Path) -> dict[str, str]:
    text = read_required_text(path, "build provenance")
    values: dict[str, str] = {}
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        if ":" not in line:
            fail(f"{path}:{line_number}: expected Key: value provenance record")
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        if not key or not value:
            fail(f"{path}:{line_number}: empty provenance key or value")
        if key in values:
            fail(f"{path}:{line_number}: duplicate provenance key {key!r}")
        values[key] = value
    return values


def verify_provenance(
    package_root: Path,
    release_version: str,
    bundle_version: str,
    minimum_macos: str,
) -> None:
    values = parse_provenance(package_root / PROVENANCE_RELATIVE_PATH)
    expected = {
        "s3g No Input Mixer app package version": release_version,
        "CMake configuration": "Release",
        "CMake project version": bundle_version,
        "Application bundle version": bundle_version,
        "Bundle identifier": APP_BUNDLE_ID,
        "Architecture": "arm64",
        "Minimum macOS": minimum_macos,
    }
    for key, expected_value in expected.items():
        actual = values.get(key)
        if actual != expected_value:
            fail(
                f"build provenance {key!r} is {actual!r}, expected "
                f"{expected_value!r}"
            )

    artifact = values.get("Artifact tree", "")
    artifact_path = PurePosixPath(artifact)
    expected_suffix = (
        "apps",
        "no_input_mixer_standalone",
        APP_NAME,
    )
    if (
        artifact_path.is_absolute()
        or len(artifact_path.parts) != len(expected_suffix) + 1
        or artifact_path.parts[-len(expected_suffix) :] != expected_suffix
        or artifact_path.parts[0] in {"", ".", ".."}
    ):
        fail(f"build provenance has an invalid Artifact tree: {artifact!r}")

    revision = values.get("Source revision", "")
    if not GIT_REVISION_RE.fullmatch(revision):
        fail(f"build provenance has an invalid Source revision: {revision!r}")
    source_status = values.get("Source status")
    if source_status not in {
        "clean",
        "dirty (explicitly allowed for non-final testing)",
    }:
        fail(f"build provenance has an invalid Source status: {source_status!r}")
    if not values.get("Code-signing identity"):
        fail("build provenance has no Code-signing identity")


def verify_release_files(package_root: Path, release_version: str) -> None:
    license_text = read_required_text(package_root / "LICENSE.txt", "license")
    if "BSD 3-Clause License" not in license_text:
        fail("packaged LICENSE.txt does not identify the BSD 3-Clause License")

    notices = read_required_text(
        package_root / "THIRD_PARTY_NOTICES.md", "third-party notices"
    )
    if "Third-Party" not in notices and "Third Party" not in notices:
        fail("packaged THIRD_PARTY_NOTICES.md has no third-party notice heading")

    release_notes = read_required_text(
        package_root / "RELEASE_NOTES.md", "release notes"
    )
    require_text_markers(
        release_notes,
        (
            f"# s3g No Input Mixer {release_version}",
            f"{PACKAGE_PREFIX}{release_version}.zip",
        ),
        "RELEASE_NOTES.md",
    )


def installer_dry_run(package_root: Path, temporary_root: Path) -> None:
    installer = package_root / INSTALLER_NAME
    if not installer.is_file() or installer.is_symlink() or not os.access(
        installer, os.X_OK
    ):
        fail(f"packaged installer is missing or non-executable: {installer}")

    test_home = temporary_root / "home"
    destination = test_home / "Applications" / APP_NAME
    backup_root = (
        test_home
        / "Library"
        / "Application Support"
        / "s3g-dsp"
        / "App Backups"
    )
    test_home.mkdir(parents=True)
    environment = os.environ.copy()
    environment.update(
        {
            "HOME": str(test_home),
            "S3G_NIM_APP_DESTINATION": str(destination),
            "S3G_NIM_APP_BACKUP_ROOT": str(backup_root),
            "S3G_NIM_APP_TEST_RUNNING": "0",
        }
    )
    try:
        result = subprocess.run(
            [str(installer), "--dry-run"],
            cwd=package_root,
            env=environment,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as exc:
        fail(f"cannot run packaged installer dry-run: {exc}")
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        fail(f"packaged installer dry-run failed: {detail}")
    output = f"{result.stdout}\n{result.stderr}"
    if "dry run" not in output.casefold():
        fail("packaged installer did not identify its dry-run mode")
    if str(destination) not in output:
        fail("packaged installer dry-run did not report its isolated destination")
    if destination.exists() or destination.is_symlink():
        fail("packaged installer dry-run unexpectedly created its destination")
    if backup_root.exists() or backup_root.is_symlink():
        fail("packaged installer dry-run unexpectedly created its backup root")
    if any(test_home.iterdir()):
        fail("packaged installer dry-run unexpectedly wrote inside its temporary HOME")


def verify_package(args: argparse.Namespace) -> int:
    package_path = args.package.expanduser().absolute()
    with tempfile.TemporaryDirectory(prefix="s3g-nim-app-package-") as temporary:
        temporary_root = Path(temporary)
        package_root, release_version = prepare_package(
            package_path,
            temporary_root / "archive",
            args.release_version,
        )
        validate_tree_entries(package_root)
        require_exact_layout(package_root)
        bundle_version, minimum_macos = verify_app(package_root, release_version)
        verify_readme(package_root, release_version, minimum_macos)
        verify_provenance(
            package_root,
            release_version,
            bundle_version,
            minimum_macos,
        )
        verify_release_files(package_root, release_version)
        installer_dry_run(package_root, temporary_root / "installer")

    print(
        "macOS No Input Mixer app package verification passed: "
        f"{release_version}; app {bundle_version}; arm64; macOS {minimum_macos}+"
    )
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="staging directory or .zip archive")
    parser.add_argument(
        "--release-version",
        help=(
            "expected package release label; by default it is read from the "
            f"{PACKAGE_PREFIX}<version> root name"
        ),
    )
    args = parser.parse_args()
    if args.release_version is not None and not safe_release_version(
        args.release_version
    ):
        parser.error("--release-version contains unsafe characters")
    return args


def main() -> int:
    args = parse_args()
    try:
        return verify_package(args)
    except VerificationError as exc:
        print(
            f"macOS No Input Mixer app package verification failed: {exc}",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
