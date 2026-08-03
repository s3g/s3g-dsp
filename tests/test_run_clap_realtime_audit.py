#!/usr/bin/env python3
"""Focused tests for scripts/run-clap-realtime-audit.py."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "run-clap-realtime-audit.py"


class ManifestRunnerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.build_root = self.root / "plugins"
        self.first = self.build_root / "clap_first" / "first.clap"
        self.second = self.build_root / "clap_second" / "second.clap"
        self.first.mkdir(parents=True)
        self.manifest = self.root / "clap-bundles.tsv"
        self.manifest.write_text(
            textwrap.dedent(
                """\
                # build\tinstalled\tid\tname
                clap_first/first.clap\tfirst-installed.clap\torg.s3g.first\tFirst Plugin
                clap_second/second.clap\tsecond-installed.clap\torg.s3g.second\tSecond Plugin
                """
            ),
            encoding="utf-8",
        )
        self.audit = self.root / "mock-audit.py"
        self.audit.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                from pathlib import Path
                import sys
                import time

                if sys.argv[-1] == "org.s3g.second":
                    time.sleep(2)
                output_index = sys.argv.index("--json") + 1
                output = Path(sys.argv[output_index])
                scenario = {"name": "mock", "measured_iterations": 1}
                if "--allocation-probe" in sys.argv:
                    scenario["allocation_probe"] = {
                        "measured_blocks": 1,
                        "totals": {
                            "operations": 0,
                            "allocation_failures": 0,
                            "invalid_alignment_calls": 0,
                        }
                    }
                output.write_text(json.dumps({
                    "schema": "org.s3g.s3g-dsp.clap-realtime-audit/v1",
                    "plugin": {"id": sys.argv[-1]},
                    "configuration": {
                        "allocation_probe": "--allocation-probe" in sys.argv,
                    },
                    "scenarios": [scenario],
                    "argv": sys.argv[1:],
                }), encoding="utf-8")
                """
            ),
            encoding="utf-8",
        )
        self.audit.chmod(0o755)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_runner(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--audit-executable",
                str(self.audit),
                "--manifest",
                str(self.manifest),
                "--build-root",
                str(self.build_root),
                *arguments,
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def write_static_audit_report(self, report: object) -> None:
        serialized = json.dumps(report)
        self.audit.write_text(
            textwrap.dedent(
                f"""\
                #!/usr/bin/env python3
                from pathlib import Path
                import sys

                output = Path(sys.argv[sys.argv.index("--json") + 1])
                output.write_text({serialized!r}, encoding="utf-8")
                """
            ),
            encoding="utf-8",
        )

    @staticmethod
    def valid_child_report(
        *,
        schema: str = "org.s3g.s3g-dsp.clap-realtime-audit/v1",
        plugin_id: str = "org.s3g.first",
        allocation_probe: bool = False,
        scenarios: list[object] | None = None,
    ) -> dict[str, object]:
        if scenarios is None:
            scenarios = [{"name": "baseline"}]
        return {
            "schema": schema,
            "plugin": {"id": plugin_id},
            "configuration": {"allocation_probe": allocation_probe},
            "scenarios": scenarios,
        }

    def test_filter_and_timing_options_are_forwarded(self) -> None:
        result = self.run_runner(
            "--filter",
            "First|org\\.example\\.not-present",
            "--sample-rates",
            "48000,96000",
            "--blocks",
            "64,256",
            "--warmup",
            "7",
            "--iterations",
            "11",
            "--event-burst",
            "23",
            "--allocation-probe",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["selected"], 1)
        self.assertEqual(aggregate["completed"], 1)
        child_arguments = aggregate["audits"][0]["report"]["argv"]
        self.assertIn("--sample-rates", child_arguments)
        self.assertIn("48000,96000", child_arguments)
        self.assertIn("--blocks", child_arguments)
        self.assertIn("64,256", child_arguments)
        self.assertIn("--warmup", child_arguments)
        self.assertIn("7", child_arguments)
        self.assertIn("--iterations", child_arguments)
        self.assertIn("11", child_arguments)
        self.assertIn("--event-burst", child_arguments)
        self.assertIn("23", child_arguments)
        self.assertIn("--allocation-probe", child_arguments)
        self.assertTrue(aggregate["configuration"]["allocation_probe"])
        self.assertEqual(child_arguments[-1], "org.s3g.first")

    def test_exclude_filter_uses_plugin_id(self) -> None:
        result = self.run_runner("--exclude-filter", "org\\.s3g\\.second$")
        self.assertEqual(result.returncode, 0, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["selected"], 1)
        self.assertEqual(aggregate["completed"], 1)
        self.assertEqual(aggregate["configuration"]["exclude_filter"], "org\\.s3g\\.second$")

    def test_full_sweep_and_stress_profiles_are_forwarded(self) -> None:
        result = self.run_runner(
            "--filter",
            "First",
            "--full-sweep",
            "--automation-ladder",
            "--control-publication-stress",
            "--nim-midi-flood",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        aggregate = json.loads(result.stdout)
        child_arguments = aggregate["audits"][0]["report"]["argv"]
        self.assertIn("48000,96000", child_arguments)
        self.assertIn("32,64,128,256", child_arguments)
        self.assertIn("--event-bursts", child_arguments)
        self.assertIn("1,4,8,16,64", child_arguments)
        self.assertIn("--distributed-events", child_arguments)
        self.assertIn("--control-publication-stress", child_arguments)
        self.assertIn("--nim-midi-flood", child_arguments)

    def test_affected_only_selects_known_weak_points(self) -> None:
        affected = self.build_root / "clap_affected" / "affected.clap"
        spray = self.build_root / "clap_spray" / "spray.clap"
        water = self.build_root / "clap_water" / "water.clap"
        affected.mkdir(parents=True)
        spray.mkdir(parents=True)
        water.mkdir(parents=True)
        self.manifest.write_text(
            textwrap.dedent(
                """\
                clap_affected/affected.clap\taffected.clap\torg.s3g.s3g-dsp.node-bus-mixer\tAffected Node Bus
                clap_spray/spray.clap\tspray.clap\torg.s3g.s3g-dsp.spectral-spray\tAffected Spray
                clap_water/water.clap\twater.clap\torg.s3g.s3g-dsp.ambi-water-encoder-64\tAffected Water
                clap_first/first.clap\tfirst.clap\torg.s3g.first\tFirst Plugin
                """
            ),
            encoding="utf-8",
        )
        result = self.run_runner("--affected-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["selected"], 3)
        self.assertEqual(
            {audit["plugin_id"] for audit in aggregate["audits"]},
            {
                "org.s3g.s3g-dsp.node-bus-mixer",
                "org.s3g.s3g-dsp.spectral-spray",
                "org.s3g.s3g-dsp.ambi-water-encoder-64",
            },
        )
        self.assertTrue(aggregate["configuration"]["affected_only"])

    def test_missing_bundle_is_failure_by_default(self) -> None:
        result = self.run_runner()
        self.assertEqual(result.returncode, 1)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["completed"], 1)
        self.assertEqual(len(aggregate["failures"]), 1)
        self.assertEqual(aggregate["failures"][0]["plugin_id"], "org.s3g.second")

    def test_skip_missing_records_skip_and_succeeds(self) -> None:
        result = self.run_runner("--skip-missing")
        self.assertEqual(result.returncode, 0, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["completed"], 1)
        self.assertEqual(len(aggregate["skipped"]), 1)
        self.assertEqual(aggregate["failures"], [])

    def test_plugin_timeout_is_recorded_as_failure(self) -> None:
        self.second.mkdir(parents=True)
        result = self.run_runner("--timeout-seconds", "0.05")
        self.assertEqual(result.returncode, 1)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["completed"], 1)
        self.assertEqual(len(aggregate["failures"]), 1)
        self.assertEqual(aggregate["failures"][0]["plugin_id"], "org.s3g.second")
        self.assertIn("timeout", aggregate["failures"][0]["reason"])

    def test_timeout_must_be_finite(self) -> None:
        for value in ("nan", "inf", "-inf"):
            with self.subTest(value=value):
                result = self.run_runner(f"--timeout-seconds={value}")
                self.assertEqual(result.returncode, 2)
                self.assertIn("finite number greater than zero", result.stderr)

    def test_successful_child_report_must_match_contract(self) -> None:
        cases = (
            ("non-object", [], "top level must be an object"),
            (
                "schema",
                self.valid_child_report(schema="org.example.wrong/v1"),
                "schema must be",
            ),
            (
                "plugin-id",
                self.valid_child_report(plugin_id="org.s3g.wrong"),
                "plugin.id must be",
            ),
            (
                "allocation-probe",
                self.valid_child_report(allocation_probe=True),
                "allocation_probe must match",
            ),
            (
                "empty-scenarios",
                self.valid_child_report(scenarios=[]),
                '"scenarios" must be a nonempty array',
            ),
        )
        for name, child_report, expected_reason in cases:
            with self.subTest(name=name):
                self.write_static_audit_report(child_report)
                result = self.run_runner("--filter", "First")
                self.assertEqual(result.returncode, 1, result.stderr)
                aggregate = json.loads(result.stdout)
                self.assertEqual(aggregate["completed"], 0)
                self.assertEqual(len(aggregate["failures"]), 1)
                self.assertIn(expected_reason, aggregate["failures"][0]["reason"])
                self.assertEqual(aggregate["failures"][0]["report"], child_report)

    def test_successful_child_malformed_json_is_failure(self) -> None:
        self.audit.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                from pathlib import Path
                import sys

                output = Path(sys.argv[sys.argv.index("--json") + 1])
                output.write_text("{not-json", encoding="utf-8")
                """
            ),
            encoding="utf-8",
        )
        result = self.run_runner("--filter", "First")
        self.assertEqual(result.returncode, 1, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["completed"], 0)
        self.assertEqual(len(aggregate["failures"]), 1)
        self.assertIn("invalid audit JSON", aggregate["failures"][0]["reason"])

    def test_failed_child_report_is_retained(self) -> None:
        self.audit.write_text(
            textwrap.dedent(
                """\
                #!/usr/bin/env python3
                import json
                from pathlib import Path
                import sys

                output = Path(sys.argv[sys.argv.index("--json") + 1])
                output.write_text(json.dumps({"failure_detail": 42}), encoding="utf-8")
                raise SystemExit(7)
                """
            ),
            encoding="utf-8",
        )
        result = self.run_runner("--filter", "First")
        self.assertEqual(result.returncode, 1)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["completed"], 0)
        self.assertEqual(aggregate["failures"][0]["report"]["failure_detail"], 42)

    def test_release_gate_passes_qualified_long_run(self) -> None:
        scenario = {
            "name": "baseline",
            "sample_rate": 48000,
            "block_size": 64,
            "measured_iterations": 10000,
            "deadline_misses": 0,
            "deadline_load": {"p99": 0.74},
            "error": None,
        }
        self.write_static_audit_report(
            self.valid_child_report(scenarios=[scenario])
        )
        result = self.run_runner("--filter", "First", "--release-gate")
        self.assertEqual(result.returncode, 0, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertTrue(aggregate["release_gate"]["passed"])
        self.assertEqual(aggregate["release_gate"]["evaluated_scenarios"], 1)
        self.assertEqual(aggregate["configuration"]["iterations"], 10000)
        self.assertEqual(aggregate["configuration"]["timeout_seconds"], 3600.0)
        self.assertEqual(
            aggregate["release_gate"]["maximum_deadline_miss_rate"], 0.01
        )

    def test_release_gate_accepts_isolated_wall_clock_tail(self) -> None:
        scenario = {
            "name": "baseline",
            "sample_rate": 48000,
            "block_size": 64,
            "measured_iterations": 10000,
            "deadline_misses": 100,
            "deadline_load": {"p99": 0.74},
            "error": None,
        }
        self.write_static_audit_report(
            self.valid_child_report(scenarios=[scenario])
        )
        result = self.run_runner("--filter", "First", "--release-gate")
        self.assertEqual(result.returncode, 0, result.stderr)
        gate = json.loads(result.stdout)["release_gate"]
        self.assertTrue(gate["passed"])
        self.assertEqual(gate["observed_deadline_misses"], 100)
        self.assertEqual(gate["observed_measured_iterations"], 10000)
        self.assertEqual(gate["observed_deadline_miss_rate"], 0.01)

    def test_release_gate_reports_broad_miss_tail_and_p99_violation(self) -> None:
        scenario = {
            "name": "automation-burst-64-same",
            "sample_rate": 96000,
            "block_size": 32,
            "measured_iterations": 10000,
            "deadline_misses": 101,
            "deadline_load": {"p99": 0.8},
            "error": None,
        }
        self.write_static_audit_report(
            self.valid_child_report(scenarios=[scenario])
        )
        result = self.run_runner("--filter", "First", "--release-gate")
        self.assertEqual(result.returncode, 1, result.stderr)
        aggregate = json.loads(result.stdout)
        gate = aggregate["release_gate"]
        self.assertFalse(gate["passed"])
        reasons = gate["violations"][0]["reasons"]
        self.assertTrue(any("deadline miss rate" in reason for reason in reasons))
        self.assertTrue(any("p99" in reason for reason in reasons))

    def test_release_gate_miss_rate_limit_is_configurable(self) -> None:
        scenario = {
            "name": "baseline",
            "sample_rate": 48000,
            "block_size": 64,
            "measured_iterations": 10000,
            "deadline_misses": 2,
            "deadline_load": {"p99": 0.2},
            "error": None,
        }
        self.write_static_audit_report(
            self.valid_child_report(scenarios=[scenario])
        )
        result = self.run_runner(
            "--filter",
            "First",
            "--release-gate",
            "--release-max-deadline-miss-rate",
            "0.0001",
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertEqual(
            aggregate["configuration"]["release_max_deadline_miss_rate"],
            0.0001,
        )
        reasons = aggregate["release_gate"]["violations"][0]["reasons"]
        self.assertTrue(any("deadline miss rate" in reason for reason in reasons))

    def test_release_gate_rejects_realtime_allocations(self) -> None:
        scenario = {
            "name": "baseline",
            "sample_rate": 48000,
            "block_size": 64,
            "measured_iterations": 10000,
            "deadline_misses": 0,
            "deadline_load": {"p99": 0.1},
            "allocation_probe": {
                "measured_blocks": 10000,
                "totals": {
                    "operations": 2,
                    "allocation_failures": 0,
                    "invalid_alignment_calls": 0,
                }
            },
            "error": None,
        }
        self.write_static_audit_report(
            self.valid_child_report(
                allocation_probe=True,
                scenarios=[scenario],
            )
        )
        result = self.run_runner(
            "--filter", "First", "--allocation-probe", "--release-gate"
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        aggregate = json.loads(result.stdout)
        gate = aggregate["release_gate"]
        self.assertFalse(gate["passed"])
        reasons = gate["violations"][0]["reasons"]
        self.assertTrue(any("operations must be zero" in reason for reason in reasons))

    def test_allocation_gate_requires_probe(self) -> None:
        result = self.run_runner("--filter", "First", "--allocation-gate")
        self.assertEqual(result.returncode, 1)
        self.assertIn("requires --allocation-probe", result.stderr)

    def test_allocation_gate_rejects_realtime_allocations(self) -> None:
        scenario = {
            "name": "baseline",
            "measured_iterations": 1,
            "allocation_probe": {
                "measured_blocks": 1,
                "totals": {
                    "operations": 2,
                    "allocation_failures": 0,
                    "invalid_alignment_calls": 0,
                }
            },
        }
        self.write_static_audit_report(
            self.valid_child_report(allocation_probe=True, scenarios=[scenario])
        )
        result = self.run_runner(
            "--filter", "First", "--allocation-probe", "--allocation-gate"
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertFalse(aggregate["allocation_gate"]["passed"])
        reasons = aggregate["allocation_gate"]["violations"][0]["reasons"]
        self.assertTrue(any("operations must be zero" in reason for reason in reasons))

    def test_probe_report_requires_allocation_totals(self) -> None:
        self.write_static_audit_report(
            self.valid_child_report(allocation_probe=True)
        )
        result = self.run_runner(
            "--filter", "First", "--allocation-probe", "--allocation-gate"
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        aggregate = json.loads(result.stdout)
        self.assertEqual(aggregate["completed"], 0)
        self.assertIn(
            "allocation_probe must be an object",
            aggregate["failures"][0]["reason"],
        )

    def test_release_gate_rejects_short_explicit_run(self) -> None:
        result = self.run_runner(
            "--filter", "First", "--release-gate", "--iterations", "9999"
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("requires --iterations", result.stderr)


if __name__ == "__main__":
    unittest.main()
