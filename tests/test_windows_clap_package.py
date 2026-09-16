"""Cross-platform tests for the native Windows release packager's safety gates."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "windows_package", ROOT / "scripts/package-windows-clap-prerelease.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class WindowsPackageTests(unittest.TestCase):
    def test_complete_manifest_excludes_only_energy(self):
        inventory = PACKAGE.windows_inventory(ROOT / "scripts/clap-bundles.tsv")
        self.assertEqual(len(inventory), 121)
        self.assertNotIn(PACKAGE.EXCLUDED, [item.installed_name for item in inventory])
        self.assertIn("s3g_tracker.clap", [item.installed_name for item in inventory])

    def test_manifest_path_traversal_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.tsv"
            manifest.write_text("../outside.clap\ts3g_test.clap\torg.s3g.s3g-dsp.test\tTest\n")
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                PACKAGE.windows_inventory(manifest)

    def test_resolves_actual_target_not_canonical_name(self):
        build = Path(tempfile.gettempdir()) / "s3g-build"
        targets = [{"name": "different_target", "type": "MODULE_LIBRARY", "artifacts": [
            {"path": "plugins/clap_example/Release/s3g_example.clap"}]}]
        name, binary = PACKAGE.resolve_artifact(build, targets, "clap_example/s3g_example.clap")
        self.assertEqual(name, "different_target")
        self.assertEqual(binary.name, "s3g_example.clap")
        for invalid in ([], targets * 2, [{**targets[0], "type": "STATIC_LIBRARY"}]):
            with self.assertRaisesRegex(ValueError, "exactly one"):
                PACKAGE.resolve_artifact(build, invalid, "clap_example/s3g_example.clap")

    def test_debug_artifact_is_not_release(self):
        with self.assertRaises(ValueError):
            PACKAGE.resolve_artifact(Path(tempfile.gettempdir()), [{
                "name": "example", "type": "MODULE_LIBRARY", "artifacts": [
                    {"path": "plugins/clap_example/Debug/s3g_example.clap"}]}],
                "clap_example/s3g_example.clap")

    def test_file_api_selects_release_and_latest_index(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            reply = build / ".cmake/api/v1/reply"
            reply.mkdir(parents=True)
            (reply / "index-1.json").write_text('{"old": true}')
            (reply / "index-2.json").write_text(json.dumps({"reply": {
                "client-s3g-release": {"codemodel-v2": {"jsonFile": "model.json"}}}}))
            (reply / "model.json").write_text(json.dumps({"configurations": [
                {"name": "Debug", "targets": [{"jsonFile": "not-read.json"}]},
                {"name": "Release", "targets": [{"jsonFile": "target.json"}]}]}))
            (reply / "target.json").write_text('{"name": "actual_target"}')
            self.assertEqual(PACKAGE.release_targets(build), [{"name": "actual_target"}])

    def test_resource_collisions_require_identical_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "source", root / "output"
            source.mkdir()
            (source / "font.txt").write_text("font license")
            PACKAGE.merge_tree(source, output)
            PACKAGE.merge_tree(source, output)
            self.assertEqual((output / "font.txt").read_text(), "font license")
            (source / "font.txt").write_text("different")
            with self.assertRaisesRegex(ValueError, "Conflicting"):
                PACKAGE.merge_tree(source, output)

    def test_resource_symlinks_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            (root / "private").write_text("not a resource")
            try:
                (source / "link").symlink_to(root / "private")
            except OSError:
                self.skipTest("Symlink creation unavailable to this Windows account")
            with self.assertRaisesRegex(ValueError, "symlink"):
                PACKAGE.merge_tree(source, root / "output")

    def test_regular_file_cannot_escape_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            source.mkdir()
            outside = root / "outside"
            outside.write_text("private")
            with self.assertRaisesRegex(ValueError, "escaped"):
                PACKAGE.regular_file(outside, source)

    def test_native_release_cache_requirements(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        cache = dict(next(item for item in presets["configurePresets"]
                          if item["name"] == "clap-windows-release")["cacheVariables"])
        cache.update(CMAKE_HOME_DIRECTORY=str(ROOT), CMAKE_GENERATOR="Visual Studio 18 2026",
                     CMAKE_GENERATOR_PLATFORM="x64", CMAKE_CONFIGURATION_TYPES="Debug;Release",
                     CMAKE_PROJECT_VERSION="0.10.0")
        PACKAGE.validate_cache(cache, PACKAGE.VERSION)
        for key, value in (("CMAKE_GENERATOR", "Unix Makefiles"),
                           ("CMAKE_HOME_DIRECTORY", ""),
                           ("CMAKE_GENERATOR_PLATFORM", "ARM64"),
                           ("CMAKE_CONFIGURATION_TYPES", "Debug"),
                           ("CMAKE_PROJECT_VERSION", "0.9.0"),
                           ("CMAKE_MSVC_RUNTIME_LIBRARY", "MultiThreadedDLL"),
                           ("S3G_ENABLE_WORLD", "OFF"),
                           ("S3G_BUILD_TRACKER_PREVIEW", "OFF")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                PACKAGE.validate_cache({**cache, key: value}, PACKAGE.VERSION)

    def test_pe_architecture_exports_and_runtime_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "test.clap"
            data = bytearray(256)
            data[:2] = b"MZ"
            struct.pack_into("<I", data, 60, 128)
            data[128:134] = b"PE\0\0\x64\x86"
            binary.write_bytes(data)
            with patch.object(PACKAGE, "run", side_effect=["KERNEL32.dll", "1 0 clap_entry"]):
                PACKAGE.validate_pe(binary, "dumpbin")
            for dependency in ("VCRUNTIME140.dll", "MSVCP140.dll", "libstdc++-6.dll",
                               "libgcc_s_seh-1.dll", "libwinpthread-1.dll"):
                with patch.object(PACKAGE, "run", return_value=dependency), self.assertRaisesRegex(ValueError, "runtime"):
                    PACKAGE.validate_pe(binary, "dumpbin")
            with patch.object(PACKAGE, "run", side_effect=["KERNEL32.dll", "no entry"]), self.assertRaisesRegex(ValueError, "export"):
                PACKAGE.validate_pe(binary, "dumpbin")
            data[132:134] = b"\x64\xaa"  # ARM64, not x64
            binary.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "x64"):
                PACKAGE.validate_pe(binary, "dumpbin")

    def test_release_preset_enables_all_converted_mac_families(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        cache = next(item for item in presets["configurePresets"]
                     if item["name"] == "clap-release")["cacheVariables"]
        import re
        families = re.findall(r"option\((S3G_ENABLE_\w+_VSTGUI_ON_MACOS)",
                              (ROOT / "CMakeLists.txt").read_text())
        for option in families + ["S3G_ENABLE_TRACKER_PORTABLE_SHELL_ON_MACOS"]:
            self.assertEqual(cache.get(option), "ON", option)
        self.assertEqual(cache["CMAKE_OSX_ARCHITECTURES"], "arm64")


if __name__ == "__main__":
    unittest.main()
