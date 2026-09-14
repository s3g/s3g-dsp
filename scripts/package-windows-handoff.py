#!/usr/bin/env python3
"""Package an exact Windows handoff without Git writes, installs or network calls.

Uses tracked working-tree files plus an explicit untracked-file allowlist. Never
follows symlinks or recursively copies the workspace. Run with Python 3.9+.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parent.parent
KIT_ROOT = "s3g-dsp-windows-handoff"
HANDOFF_FILES = {
    "WINDOWS_CODE_INTEGRATION.md",
    "WINDOWS_TESTING_HANDOFF.md",
    "scripts/package-windows-handoff.py",
    "scripts/windows-handoff/START_HERE.md",
    "scripts/windows-handoff/CODEX_START_PROMPT.txt",
    "scripts/windows-handoff/VERIFY_HANDOFF.ps1",
}
PACKAGES = {
    "s3g-dsp-windows-clap-suite-x64-test-20260914-130216.zip":
        "f1094d38ac3686bcf7bce5854b1d0e48cf9040bf9a6728ff5ad1d0ee25770a0d",
    "s3g-tracker-windows-x64-test-20260914-130343.zip":
        "9c59539ed982a55f28ce60029a84e2736fefabc5cf99a06b4df2d7ba72aca5e7",
}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT).decode("utf-8")


def sha256_file(path):
    with path.open("rb") as stream:
        return sha256_stream(stream)


def sha256_stream(stream):
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def validate_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or str(path) != name:
        raise ValueError(f"Unsafe archive path: {name!r}")
    for part in path.parts:
        if (part in (".", "..") or part.endswith((".", " ")) or
                any(ord(c) < 32 or c in '<>:"\\|?*' for c in part) or
                re.fullmatch(r"CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9]",
                             part.split(".")[0], re.IGNORECASE)):
            raise ValueError(f"Not a safe Windows path: {name!r}")
    # Allow a short C:\s3g extraction root without depending on long-path policy.
    if len("C:/s3g/" + KIT_ROOT + "/" + name) >= 240:
        raise ValueError(f"Archive path is too long for this handoff: {name}")


def regular_source(path):
    current = ROOT
    for part in PurePosixPath(path).parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"Refusing source symlink: {path}")
    if not current.is_file():
        raise ValueError(f"Missing regular source file: {path}")
    return current


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include", action="append", default=[],
                        help="Explicit additional non-ignored untracked source path")
    args = parser.parse_args()
    status = git("status", "--porcelain=v1", "--untracked-files=all")
    head = git("rev-parse", "HEAD").strip()
    untracked = set(filter(None, git("ls-files", "--others", "--exclude-standard", "-z").split("\0")))
    allowed = HANDOFF_FILES | set(args.include)
    unexpected = untracked - allowed
    if unexpected:
        raise ValueError("Review untracked files before transfer; explicitly include source with "
                         "--include, or keep it outside this snapshot:\n" + "\n".join(sorted(unexpected)))
    invalid_includes = set(args.include) - untracked
    if invalid_includes:
        raise ValueError("--include must name a non-ignored untracked file: " + str(sorted(invalid_includes)))

    sources = {}
    omitted = []
    for entry in filter(None, git("ls-files", "--stage", "-z").split("\0")):
        metadata, name = entry.split("\t", 1)
        mode, blob, stage = metadata.split()
        if stage != "0":
            raise ValueError(f"Unmerged source file: {name}")
        if mode == "120000" and name == "max":
            target = (ROOT / name).readlink().as_posix()
            if target != "../s3g-clap-max":
                raise ValueError(f"Unexpected external max link: {target}")
            omitted.append({"path": name, "target": target, "index_blob": blob,
                            "reason": "External sibling repository; not needed for CLAP builds"})
            continue
        if mode not in ("100644", "100755"):
            raise ValueError(f"Unsupported source mode {mode}: {name}")
        sources[name] = {"origin": "tracked-working-tree", "git_mode": mode, "index_blob": blob}
    for name in untracked:
        sources[name] = {"origin": "explicit-untracked", "git_mode": "100644"}
    if HANDOFF_FILES - sources.keys():
        raise ValueError("Missing handoff files")

    payload = {}
    for name, metadata in sorted(sources.items()):
        validate_name(name)
        if any(part in {".git", ".codex", ".agents", ".env"} for part in PurePosixPath(name).parts):
            raise ValueError(f"Private configuration is outside this transfer: {name}")
        path = regular_source(name)
        metadata.update(sha256=sha256_file(path), bytes=path.stat().st_size)
        payload["s3g-dsp/" + name] = (path, metadata["sha256"])

    for name, expected in PACKAGES.items():
        path = ROOT / "dist" / name
        if path.is_symlink() or sha256_file(path) != expected:
            raise ValueError(f"Package does not match the documented baseline: {name}")
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            if bad:
                raise ValueError(f"Corrupt package member: {name}/{bad}")
        payload["windows-test-packages/" + name] = (path, expected)
    suite_checksum = ROOT / "dist" / (next(iter(PACKAGES)) + ".sha256")
    if suite_checksum.is_symlink():
        raise ValueError("Refusing checksum symlink")
    payload["windows-test-packages/" + suite_checksum.name] = (suite_checksum, sha256_file(suite_checksum))
    for name in ("START_HERE.md", "CODEX_START_PROMPT.txt", "VERIFY_HANDOFF.ps1"):
        path = ROOT / "scripts" / "windows-handoff" / name
        payload[name] = (path, sha256_file(path))

    created = datetime.now(timezone.utc)
    snapshot = {
        "format": 1, "created_utc": created.isoformat(), "base_commit": head,
        "branch": git("branch", "--show-current").strip(),
        "working_tree_status": status, "sources": sources, "omitted": omitted,
        "git_history_included": False, "dependencies_vendored": False,
        "windows_packages_sha256": PACKAGES,
    }
    generated = {"SOURCE_SNAPSHOT.json": (json.dumps(snapshot, indent=2, ensure_ascii=False) + "\n").encode("utf-8")}
    hashes = {name: digest for name, (_, digest) in payload.items()}
    hashes.update({name: hashlib.sha256(data).hexdigest() for name, data in generated.items()})
    seen = set()
    for name in [*hashes, "SHA256SUMS.txt"]:
        validate_name(name)
        if name.casefold() in seen:
            raise ValueError(f"Windows case-insensitive path collision: {name}")
        seen.add(name.casefold())
    generated["SHA256SUMS.txt"] = "".join(f"{digest}  {name}\n" for name, digest in sorted(hashes.items())).encode("utf-8")

    (ROOT / "dist").mkdir(exist_ok=True)
    output = ROOT / "dist" / f"s3g-dsp-windows-handoff-{created:%Y%m%d-%H%M%S}-utc.zip"
    # Exclusive creation preserves earlier packages; validation failure leaves
    # the archive for inspection but does not issue its success checksum.
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, (path, _) in sorted(payload.items()):
            archive.write(path, KIT_ROOT + "/" + name,
                          compress_type=zipfile.ZIP_STORED if name.endswith(".zip") else zipfile.ZIP_DEFLATED)
        for name, data in sorted(generated.items()):
            archive.writestr(KIT_ROOT + "/" + name, data)

    with zipfile.ZipFile(output) as archive:
        expected_names = {KIT_ROOT + "/" + name for name in [*payload, *generated]}
        if len(archive.namelist()) != len(expected_names) or set(archive.namelist()) != expected_names:
            raise ValueError("Archive inventory mismatch")
        for name, expected in hashes.items():
            with archive.open(KIT_ROOT + "/" + name) as stream:
                if sha256_stream(stream) != expected:
                    raise ValueError(f"Archived payload hash mismatch: {name}")
        if archive.testzip():
            raise ValueError("Archive CRC validation failed")
    # Detect source changes during packaging rather than silently mixing states.
    if git("status", "--porcelain=v1", "--untracked-files=all") != status or git("rev-parse", "HEAD").strip() != head:
        raise ValueError("Git state changed during packaging; no success checksum issued")
    for name, metadata in sources.items():
        if sha256_file(regular_source(name)) != metadata["sha256"]:
            raise ValueError(f"Source changed during packaging: {name}")
    digest = sha256_file(output)
    with output.with_suffix(output.suffix + ".sha256").open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(f"{digest}  {output.name}\n")
    print(f"PASS: {len(sources)} source files, {len(hashes)} verified payload files")
    print(f"Base commit: {head}")
    print(f"Archive: {output}")
    print(f"Bytes: {output.stat().st_size}")
    print(f"SHA256: {digest}")
    print("No Git writes, installed plug-in changes, or external link traversal.")


if __name__ == "__main__":
    main()
