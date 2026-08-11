#!/usr/bin/env python3

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "run-clap-validator-manifest.py"


class ManifestValidatorRunnerTests(unittest.TestCase):
    def make_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        manifest = root / "manifest.tsv"
        manifest.write_text(
            "one/one.clap\tone.clap\torg.s3g.test.pass\ts3g Pass\n"
            "two/two.clap\ttwo.clap\torg.s3g.test.fail\ts3g Fail\n"
            "three/three.clap\tthree.clap\torg.s3g.test.slow\ts3g Slow\n",
            encoding="utf-8",
        )
        build = root / "plugins"
        for relative in ("one/one.clap", "two/two.clap", "three/three.clap"):
            (build / relative).mkdir(parents=True)
        validator = root / "fake-validator.py"
        validator.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import sys
                import time
                plugin_id = sys.argv[sys.argv.index("--plugin-id") + 1]
                if plugin_id.endswith("fail"):
                    print("deliberate failure")
                    raise SystemExit(7)
                if plugin_id.endswith("slow"):
                    time.sleep(2)
                print("ok")
                """
            ),
            encoding="utf-8",
        )
        validator.chmod(0o755)
        return manifest, build, validator

    def test_continues_after_failure_and_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, build, validator = self.make_fixture(root)
            output = root / "report.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(RUNNER),
                    "--validator",
                    str(validator),
                    "--manifest",
                    str(manifest),
                    "--build-root",
                    str(build),
                    "--output",
                    str(output),
                    "--timeout-seconds",
                    "0.1",
                    "--jobs",
                    "3",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 1, completed.stdout + completed.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["selected"], 3)
            self.assertEqual(report["counts"]["passed"], 1)
            self.assertEqual(report["counts"]["failed"], 1)
            self.assertEqual(report["counts"]["timeout"], 1)
            self.assertFalse(report["passed"])
            self.assertEqual(len(list((root / "report-logs").glob("*.log"))), 3)

    def test_filters_manifest_ids(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, build, validator = self.make_fixture(root)
            output = root / "report.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(RUNNER),
                    "--validator",
                    str(validator),
                    "--manifest",
                    str(manifest),
                    "--build-root",
                    str(build),
                    "--output",
                    str(output),
                    "--filter",
                    "pass$",
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["selected"], 1)
            self.assertTrue(report["passed"])
            self.assertEqual(report["filter"], "pass$")

    def test_nonfinite_timeout_is_rejected_and_stale_report_removed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, build, validator = self.make_fixture(root)
            for value in ("nan", "inf", "-inf"):
                with self.subTest(value=value):
                    output = root / f"report-{value}.json"
                    output.write_text('{"stale": true}', encoding="utf-8")
                    completed = subprocess.run(
                        [
                            sys.executable, str(RUNNER),
                            "--validator", str(validator),
                            "--manifest", str(manifest),
                            "--build-root", str(build),
                            "--output", str(output),
                            "--timeout-seconds", value,
                        ],
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(completed.returncode, 2)
                    # argparse rejects before main; stale evidence therefore
                    # remains visibly stale rather than being overwritten.
                    self.assertEqual(
                        json.loads(output.read_text(encoding="utf-8")),
                        {"stale": True},
                    )

    def test_manifest_build_path_cannot_escape_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, build, validator = self.make_fixture(root)
            manifest.write_text(
                "../escape.clap\tescape.clap\torg.s3g.escape\tEscape\n",
                encoding="utf-8",
            )
            output = root / "report.json"
            output.write_text('{"stale": true}', encoding="utf-8")
            completed = subprocess.run(
                [
                    sys.executable, str(RUNNER),
                    "--validator", str(validator),
                    "--manifest", str(manifest),
                    "--build-root", str(build),
                    "--output", str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 2)
            self.assertIn("must stay under --build-root", completed.stderr)
            self.assertFalse(output.exists())

    def test_validator_032_tracker_direction_bug_is_scoped(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "manifest.tsv"
            manifest.write_text(
                "tracker/tracker.clap\ttracker.clap\t"
                "org.s3g.s3g-dsp.tracker\ts3g Tracker\n",
                encoding="utf-8",
            )
            build = root / "plugins"
            (build / "tracker" / "tracker.clap").mkdir(parents=True)
            arguments = root / "arguments.json"
            validator = root / "fake-validator.py"
            validator.write_text(
                textwrap.dedent(
                    f"""\
                    #!/usr/bin/env python3
                    import json
                    from pathlib import Path
                    import sys
                    if "--version" in sys.argv:
                        print("clap-validator 0.3.2")
                        raise SystemExit(0)
                    Path({str(arguments)!r}).write_text(json.dumps(sys.argv[1:]))
                    """
                ),
                encoding="utf-8",
            )
            validator.chmod(0o755)
            output = root / "report.json"
            completed = subprocess.run(
                [
                    sys.executable, str(RUNNER),
                    "--validator", str(validator),
                    "--manifest", str(manifest),
                    "--build-root", str(build),
                    "--output", str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            invoked = json.loads(arguments.read_text(encoding="utf-8"))
            self.assertIn("--invert-filter", invoked)
            self.assertEqual(
                invoked[invoked.index("--test-filter") + 1],
                "^process-note-",
            )
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["validator_version"], "0.3.2")
            self.assertEqual(
                report["results"][0]["compatibility_workaround"],
                "clap-validator-0.3.2-output-note-port-direction",
            )


if __name__ == "__main__":
    unittest.main()
