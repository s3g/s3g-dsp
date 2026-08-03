#!/usr/bin/env python3

"""Focused path and version tests for the NIM app package verifier."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile


REPO_ROOT = Path(__file__).resolve().parents[1]
VERIFIER_PATH = REPO_ROOT / "scripts" / "verify-macos-nim-app-package.py"
SPEC = importlib.util.spec_from_file_location("verify_macos_nim_app_package", VERIFIER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load verifier module: {VERIFIER_PATH}")
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)


class VersionCompatibilityTests(unittest.TestCase):
    def test_numeric_bundle_version_accepts_matching_prerelease(self) -> None:
        self.assertTrue(
            VERIFIER.bundle_version_matches_release("0.6.0", "0.6.0-pre")
        )
        self.assertTrue(VERIFIER.bundle_version_matches_release("0.6.0", "0.6.0"))

    def test_numeric_bundle_version_rejects_different_or_nonnumeric_release(self) -> None:
        self.assertFalse(
            VERIFIER.bundle_version_matches_release("0.6.0", "0.6.1-pre")
        )
        self.assertFalse(
            VERIFIER.bundle_version_matches_release("0.6.0-pre", "0.6.0-pre")
        )


class ZipSafetyTests(unittest.TestCase):
    root = "s3g-no-input-mixer-app-macos-arm64-0.6.0-pre"

    def make_archive(self, members: list[tuple[zipfile.ZipInfo | str, bytes]]) -> Path:
        temporary = tempfile.NamedTemporaryFile(suffix=".zip", delete=False)
        temporary.close()
        archive_path = Path(temporary.name)
        self.addCleanup(archive_path.unlink, missing_ok=True)
        with zipfile.ZipFile(archive_path, "w") as archive:
            for member, content in members:
                archive.writestr(member, content)
        return archive_path

    def test_accepts_one_safe_package_root(self) -> None:
        archive = self.make_archive([(f"{self.root}/README.txt", b"ok")])
        self.assertEqual(
            VERIFIER.validate_zip_members(archive, "0.6.0-pre"), self.root
        )

    def test_rejects_path_traversal_and_multiple_roots(self) -> None:
        traversal = self.make_archive([(f"{self.root}/../escape", b"bad")])
        with self.assertRaises(VERIFIER.VerificationError):
            VERIFIER.validate_zip_members(traversal, "0.6.0-pre")

        multiple = self.make_archive(
            [(f"{self.root}/README.txt", b"ok"), ("second-root/file", b"bad")]
        )
        with self.assertRaises(VERIFIER.VerificationError):
            VERIFIER.validate_zip_members(multiple, "0.6.0-pre")

    def test_rejects_symlink_and_case_collision(self) -> None:
        symlink = zipfile.ZipInfo(f"{self.root}/link")
        symlink.create_system = 3
        symlink.external_attr = (stat.S_IFLNK | 0o777) << 16
        symlink_archive = self.make_archive([(symlink, b"README.txt")])
        with self.assertRaises(VERIFIER.VerificationError):
            VERIFIER.validate_zip_members(symlink_archive, "0.6.0-pre")

        collision = self.make_archive(
            [
                (f"{self.root}/README.txt", b"one"),
                (f"{self.root}/readme.txt", b"two"),
            ]
        )
        with self.assertRaises(VERIFIER.VerificationError):
            VERIFIER.validate_zip_members(collision, "0.6.0-pre")


if __name__ == "__main__":
    unittest.main()
