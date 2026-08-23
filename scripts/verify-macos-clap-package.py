#!/usr/bin/env python3

"""Verify a staged or zipped s3g-dsp macOS CLAP release package."""

from __future__ import annotations

import argparse
import csv
import ctypes
from dataclasses import dataclass
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import re
import stat
import subprocess
import sys
import tempfile
import zipfile


EXPECTED_BUNDLE_COUNT = 128
EXPECTED_DESCRIPTOR_COUNT = 132
INSTALLER_NAME = "Install s3g-dsp CLAPs.command"
MANIFEST_RELATIVE_PATH = Path("Installer Data") / "clap-bundles.tsv"
LEGACY_MANIFEST_RELATIVE_PATH = Path("Installer Data") / "clap-legacy-bundles.tsv"
SAFE_VERSION_RE = re.compile(r"^[0-9A-Za-z][0-9A-Za-z.+_-]*$")


@dataclass(frozen=True)
class Bundle:
    build_path: str
    installed_name: str
    plugin_id: str
    host_name: str


class VerificationError(RuntimeError):
    pass


class ClapVersion(ctypes.Structure):
    _fields_ = [
        ("major", ctypes.c_uint32),
        ("minor", ctypes.c_uint32),
        ("revision", ctypes.c_uint32),
    ]


class ClapPluginDescriptor(ctypes.Structure):
    _fields_ = [
        ("clap_version", ClapVersion),
        ("id", ctypes.c_char_p),
        ("name", ctypes.c_char_p),
        ("vendor", ctypes.c_char_p),
        ("url", ctypes.c_char_p),
        ("manual_url", ctypes.c_char_p),
        ("support_url", ctypes.c_char_p),
        ("version", ctypes.c_char_p),
        ("description", ctypes.c_char_p),
        ("features", ctypes.POINTER(ctypes.c_char_p)),
    ]


EntryInit = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_char_p)
EntryDeinit = ctypes.CFUNCTYPE(None)
EntryGetFactory = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_char_p)


class ClapPluginEntry(ctypes.Structure):
    _fields_ = [
        ("clap_version", ClapVersion),
        ("init", EntryInit),
        ("deinit", EntryDeinit),
        ("get_factory", EntryGetFactory),
    ]


FactoryGetCount = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p)
FactoryGetDescriptor = ctypes.CFUNCTYPE(
    ctypes.POINTER(ClapPluginDescriptor), ctypes.c_void_p, ctypes.c_uint32
)
FactoryCreatePlugin = ctypes.CFUNCTYPE(
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p
)


class ClapPluginFactory(ctypes.Structure):
    _fields_ = [
        ("get_plugin_count", FactoryGetCount),
        ("get_plugin_descriptor", FactoryGetDescriptor),
        ("create_plugin", FactoryCreatePlugin),
    ]


def fail(message: str) -> None:
    raise VerificationError(message)


def decode(value: bytes | None, label: str) -> str:
    if value is None:
        fail(f"CLAP descriptor has no {label}")
    try:
        return value.decode("utf-8")
    except UnicodeDecodeError as exc:
        fail(f"CLAP descriptor {label} is not UTF-8: {exc}")


def inspect_descriptors(binary: Path) -> list[dict[str, str]]:
    """Load one binary and return every descriptor published by its factory."""

    try:
        library = ctypes.CDLL(str(binary), mode=ctypes.RTLD_LOCAL)
        entry = ClapPluginEntry.in_dll(library, "clap_entry")
    except (OSError, ValueError) as exc:
        fail(f"cannot load CLAP entry from {binary}: {exc}")

    initialized = False
    try:
        if not entry.init(str(binary).encode("utf-8")):
            fail(f"CLAP entry initialization failed: {binary}")
        initialized = True
        factory_address = entry.get_factory(b"clap.plugin-factory")
        if not factory_address:
            fail(f"CLAP plugin factory is missing: {binary}")
        factory = ctypes.cast(
            factory_address, ctypes.POINTER(ClapPluginFactory)
        ).contents
        count = factory.get_plugin_count(factory_address)
        if count < 1:
            fail(f"CLAP plugin factory is empty: {binary}")
        descriptors: list[dict[str, str]] = []
        seen_ids: set[str] = set()
        for index in range(count):
            descriptor_pointer = factory.get_plugin_descriptor(factory_address, index)
            if not descriptor_pointer:
                fail(f"CLAP factory returned a null descriptor at index {index}: {binary}")
            descriptor = descriptor_pointer.contents
            plugin_id = decode(descriptor.id, "ID")
            name = decode(descriptor.name, "name")
            version = decode(descriptor.version, "version")
            if not plugin_id or not name or not version:
                fail(f"CLAP factory returned an empty descriptor field at index {index}: {binary}")
            if not SAFE_VERSION_RE.fullmatch(version):
                fail(f"CLAP descriptor {plugin_id!r} has unsafe version {version!r}: {binary}")
            if plugin_id in seen_ids:
                fail(f"CLAP factory returned duplicate descriptor ID {plugin_id!r}: {binary}")
            seen_ids.add(plugin_id)
            descriptors.append({"id": plugin_id, "name": name, "version": version})
        return descriptors
    finally:
        if initialized:
            entry.deinit()


