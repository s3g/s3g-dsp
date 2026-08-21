#!/usr/bin/env python3

"""Focused runtime-descriptor inventory tests for the CLAP package verifier."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
VERIFIER_PATH = REPO_ROOT / "scripts" / "verify-macos-clap-package.py"
SPEC = importlib.util.spec_from_file_location("verify_macos_clap_package", VERIFIER_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load verifier module: {VERIFIER_PATH}")
VERIFIER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = VERIFIER
SPEC.loader.exec_module(VERIFIER)


def descriptor(plugin_id: str, name: str = "Plugin", version: str = "0.8.0") -> dict[str, str]:
    return {"id": plugin_id, "name": name, "version": version}


class DescriptorInventoryTests(unittest.TestCase):
    def test_accepts_exact_unique_inventory(self) -> None:
        inventory = [
            ("one.clap", descriptor("org.s3g.one")),
            ("two.clap", descriptor("org.s3g.two")),
            ("two.clap", descriptor("org.s3g.two-aux")),
        ]
        VERIFIER.validate_descriptor_inventory(inventory, 3)

    def test_rejects_wrong_total(self) -> None:
        with self.assertRaises(VERIFIER.VerificationError):
            VERIFIER.validate_descriptor_inventory(
                [("one.clap", descriptor("org.s3g.one"))], 2
            )

    def test_rejects_duplicate_id_across_bundles(self) -> None:
        inventory = [
            ("one.clap", descriptor("org.s3g.same")),
            ("two.clap", descriptor("org.s3g.same")),
        ]
        with self.assertRaises(VERIFIER.VerificationError):
            VERIFIER.validate_descriptor_inventory(inventory, 2)

    def test_rejects_empty_or_unsafe_fields(self) -> None:
        for invalid in (
            descriptor(""),
            descriptor("org.s3g.one", name=""),
            descriptor("org.s3g.one", version="bad version"),
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(VERIFIER.VerificationError):
                    VERIFIER.validate_descriptor_inventory([("one.clap", invalid)], 1)


if __name__ == "__main__":
    unittest.main()
