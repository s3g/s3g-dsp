#!/usr/bin/env python3
"""Focused tests for the realtime release-profile orchestrator."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
ORCHESTRATOR = ROOT / "scripts" / "run-clap-realtime-release-gates.py"


class ReleaseGateOrchestratorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.runner = self.root / "mock-runner.py"
        self.audit = self.root / "mock-audit"
        self.audit.write_text("", encoding="utf-8")
        self.audit.chmod(0o755)
        self.manifest = self.root / "manifest.tsv"
        self.manifest.write_text("# mock\n", encoding="utf-8")
        self.build_root = self.root / "plugins"
        self.build_root.mkdir()
        self.output_dir = self.root / "reports"
        self.runner.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                from pathlib import Path
                import sys

                output = Path(sys.argv[sys.argv.index("--output") + 1])
                key = output.stem.removeprefix("clap-realtime-release-gate-")
                profiles = {
                    "core": (
                        ["org.s3g.s3g-dsp.ambi-group-rotate-64", "org.s3g.s3g-dsp.node-bus-mixer"],
                        [48000, 96000], [32, 64, 128, 256], True,
                    ),
                    "buffer-floor-48khz": (
                        ["org.s3g.s3g-dsp.delay-processor-24ch", "org.s3g.s3g-dsp.ambisonic-rotate-64", "org.s3g.s3g-dsp.ambi-group-rotate-128"],
                        [48000], [32, 64, 128, 256], True,
                    ),
                    "environmental-96khz-advisory": (
                        ["org.s3g.s3g-dsp.ambi-water-encoder-64", "org.s3g.s3g-dsp.ambi-insect-encoder-64"],
                        [96000], [32, 64, 128, 256], False,
                    ),
                }
                ids, rates, blocks, strict = profiles[key]
                failed = key == "core"
                probed = "--allocation-probe-library" in sys.argv
                audits = []
                for plugin_id in ids:
                    scenarios = [
                        {"name": "baseline", "sample_rate": rate, "block_size": block}
                        for rate in rates for block in blocks
                    ]
                    audits.append({"plugin_id": plugin_id, "report": {"scenarios": scenarios}})
                output.write_text(json.dumps({
                    "schema": "org.s3g.s3g-dsp.clap-realtime-audit.aggregate/v1",
                    "selected": len(ids),
                    "completed": len(ids),
                    "failures": [],
                    "skipped": [],
                    "configuration": {
                        "sample_rates": ",".join(str(value) for value in rates),
                        "blocks": ",".join(str(value) for value in blocks),
                        "iterations": 10000 if strict else 1000,
                        "automation_ladder": True,
                        "allocation_probe": probed,
                    },
                    "audits": audits,
                    "release_gate": {
                        "enabled": strict,
                        "passed": not failed,
                        "minimum_measured_iterations": 10000,
                        "maximum_p99_deadline_load": 0.75,
                        "maximum_deadline_miss_rate": 0.01,
                    },
                    "allocation_gate": {
                        "enabled": probed and not strict,
                        "passed": True,
                    },
                    "argv": sys.argv[1:],
                }), encoding="utf-8")
                raise SystemExit(1 if failed else 0)
                """
            ),
            encoding="utf-8",
        )
        self.runner.chmod(0o755)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_orchestrator(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(ORCHESTRATOR),
                "--runner",
                str(self.runner),
                "--audit-executable",
                str(self.audit),
                "--manifest",
                str(self.manifest),
                "--build-root",
                str(self.build_root),
                "--output-dir",
                str(self.output_dir),
                *arguments,
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_failure_does_not_stop_later_profiles(self) -> None:
        result = self.run_orchestrator(
            "--profile", "core", "--profile", "buffer-floor-48khz"
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        summary = json.loads(
            (self.output_dir / "clap-realtime-release-gates-summary.json")
            .read_text(encoding="utf-8")
        )
        self.assertEqual(len(summary["profiles"]), 2)
        self.assertFalse(summary["passed"])
        self.assertEqual(summary["profiles"][0]["returncode"], 1)
        self.assertEqual(summary["profiles"][1]["returncode"], 0)
        self.assertTrue(summary["profiles"][1]["report_valid"])
        self.assertTrue(
            (self.output_dir
             / "clap-realtime-release-gate-buffer-floor-48khz.json").is_file()
        )

    def test_advisory_profile_omits_release_gate(self) -> None:
        result = self.run_orchestrator(
            "--profile", "environmental-96khz-advisory"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        summary = json.loads(
            (self.output_dir / "clap-realtime-release-gates-summary.json")
            .read_text(encoding="utf-8")
        )
        self.assertIsNone(summary["profiles"][0]["release_gate_passed"])
        self.assertFalse(summary["profiles"][0]["release_gate"])
        self.assertTrue(summary["profiles"][0]["report_valid"])

    def test_stale_report_cannot_make_missing_output_pass(self) -> None:
        self.runner.write_text(
            "#!/usr/bin/env python3\nraise SystemExit(0)\n",
            encoding="utf-8",
        )
        self.runner.chmod(0o755)
        self.output_dir.mkdir(parents=True)
        stale = self.output_dir / "clap-realtime-release-gate-buffer-floor-48khz.json"
        stale.write_text('{"stale": true}', encoding="utf-8")
        result = self.run_orchestrator("--profile", "buffer-floor-48khz")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(stale.exists())
        summary = json.loads(
            (self.output_dir / "clap-realtime-release-gates-summary.json")
            .read_text(encoding="utf-8")
        )
        self.assertFalse(summary["profiles"][0]["report_valid"])

    def test_strict_profile_rejects_weakened_timing_policy(self) -> None:
        source = self.runner.read_text(encoding="utf-8")
        self.runner.write_text(
            source.replace(
                '"maximum_deadline_miss_rate": 0.01',
                '"maximum_deadline_miss_rate": 0.02',
            ),
            encoding="utf-8",
        )
        self.runner.chmod(0o755)
        result = self.run_orchestrator("--profile", "buffer-floor-48khz")
        self.assertEqual(result.returncode, 1, result.stderr)
        summary = json.loads(
            (self.output_dir / "clap-realtime-release-gates-summary.json")
            .read_text(encoding="utf-8")
        )
        self.assertFalse(summary["profiles"][0]["report_valid"])
        self.assertTrue(
            any(
                "deadline-miss rate limit" in error
                for error in summary["profiles"][0]["report_errors"]
            )
        )

    def test_allocation_library_is_forwarded_and_advisory_is_gated(self) -> None:
        probe = self.root / "probe.dylib"
        probe.write_bytes(b"mock")
        result = self.run_orchestrator(
            "--profile", "environmental-96khz-advisory",
            "--allocation-probe-library", str(probe),
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(
            (self.output_dir / "clap-realtime-release-gate-environmental-96khz-advisory.json")
            .read_text(encoding="utf-8")
        )
        self.assertIn("--allocation-probe-library", report["argv"])
        self.assertIn("--allocation-gate", report["argv"])


if __name__ == "__main__":
    unittest.main()
