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
RUNNER = ROOT / "scripts" / "run-non-nim-release-checks.py"


class NonNimReleaseChecksTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.scripts = self.source / "scripts"
        self.scripts.mkdir(parents=True)
        self.build = self.root / "build"
        (self.build / "plugins").mkdir(parents=True)
        (self.scripts / "clap-bundles.tsv").write_text(
            "one/one.clap\tone.clap\torg.s3g.test.one\tOne\n"
            "nim/nim.clap\tnim.clap\torg.s3g.s3g-dsp.no-input-mixer-8ch\tNIM\n"
            "gesture/gesture.clap\tgesture.clap\torg.s3g.s3g-dsp.nim-gesture\tGesture\n",
            encoding="utf-8",
        )
        self._write_python("check-clap-bundle-manifest.py", "raise SystemExit(0)\n")
        self._write_python("check-clap-objc-symbols.py", "raise SystemExit(0)\n")
        self._write_validator_runner()
        self._write_allocation_runner()
        self._write_profile_runner()

        self.ctest = self.root / "ctest"
        self.ctest.write_text(
            "#!/usr/bin/env python3\n"
            "from pathlib import Path\nimport sys\n"
            f"Path({str(self.root / 'ctest-args.json')!r}).write_text(__import__('json').dumps(sys.argv[1:]))\n",
            encoding="utf-8",
        )
        self.ctest.chmod(0o755)
        self.validator = self.root / "clap-validator"
        self.audit = self.root / "realtime-audit"
        self.probe = self.root / "probe.dylib"
        for path in (self.validator, self.audit, self.probe):
            path.write_bytes(b"mock")
            path.chmod(0o755)
        self.output = self.build / "non-nim-release-checks.json"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_python(self, name: str, body: str) -> None:
        (self.scripts / name).write_text(
            "#!/usr/bin/env python3\n" + body,
            encoding="utf-8",
        )

    def _write_validator_runner(self) -> None:
        self._write_python(
            "run-clap-validator-manifest.py",
            textwrap.dedent(
                """
                import json
                from pathlib import Path
                import sys
                output = Path(sys.argv[sys.argv.index("--output") + 1])
                output.write_text(json.dumps({
                    "schema": "org.s3g.s3g-dsp.clap-validator-manifest/v1",
                    "passed": True,
                    "selected": 1,
                    "results": [{"plugin_id": "org.s3g.test.one", "status": "passed"}],
                }), encoding="utf-8")
                """
            ),
        )

    def _write_allocation_runner(self) -> None:
        self._write_python(
            "run-clap-realtime-audit.py",
            textwrap.dedent(
                """
                import json
                from pathlib import Path
                import sys
                output = Path(sys.argv[sys.argv.index("--output") + 1])
                output.write_text(json.dumps({
                    "schema": "org.s3g.s3g-dsp.clap-realtime-audit.aggregate/v1",
                    "selected": 1,
                    "completed": 1,
                    "failures": [],
                    "skipped": [],
                    "configuration": {"allocation_probe": True, "allocation_gate": True},
                    "allocation_gate": {"enabled": True, "passed": True},
                    "audits": [{"plugin_id": "org.s3g.test.one"}],
                }), encoding="utf-8")
                """
            ),
        )

    def _write_profile_runner(self) -> None:
        profiles = [
            "core", "buffer-floor-48khz", "buffer-floor-96khz",
            "spectral-8ch-48khz", "spectral-8ch-96khz",
            "spectral-24ch-48khz", "spectral-24ch-96khz",
            "spectral-spray", "environmental-48khz",
            "environmental-96khz-advisory",
        ]
        self._write_python(
            "run-clap-realtime-release-gates.py",
            textwrap.dedent(
                f"""
                import json
                from pathlib import Path
                import sys
                output_dir = Path(sys.argv[sys.argv.index("--output-dir") + 1])
                output = output_dir / "clap-realtime-release-gates-summary.json"
                output.write_text(json.dumps({{
                    "schema": "org.s3g.s3g-dsp.clap-realtime-release-gates/v1",
                    "passed": True,
                    "profiles": [
                        {{"profile": key, "returncode": 0, "report_valid": True}}
                        for key in {profiles!r}
                    ],
                }}), encoding="utf-8")
                """
            ),
        )

    def run_checks(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable, str(RUNNER),
                "--source-root", str(self.source),
                "--build-root", str(self.build),
                "--ctest", str(self.ctest),
                "--validator", str(self.validator),
                "--realtime-audit", str(self.audit),
                "--allocation-probe-library", str(self.probe),
                "--output", str(self.output),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_complete_evidence_and_nonempty_ctest_gate_pass(self) -> None:
        completed = self.run_checks()
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        report = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertTrue(report["passed"])
        self.assertEqual(len(report["checks"]), 6)
        self.assertTrue(all(check["status"] == "passed" for check in report["checks"]))
        ctest_arguments = json.loads(
            (self.root / "ctest-args.json").read_text(encoding="utf-8")
        )
        self.assertIn("--no-tests=error", ctest_arguments)

    def test_stale_missing_evidence_fails_but_later_checks_continue(self) -> None:
        self._write_python("run-clap-validator-manifest.py", "raise SystemExit(0)\n")
        stale = self.build / "clap-validator-non-nim.json"
        stale.write_text('{"stale": true}', encoding="utf-8")
        completed = self.run_checks()
        self.assertEqual(completed.returncode, 1)
        report = json.loads(self.output.read_text(encoding="utf-8"))
        statuses = {check["name"]: check["status"] for check in report["checks"]}
        self.assertEqual(statuses["clap-validator"], "failed")
        self.assertEqual(statuses["allocation-sweep-non-nim"], "passed")
        self.assertEqual(statuses["realtime-profiles"], "passed")
        self.assertFalse(stale.exists())


if __name__ == "__main__":
    unittest.main()