def descriptor_by_id(
    descriptors: list[dict[str, str]], expected_id: str, binary: Path
) -> dict[str, str]:
    for descriptor in descriptors:
        if descriptor.get("id") == expected_id:
            return descriptor
    fail(f"CLAP descriptor {expected_id!r} is absent from {binary}")


def inspect_descriptor(binary: Path, expected_id: str) -> dict[str, str]:
    """Load one binary and return its matching CLAP descriptor metadata."""

    return descriptor_by_id(inspect_descriptors(binary), expected_id, binary)


def inspect_descriptors_subprocess(binary: Path) -> list[dict[str, str]]:
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        "--inspect-descriptors",
        str(binary),
    ]
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "unknown loader error"
        fail(f"cannot inspect CLAP descriptor in {binary}: {detail}")
    try:
        value = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        fail(f"descriptor inspector returned invalid JSON for {binary}: {exc}")
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        fail(f"descriptor inspector returned invalid metadata for {binary}")
    return [
        {str(key): str(item) for key, item in descriptor.items()}
        for descriptor in value
    ]


def inspect_descriptor_subprocess(binary: Path, expected_id: str) -> dict[str, str]:
    return descriptor_by_id(inspect_descriptors_subprocess(binary), expected_id, binary)


def validate_descriptor_inventory(
    inventory: list[tuple[str, dict[str, str]]], expected_count: int
) -> None:
    if len(inventory) != expected_count:
        fail(
            f"package exposes {len(inventory)} runtime CLAP descriptors, "
            f"expected {expected_count}"
        )
    owners_by_id: dict[str, str] = {}
    for owner, descriptor in inventory:
        plugin_id = descriptor.get("id", "")
        name = descriptor.get("name", "")
        version = descriptor.get("version", "")
        if not plugin_id or not name or not version:
            fail(f"{owner}: runtime CLAP descriptor has an empty ID, name, or version")
        if not SAFE_VERSION_RE.fullmatch(version):
            fail(f"{owner}: runtime CLAP descriptor {plugin_id!r} has unsafe version {version!r}")
        prior_owner = owners_by_id.get(plugin_id)
        if prior_owner is not None:
            fail(
                f"runtime CLAP descriptor ID {plugin_id!r} is duplicated in "
                f"{prior_owner} and {owner}"
            )
        owners_by_id[plugin_id] = owner


def safe_bundle_filename(value: str) -> bool:
    path = PurePosixPath(value)
    return (
        path.name == value
        and value.startswith("s3g_")
        and value.endswith(".clap")
        and all(
            character.isascii()
            and (character.islower() or character.isdigit() or character in "_.")
            for character in value
        )
    )


def read_manifest(path: Path) -> list[Bundle]:
    try:
        stream = path.open("r", encoding="utf-8", newline="")
    except OSError as exc:
        fail(f"cannot read package manifest {path}: {exc}")
    bundles: list[Bundle] = []
    with stream:
        reader = csv.reader(stream, delimiter="\t", strict=True)
        try:
            for row in reader:
                if not row or row[0].lstrip().startswith("#"):
                    continue
                if len(row) != 4 or any(not value or value != value.strip() for value in row):
                    fail(f"{path}:{reader.line_num}: expected four non-empty trimmed columns")
                bundle = Bundle(*row)
                if not safe_bundle_filename(bundle.installed_name):
                    fail(f"{path}:{reader.line_num}: unsafe installed name {bundle.installed_name!r}")
                if not bundle.plugin_id.startswith("org.s3g.s3g-dsp."):
                    fail(f"{path}:{reader.line_num}: unexpected plugin ID {bundle.plugin_id!r}")
                bundles.append(bundle)
        except csv.Error as exc:
            fail(f"invalid package manifest {path}: {exc}")
    if not bundles:
        fail(f"package manifest is empty: {path}")
    for label, values in (
        ("installed name", [bundle.installed_name.casefold() for bundle in bundles]),
        ("plugin ID", [bundle.plugin_id.casefold() for bundle in bundles]),
        ("host name", [bundle.host_name.casefold() for bundle in bundles]),
    ):
        if len(values) != len(set(values)):
            fail(f"package manifest contains a duplicate {label}")
    return bundles


