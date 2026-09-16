"""Cross-platform tests for the native Windows release packager's safety gates."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "windows_package", ROOT / "scripts/package-windows-clap-prerelease.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class WindowsPackageTests(unittest.TestCase):
    def test_release_build_preset_uses_complete_platform_target(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        windows = next(item for item in presets["buildPresets"]
                       if item["name"] == "clap-windows-release")
        self.assertEqual(windows["targets"], ["s3g_windows_prerelease"])
        mac = next(item for item in presets["buildPresets"]
                   if item["name"] == "clap-release")
        self.assertNotIn("targets", mac)

    @unittest.skipUnless(shutil.which("cmake"), "CMake unavailable")
    def test_windows_release_target_builds_plugins_and_selected_regressions(self):
        helper = ROOT / "cmake/S3GWindowsPrerelease.cmake"
        workflow = (ROOT / ".github/workflows/windows-prerelease.yml").read_text()
        selection = re.search(r'-R "([^"]+)"', workflow).group(1)
        self.assertIn(f'target MATCHES "{selection}"', helper.read_text())
        self.assertIn(f'-R "{selection}"',
                      (ROOT / "docs/building-from-source.html").read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, build = root / "source", root / "build"
            source.mkdir()
            (source / "ok.cpp").write_text("int main() { return 0; }\n")
            (source / "unported.cpp").write_text('#error "Not part of this release target"\n')
            test_helper = '''
function(regression target)
  add_executable(${target} "${PROJECT_SOURCE_DIR}/ok.cpp")
  add_test(NAME ${target} COMMAND ${target})
endfunction()
'''
            (source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(release_target_fixture LANGUAGES CXX)
enable_testing()
{test_helper}
regression(s3g_windows_dsp_float_mode)
regression(s3g_vstgui_windows_font_smoke)
regression(s3g_spectral_windows_fft)
regression(s3g_nim_windows_bit_scan)
regression(s3g_shared_efficiency_dsp)
regression(s3g_sample_family_interaction_smoke)
add_executable(s3g_clap_realtime_audit unported.cpp)
add_executable(s3g_stereo_conduit_canvas_smoke unported.cpp)
add_subdirectory(tracker)
add_subdirectory(plugins/clap_example)
add_subdirectory(plugins/clap_tracker)
include("{helper.as_posix()}")
s3g_add_windows_prerelease_target()
''')
            for relative, contents in {
                "tracker": '''
regression(s3g_tracker_core_tests)
add_executable(s3g_tracker_starter_pack_generator "${PROJECT_SOURCE_DIR}/ok.cpp")
add_test(NAME s3g_tracker_starter_pack_generator_tests COMMAND s3g_tracker_starter_pack_generator)
''',
                "plugins/clap_example": '''
foreach(target example_8 example_24)
  add_library(${target} MODULE "${PROJECT_SOURCE_DIR}/ok.cpp")
  set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".clap")
endforeach()
regression(s3g_macro_delay_windows_clap_smoke)
add_executable(example_unported_tool "${PROJECT_SOURCE_DIR}/unported.cpp")
''',
                "plugins/clap_tracker": '''
add_library(s3g_tracker_clap MODULE "${PROJECT_SOURCE_DIR}/ok.cpp")
set_target_properties(s3g_tracker_clap PROPERTIES PREFIX "" OUTPUT_NAME "s3g_tracker" SUFFIX ".clap")
regression(s3g_tracker_windows_clap_smoke)
''',
            }.items():
                folder = source / relative
                folder.mkdir(parents=True)
                (folder / "CMakeLists.txt").write_text(contents)

            def run(*command):
                result = subprocess.run(command, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout

            run("cmake", "-S", str(source), "-B", str(build))
            run("cmake", "--build", str(build), "--config", "Release",
                "--target", "s3g_windows_prerelease", "--parallel", "2")
            self.assertEqual({path.name for path in build.rglob("*.clap")},
                             {"example_8.clap", "example_24.clap", "s3g_tracker.clap"})
            tests = json.loads(run("ctest", "--test-dir", str(build), "-C", "Release",
                                   "--show-only=json-v1", "-R", selection))["tests"]
            self.assertEqual(len(tests), 10)
            for test in tests:
                self.assertTrue(Path(test["command"][0]).is_file(), test["name"])
            run("ctest", "--test-dir", str(build), "-C", "Release",
                "--output-on-failure", "--no-tests=error", "-R", selection)

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
