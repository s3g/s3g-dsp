#!/usr/bin/env python3
"""Build and package the experimental Windows x64 suite on native MSVC.

Requires a configured clap-windows-release tree and Python 3.10+. Never installs,
publishes, includes private notes, or replaces an existing archive. The older
MinGW test packager remains available separately.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.10.0-pre"
EXCLUDED = "s3g_analyzer_ambi_energy_64.clap"
EXPECTED_FILES = 121
EXPECTED_DESCRIPTORS = 128
SPEC = importlib.util.spec_from_file_location(
    "s3g_package_verifier", ROOT / "scripts/verify-macos-clap-package.py")
VERIFIER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VERIFIER
SPEC.loader.exec_module(VERIFIER)


def run(*args):
    return subprocess.check_output([str(arg) for arg in args], cwd=ROOT,
                                   text=True, encoding="utf-8", errors="replace")


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def read_cache(build):
    return {match[1]: match[2] for line in (build / "CMakeCache.txt").read_text(
        encoding="utf-8").splitlines()
        if (match := re.match(r"^([^/#][^:]*):[^=]+=(.*)$", line))}


def validate_cache(cache, version):
    if not cache.get("CMAKE_HOME_DIRECTORY") or Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != ROOT:
        raise ValueError("Build directory belongs to a different source tree")
    if not cache.get("CMAKE_GENERATOR", "").startswith("Visual Studio "):
        raise ValueError("Use a native Visual Studio clap-windows-release build")
    if cache.get("CMAKE_GENERATOR_PLATFORM") != "x64":
        raise ValueError("An x64 build is required")
    if "Release" not in cache.get("CMAKE_CONFIGURATION_TYPES", "").split(";"):
        raise ValueError("Release configuration is missing")
    if cache.get("CMAKE_PROJECT_VERSION") != version.split("-", 1)[0]:
        raise ValueError("Suite version differs from the configured project version")
    if cache.get("CMAKE_MSVC_RUNTIME_LIBRARY") != "MultiThreaded$<$<CONFIG:Debug>:Debug>":
        raise ValueError("Use the release preset's static MSVC runtime")
    for key in ("S3G_BUILD_CLAP_PLUGIN", "S3G_BUILD_FUTURE_COMPONENTS",
                "S3G_BUILD_TRACKER_PREVIEW", "S3G_BUILD_RELAY_PREVIEW",
                "S3G_BUILD_BREAKBEAT_SLICER_PREVIEW", "S3G_ENABLE_WORLD",
                "S3G_ENABLE_PORTABLE_CLAP_GUI", "BUILD_TESTING"):
        if cache.get(key) != "ON":
            raise ValueError(f"Packaging requires {key}=ON")


def windows_inventory(manifest):
    bundles = VERIFIER.read_manifest(manifest)
    for bundle in bundles:
        if not re.fullmatch(r"clap_[a-z0-9_]+/s3g_[a-z0-9_]+\.clap", bundle.build_path):
            raise ValueError(f"Unsafe manifest build path: {bundle.build_path}")
    selected = [item for item in bundles if item.installed_name != EXCLUDED]
    if len(bundles) - len(selected) != 1 or len(selected) != EXPECTED_FILES:
        raise ValueError("Expected 121 Windows files and exactly one Ambi Energy exclusion")
    return selected


def release_targets(build):
    """Use CMake's file API, not compiler-specific link.txt or guessed targets."""
    reply = build / ".cmake/api/v1/reply"
    index = json.loads(max(reply.glob("index-*.json")).read_text(encoding="utf-8"))
    reference = index["reply"]["client-s3g-release"]["codemodel-v2"]["jsonFile"]
    model = json.loads((reply / reference).read_text(encoding="utf-8"))
    configurations = [item for item in model["configurations"] if item["name"] == "Release"]
    if len(configurations) != 1:
        raise ValueError("Expected one Release codemodel")
    return [json.loads((reply / item["jsonFile"]).read_text(encoding="utf-8"))
            for item in configurations[0]["targets"]]


def resolve_artifact(build, targets, relative):
    path = PurePosixPath(relative)
    expected = (build / "plugins" / path.parent / "Release" / path.name).resolve()
    matches = [(target["name"], expected) for target in targets
               if target.get("type") in ("MODULE_LIBRARY", "SHARED_LIBRARY", "EXECUTABLE")
               for artifact in target.get("artifacts", [])
               if (build / artifact["path"]).resolve() == expected]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one configured Release target for {relative}")
    return matches[0]


def regular_file(path, root):
    if not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"Missing file or escaped source root: {path}")
    for part in (path, *path.parents):
        if part.is_symlink():
            raise ValueError(f"Refusing symlink: {path}")
        if part == root:
            break
    return path


def merge_tree(source, destination):
    if not source.is_dir() or source.is_symlink():
        raise ValueError(f"Missing or symlinked resource directory: {source}")
    # Reject directory links too, including links not followed by rglob.
    for item in sorted(source.rglob("*")):
        if item.is_symlink():
            raise ValueError(f"Refusing resource symlink: {item}")
        if not item.is_file():
            continue
        regular_file(item, source)
        output = destination / item.relative_to(source)
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            if digest(item) != digest(output):
                raise ValueError(f"Conflicting shared resource: {output}")
        else:
            shutil.copy2(item, output)