def validate_zip_members(path: Path) -> str:
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        fail(f"cannot open package archive {path}: {exc}")
    roots: set[str] = set()
    with archive:
        for member in archive.infolist():
            pure = PurePosixPath(member.filename)
            if pure.is_absolute() or not pure.parts or ".." in pure.parts:
                fail(f"unsafe archive member path: {member.filename!r}")
            mode = member.external_attr >> 16
            if stat.S_ISLNK(mode):
                fail(f"package archive contains a symlink: {member.filename!r}")
            roots.add(pure.parts[0])
    if len(roots) != 1:
        fail(f"package archive must contain exactly one root directory, found {sorted(roots)}")
    return next(iter(roots))


def prepare_package(path: Path, temporary_root: Path) -> Path:
    if path.is_dir():
        return path.resolve()
    if not path.is_file() or path.suffix.casefold() != ".zip":
        fail(f"package must be a staging directory or .zip archive: {path}")
    root_name = validate_zip_members(path)
    ditto = Path("/usr/bin/ditto")
    if not ditto.is_file():
        fail("/usr/bin/ditto is required to preserve package permissions while extracting")
    result = subprocess.run(
        [str(ditto), "-x", "-k", str(path), str(temporary_root)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(f"cannot extract package archive: {result.stderr.strip()}")
    package_root = temporary_root / root_name
    if not package_root.is_dir():
        fail(f"archive root was not extracted as a directory: {package_root}")
    return package_root


def plist_string(metadata: dict[object, object], key: str, bundle: Path) -> str:
    value = metadata.get(key)
    if not isinstance(value, str) or not value:
        fail(f"{bundle}: Info.plist has no valid {key}")
    return value


def run_checked(command: list[str], label: str) -> str:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        fail(f"{label} failed: {detail}")
    return result.stdout


def verify_bundle(
    package_root: Path,
    bundle: Bundle,
    *,
    check_architecture: bool,
    check_signature: bool,
    check_descriptor: bool,
) -> tuple[str, list[dict[str, str]]]:
    bundle_path = package_root / bundle.installed_name
    if not bundle_path.is_dir() or bundle_path.is_symlink():
        fail(f"missing or unsafe packaged bundle: {bundle_path}")
    plist_path = bundle_path / "Contents" / "Info.plist"
    if plist_path.is_symlink():
        fail(f"packaged bundle has a symlinked Info.plist: {plist_path}")
    try:
        with plist_path.open("rb") as stream:
            metadata = plistlib.load(stream)
    except (OSError, plistlib.InvalidFileException) as exc:
        fail(f"cannot read {plist_path}: {exc}")

    actual_id = plist_string(metadata, "CFBundleIdentifier", bundle_path)
    actual_name = plist_string(metadata, "CFBundleName", bundle_path)
    executable_name = plist_string(metadata, "CFBundleExecutable", bundle_path)
    short_version = plist_string(metadata, "CFBundleShortVersionString", bundle_path)
    bundle_version = plist_string(metadata, "CFBundleVersion", bundle_path)
    if actual_id != bundle.plugin_id:
        fail(f"{bundle_path}: CFBundleIdentifier {actual_id!r} != {bundle.plugin_id!r}")
    if actual_name != bundle.host_name:
        fail(f"{bundle_path}: CFBundleName {actual_name!r} != {bundle.host_name!r}")
    if PurePosixPath(executable_name).name != executable_name:
        fail(f"{bundle_path}: unsafe CFBundleExecutable {executable_name!r}")
    if short_version != bundle_version:
        fail(
            f"{bundle_path}: CFBundleShortVersionString {short_version!r} "
            f"!= CFBundleVersion {bundle_version!r}"
        )
    executable = bundle_path / "Contents" / "MacOS" / executable_name
    if executable.is_symlink() or not executable.is_file() or not os.access(executable, os.X_OK):
        fail(f"missing or non-executable packaged binary: {executable}")

    if check_architecture:
        architectures = run_checked(
            ["/usr/bin/lipo", "-archs", str(executable)], f"architecture check for {bundle_path}"
        ).strip()
        if architectures != "arm64":
            fail(f"{bundle_path}: expected arm64-only binary, found {architectures!r}")
    if check_signature:
        run_checked(
            ["/usr/bin/codesign", "--verify", "--deep", "--strict", str(bundle_path)],
            f"signature check for {bundle_path}",
        )
    descriptors: list[dict[str, str]] = []
    if check_descriptor:
        descriptors = inspect_descriptors_subprocess(executable)
        descriptor = descriptor_by_id(descriptors, bundle.plugin_id, executable)
        if descriptor.get("id") != bundle.plugin_id:
            fail(f"{bundle_path}: CLAP descriptor ID does not match the manifest")
        if descriptor.get("name") != bundle.host_name:
            fail(
                f"{bundle_path}: CLAP descriptor name {descriptor.get('name')!r} "
                f"!= {bundle.host_name!r}"
            )
        if descriptor.get("version") != short_version:
            fail(
                f"{bundle_path}: CLAP descriptor version {descriptor.get('version')!r} "
                f"!= Info.plist version {short_version!r}"
            )
    return short_version, descriptors


def synchronize_bundle_version(
    package_root: Path, bundle: Bundle
) -> tuple[str, list[dict[str, str]]]:
    """Set unsigned staged Info.plist versions from the runtime descriptor."""

    bundle_path = package_root / bundle.installed_name
    if not bundle_path.is_dir() or bundle_path.is_symlink():
        fail(f"missing or unsafe staged bundle: {bundle_path}")
    plist_path = bundle_path / "Contents" / "Info.plist"
    if plist_path.is_symlink():
        fail(f"staged bundle has a symlinked Info.plist: {plist_path}")
    try:
        with plist_path.open("rb") as stream:
            metadata = plistlib.load(stream)
    except (OSError, plistlib.InvalidFileException) as exc:
        fail(f"cannot read {plist_path}: {exc}")

    actual_id = plist_string(metadata, "CFBundleIdentifier", bundle_path)
    actual_name = plist_string(metadata, "CFBundleName", bundle_path)
    executable_name = plist_string(metadata, "CFBundleExecutable", bundle_path)
    if actual_id != bundle.plugin_id or actual_name != bundle.host_name:
        fail(f"refusing to modify staged metadata with an unexpected ID or name: {bundle_path}")
    if PurePosixPath(executable_name).name != executable_name:
        fail(f"{bundle_path}: unsafe CFBundleExecutable {executable_name!r}")
    executable = bundle_path / "Contents" / "MacOS" / executable_name
    if executable.is_symlink() or not executable.is_file() or not os.access(executable, os.X_OK):
        fail(f"missing or non-executable staged binary: {executable}")

    descriptors = inspect_descriptors_subprocess(executable)
    descriptor = descriptor_by_id(descriptors, bundle.plugin_id, executable)
    if descriptor.get("id") != bundle.plugin_id or descriptor.get("name") != bundle.host_name:
        fail(f"refusing to modify staged metadata for a mismatched descriptor: {bundle_path}")
    descriptor_version = descriptor.get("version", "")
    if not SAFE_VERSION_RE.fullmatch(descriptor_version):
        fail(f"{bundle_path}: unsafe or empty CLAP descriptor version {descriptor_version!r}")

    metadata["CFBundleShortVersionString"] = descriptor_version
    metadata["CFBundleVersion"] = descriptor_version
    temporary_plist = plist_path.with_name(f".{plist_path.name}.s3g-package-tmp")
    try:
        with temporary_plist.open("wb") as stream:
            plistlib.dump(metadata, stream, fmt=plistlib.FMT_XML, sort_keys=False)
        os.replace(temporary_plist, plist_path)
    except OSError as exc:
        try:
            temporary_plist.unlink(missing_ok=True)
        except OSError:
            pass
        fail(f"cannot update staged version metadata in {plist_path}: {exc}")
    return descriptor_version, descriptors


def validate_expected_bundle_set(package_root: Path, bundles: list[Bundle]) -> None:
    expected_names = {bundle.installed_name for bundle in bundles}
    actual_names = {
        child.name
        for child in package_root.iterdir()
        if child.name.endswith(".clap")
    }
    if actual_names != expected_names:
        missing = sorted(expected_names - actual_names)
        extra = sorted(actual_names - expected_names)
        fail(f"packaged bundle set differs from manifest; missing={missing}, extra={extra}")


def compare_reference_file(packaged_path: Path, reference_path: Path | None, label: str) -> None:
    if reference_path is None:
        return
    reference = reference_path.expanduser().resolve()
    try:
        if packaged_path.read_bytes() != reference.read_bytes():
            fail(f"packaged {label} differs from reference {reference}")
    except OSError as exc:
        fail(f"cannot compare reference {label} {reference}: {exc}")


def compare_reference_manifests(package_root: Path, args: argparse.Namespace) -> None:
    compare_reference_file(
        package_root / MANIFEST_RELATIVE_PATH,
        args.reference_manifest,
        "active manifest",
    )
    compare_reference_file(
        package_root / LEGACY_MANIFEST_RELATIVE_PATH,
        args.reference_legacy_manifest,
        "legacy manifest",
    )


def fix_bundle_metadata(args: argparse.Namespace) -> int:
    package_root = args.package.expanduser().resolve()
    if not package_root.is_dir():
        fail("--fix-bundle-metadata accepts an unpacked staging directory only")
    manifest_path = package_root / MANIFEST_RELATIVE_PATH
    bundles = read_manifest(manifest_path)
    if len(bundles) != args.expected_count:
        fail(f"staging directory contains {len(bundles)} manifest IDs, expected {args.expected_count}")
    compare_reference_manifests(package_root, args)
    validate_expected_bundle_set(package_root, bundles)
    versions: dict[str, int] = {}
    descriptor_inventory: list[tuple[str, dict[str, str]]] = []
    for bundle in bundles:
        version, descriptors = synchronize_bundle_version(package_root, bundle)
        versions[version] = versions.get(version, 0) + 1
        descriptor_inventory.extend((bundle.installed_name, descriptor) for descriptor in descriptors)
    validate_descriptor_inventory(descriptor_inventory, args.expected_descriptor_count)
    summary = ", ".join(f"{version}={count}" for version, count in sorted(versions.items()))
    print(
        f"synchronized {len(bundles)} staged bundle versions; verified "
        f"{len(descriptor_inventory)} runtime CLAP descriptors: {summary}"
    )
    return 0


def verify_readme(package_root: Path, release_version: str | None, bundle_count: int) -> None:
    readme = package_root / "README.txt"
    try:
        text = readme.read_text(encoding="utf-8")
    except OSError as exc:
        fail(f"cannot read packaged README.txt: {exc}")
    if release_version is not None and f"Version: {release_version}\n" not in text:
        fail(f"packaged README.txt does not declare Version: {release_version}")
    if f"Included plugins ({bundle_count} bundles):" not in text:
        fail(f"packaged README.txt does not declare the {bundle_count}-bundle active set")


def installer_dry_run(package_root: Path, bundle_count: int, temporary_root: Path) -> None:
    installer = package_root / INSTALLER_NAME
    if not installer.is_file() or not os.access(installer, os.X_OK):
        fail(f"packaged installer is missing or non-executable: {installer}")
    test_home = temporary_root / "home"
    destination = temporary_root / "install" / "s3g-dsp"
    backup = temporary_root / "backups"
    receipt = temporary_root / "receipt.tsv"
    test_home.mkdir(parents=True)
    environment = os.environ.copy()
    environment.update(
        {
            "HOME": str(test_home),
            "S3G_CLAP_DESTINATION": str(destination),
            "S3G_CLAP_BACKUP_ROOT": str(backup),
            "S3G_CLAP_RECEIPT": str(receipt),
        }
    )
    result = subprocess.run(
        [str(installer), "--dry-run"],
        cwd=package_root,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        fail(f"packaged installer dry-run failed: {detail}")
    expected_summary = f"Summary: installed={bundle_count} renamed=0 backed-up=0 warnings=0"
    if f"Active set:  {bundle_count} bundles" not in result.stdout:
        fail("packaged installer dry-run did not report the expected active-set size")
    if expected_summary not in result.stdout:
        fail(f"packaged installer dry-run did not report {expected_summary!r}")


def verify_package(args: argparse.Namespace) -> int:
    package_path = args.package.expanduser().resolve()
    with tempfile.TemporaryDirectory(prefix="s3g-clap-package-") as temporary:
        temporary_root = Path(temporary)
        package_root = prepare_package(package_path, temporary_root / "archive")
        manifest_path = package_root / MANIFEST_RELATIVE_PATH
        bundles = read_manifest(manifest_path)
        if len(bundles) != args.expected_count:
            fail(f"package contains {len(bundles)} manifest IDs, expected {args.expected_count}")

        compare_reference_manifests(package_root, args)
        validate_expected_bundle_set(package_root, bundles)

        versions: dict[str, int] = {}
        descriptor_inventory: list[tuple[str, dict[str, str]]] = []
        for bundle in bundles:
            version, descriptors = verify_bundle(
                package_root,
                bundle,
                check_architecture=not args.skip_architecture,
                check_signature=not args.skip_signature,
                check_descriptor=not args.skip_descriptor,
            )
            versions[version] = versions.get(version, 0) + 1
            descriptor_inventory.extend(
                (bundle.installed_name, descriptor) for descriptor in descriptors
            )

        if not args.skip_descriptor:
            validate_descriptor_inventory(
                descriptor_inventory, args.expected_descriptor_count
            )

        verify_readme(package_root, args.release_version, len(bundles))
        if not args.skip_installer_dry_run:
            installer_dry_run(package_root, len(bundles), temporary_root / "installer")

    version_summary = ", ".join(
        f"{version}={count}" for version, count in sorted(versions.items())
    )
    architecture_label = "arm64 " if not args.skip_architecture else ""
    version_label = "descriptor/Info" if not args.skip_descriptor else "Info.plist"
    print(
        f"macOS CLAP package verification passed: {len(bundles)} "
        f"{architecture_label}bundles"
        + (
            f", {len(descriptor_inventory)} runtime descriptors"
            if not args.skip_descriptor
            else ""
        )
        + f"; {version_label} versions: {version_summary}"
    )
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", nargs="?", type=Path, help="staging directory or .zip archive")
    parser.add_argument("--expected-count", type=int, default=EXPECTED_BUNDLE_COUNT)
    parser.add_argument(
        "--expected-descriptor-count", type=int, default=EXPECTED_DESCRIPTOR_COUNT
    )
    parser.add_argument("--release-version")
    parser.add_argument("--reference-manifest", type=Path)
    parser.add_argument("--reference-legacy-manifest", type=Path)
    parser.add_argument("--skip-architecture", action="store_true")
    parser.add_argument("--skip-signature", action="store_true")
    parser.add_argument("--skip-descriptor", action="store_true")
    parser.add_argument("--skip-installer-dry-run", action="store_true")
    parser.add_argument(
        "--fix-bundle-metadata",
        action="store_true",
        help=(
            "synchronize unsigned staged Info.plist short/build versions from each "
            "runtime CLAP descriptor, then exit"
        ),
    )
    parser.add_argument(
        "--inspect-descriptor",
        nargs=2,
        metavar=("BINARY", "PLUGIN_ID"),
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--inspect-descriptors",
        nargs=1,
        metavar=("BINARY",),
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args()
    if (
        args.inspect_descriptor is None
        and args.inspect_descriptors is None
        and args.package is None
    ):
        parser.error("package is required")
    if args.expected_count < 1:
        parser.error("--expected-count must be positive")
    if args.expected_descriptor_count < 1:
        parser.error("--expected-descriptor-count must be positive")
    return args


def main() -> int:
    args = parse_args()
    try:
        if args.inspect_descriptor is not None:
            binary, plugin_id = args.inspect_descriptor
            print(json.dumps(inspect_descriptor(Path(binary), plugin_id), sort_keys=True))
            return 0
        if args.inspect_descriptors is not None:
            (binary,) = args.inspect_descriptors
            print(json.dumps(inspect_descriptors(Path(binary)), sort_keys=True))
            return 0
        if args.fix_bundle_metadata:
            return fix_bundle_metadata(args)
        return verify_package(args)
    except VerificationError as exc:
        print(f"macOS CLAP package verification failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