def validate_pe(binary, dumpbin):
    data = binary.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"Not a PE binary: {binary}")
    offset = struct.unpack_from("<I", data, 60)[0]
    if offset + 6 > len(data) or data[offset:offset + 6] != b"PE\0\0\x64\x86":
        raise ValueError(f"Not a Windows x64 PE binary: {binary}")
    imports = run(dumpbin, "/nologo", "/dependents", binary)
    if re.search(r"\b(?:libgcc[^\s]*|libstdc\+\+[^\s]*|libwinpthread[^\s]*|"
                 r"vcruntime[^\s]*|msvcp\d[^\s]*|msvcr\d[^\s]*)\.dll\b",
                 imports, re.IGNORECASE):
        raise ValueError(f"Unpackaged compiler runtime dependency: {binary}\n{imports}")
    if binary.suffix == ".clap":
        exports = run(dumpbin, "/nologo", "/exports", binary)
        if not re.search(r"\bclap_entry\b", exports):
            raise ValueError(f"Missing CLAP export: {binary}")


def verify_payload(stage, bundles, dumpbin):
    actual = {path.name for path in stage.glob("*.clap")}
    if actual != {item.installed_name for item in bundles}:
        raise ValueError("Packaged CLAP inventory differs from the manifest")
    inventory = []
    for item in bundles:
        binary = stage / item.installed_name
        validate_pe(binary, dumpbin)
        result = subprocess.run([sys.executable, str(ROOT / "scripts/verify-macos-clap-package.py"),
                                 "--inspect-descriptors", str(binary)],
                                capture_output=True, text=True, encoding="utf-8", timeout=60)
        if result.returncode:
            raise ValueError(f"Cannot load packaged CLAP {binary}: {result.stderr}")
        descriptors = json.loads(result.stdout)
        primary = VERIFIER.descriptor_by_id(descriptors, item.plugin_id, binary)
        if primary["name"] != item.host_name:
            raise ValueError(f"Host name differs from the manifest: {binary}")
        inventory.extend((item.installed_name, descriptor) for descriptor in descriptors)
    VERIFIER.validate_descriptor_inventory(inventory, EXPECTED_DESCRIPTORS)
    for name in ("FiraCode-Regular.ttf", "FiraCode-LICENSE.txt", "IBMPlexMono-Regular.ttf",
                 "IBMPlexMono-Medium.ttf", "IBMPlexMono-SemiBold.ttf", "OFL.txt"):
        regular_file(stage / "Resources/Fonts" / name, stage)
    for name in ("VSTGUI-LICENSE.txt", "WORLD-LICENSE.txt", "RapidJSON-LICENSE.txt"):
        regular_file(stage / "Resources/Licenses" / name, stage)
    for folder, extension in (("Imprint Atlas", "s3gimprint"), ("Ray Atlas", "s3gray")):
        if len(list((stage / "Resources" / folder).glob(f"*.{extension}"))) != 19:
            raise ValueError(f"Expected 19 {folder} responses")
    return inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-clap-windows-release")
    parser.add_argument("--version", default=VERSION)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--allow-dirty", action="store_true",
                        help="Non-final rehearsal only; archive is marked rehearsal")
    args = parser.parse_args()
    if sys.platform != "win32" or struct.calcsize("P") != 8:
        parser.error("Run natively on Windows with 64-bit Python")
    if args.jobs < 1 or not re.fullmatch(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?", args.version):
        parser.error("Invalid job count or version")
    build = args.build_dir.resolve()
    status = run("git", "status", "--porcelain", "--untracked-files=all")
    revision = run("git", "rev-parse", "HEAD").strip()
    if status and not args.allow_dirty:
        raise ValueError("Commit/review release inputs first, or use --allow-dirty for rehearsal")
    name = f"s3g-dsp-windows-x64-clap-{args.version}-experimental"
    if args.allow_dirty:
        name += "-rehearsal"
    destination = ROOT / "dist" / name
    archive = Path(str(destination) + ".zip")
    checksum = Path(str(archive) + ".sha256")
    if any(path.exists() for path in (destination, archive, checksum)):
        raise ValueError("Refusing to replace an existing package; preserve it before rebuilding")
    validate_cache(read_cache(build), args.version)
    query = build / ".cmake/api/v1/query/client-s3g-release"
    query.mkdir(parents=True, exist_ok=True)
    (query / "codemodel-v2").touch()
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(build)], cwd=ROOT, check=True)
    cache = read_cache(build)
    validate_cache(cache, args.version)
    dumpbin = Path(cache["CMAKE_LINKER"]).with_name("dumpbin.exe")
    if not dumpbin.is_file():
        raise ValueError("MSVC dumpbin.exe not found beside CMAKE_LINKER")
    bundles = windows_inventory(ROOT / "scripts/clap-bundles.tsv")
    targets = release_targets(build)
    artifacts = [resolve_artifact(build, targets, item.build_path) for item in bundles]
    checker = resolve_artifact(build, targets, "clap_tracker/s3g_tracker_windows_clap_smoke.exe")
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel",
                    str(args.jobs), "--target", *[target for target, _ in artifacts], checker[0]],
                   cwd=ROOT, check=True)
    destination.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".windows-release-", dir=destination.parent) as temporary:
        stage = Path(temporary) / name
        stage.mkdir()
        for item, (_, source) in zip(bundles, artifacts):
            shutil.copy2(regular_file(source, build), stage / item.installed_name)
            for font in ("FiraCode-Regular.ttf", "FiraCode-LICENSE.txt"):
                regular_file(source.parent / "Resources/Fonts" / font, build)
            merge_tree(source.parent / "Resources", stage / "Resources")
        validate_pe(regular_file(checker[1], build), dumpbin)
        shutil.copy2(checker[1], stage / checker[1].name)
        for source, output in (
            ("LICENSE", "LICENSE.txt"), ("tracker/LICENSE", "TRACKER-LICENSE.txt"),
            ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
            ("RELEASE_NOTES.md", "RELEASE_NOTES.md"),
            ("scripts/run-tracker-windows-check.cmd", "run-windows-check.cmd"),
            ("plugins/common/TRACKER_WINDOWS_README.txt", "TRACKER-README.txt")):
            shutil.copy2(regular_file(ROOT / source, ROOT), stage / output)
        vstgui = Path(cache.get("FETCHCONTENT_SOURCE_DIR_VSTGUI") or build / "_deps/vstgui-src")
        shutil.copy2(regular_file(vstgui / "LICENSE", vstgui), stage / "Resources/Licenses/VSTGUI-LICENSE.txt")
        merge_tree(ROOT / "wavetables/vot", stage / "VOT Wavetables")
        merge_tree(ROOT / "examples/voicebanks/s3g-demo-synthetic", stage / "Ambi Vox Demo Voicebank")
        readme = (ROOT / "plugins/common/WINDOWS_PRERELEASE_README.txt").read_text(encoding="utf-8")
        (stage / "README.txt").write_text(readme.replace("@version@", args.version), encoding="utf-8")
        (stage / "PLUGIN_MANIFEST.tsv").write_text("".join(
            f"{item.build_path}\t{item.installed_name}\t{item.plugin_id}\t{item.host_name}\n"
            for item in bundles), encoding="utf-8")
        (stage / "EXCLUDED.txt").write_text(f"{EXCLUDED}\tWindows integration unfinished; Mac only.\n", encoding="utf-8")
        inventory = verify_payload(stage, bundles, dumpbin)
        (stage / "DESCRIPTORS.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
        (stage / "SOURCE_PROVENANCE.txt").write_text(
            f"Version: {args.version}\nSource revision: {revision}\n"
            f"Source status: {'dirty rehearsal; not for release' if status else 'clean'}\n"
            f"Package purpose: {'rehearsal; not for release' if args.allow_dirty else 'release candidate'}\n"
            f"Build: native MSVC x64 Release; static compiler runtime\n"
            f"Generator: {cache['CMAKE_GENERATOR']}\nFiles: 121\nDescriptors: 128\n"
            "Status: experimental; package checks do not replace REAPER testing.\n", encoding="utf-8")
        files = sorted(path for path in stage.rglob("*") if path.is_file())
        hashes = {path.relative_to(stage).as_posix(): digest(path) for path in files}
        (stage / "SHA256SUMS.txt").write_text("".join(f"{value}  {key}\n" for key, value in hashes.items()), encoding="utf-8")
        candidate = Path(temporary) / archive.name
        with zipfile.ZipFile(candidate, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    output.write(path, path.relative_to(stage.parent).as_posix())
        with zipfile.ZipFile(candidate) as output:
            if output.testzip() is not None:
                raise ValueError("ZIP CRC verification failed")
            extracted = Path(temporary) / "verification"
            output.extractall(extracted)
        restored = extracted / name
        if {path.relative_to(restored).as_posix() for path in restored.rglob("*") if path.is_file()} != set(hashes) | {"SHA256SUMS.txt"}:
            raise ValueError("ZIP inventory verification failed")
        for relative, value in hashes.items():
            if digest(restored / relative) != value:
                raise ValueError(f"ZIP content verification failed: {relative}")
        verify_payload(restored, bundles, dumpbin)
        if run("git", "status", "--porcelain", "--untracked-files=all") != status or run("git", "rev-parse", "HEAD").strip() != revision:
            raise ValueError("Source tree changed while packaging; no archive published")
        stage.rename(destination)
        candidate.rename(archive)
        with checksum.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(f"{digest(archive)}  {archive.name}\n")
    print(f"PASS: 121 Windows x64 CLAP files / 128 runtime descriptors\n{archive}\n{checksum}")
    print("Experimental package prepared, not installed or published. Native REAPER testing remains required.")


if __name__ == "__main__":
    main()
