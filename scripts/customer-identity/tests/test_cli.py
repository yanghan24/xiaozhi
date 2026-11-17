from contextlib import redirect_stderr
from copy import deepcopy
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from identity_check.cli import main
from identity_check.errors import CheckError


class FakeSource:
    def __init__(self, env, timeout=20):
        pass

    def collect(self, records, start_ms=None, end_ms=None, baseline=None):
        row = {"label": "F1", "creation_order": 1, "stored_status": "IDENTITY_VERIFIED",
               "created_at_ms": 1000, "expires_at_ms": 301000, "identity_issued_at_ms": 1000,
               "sid_present": True, "identity_present": True, "authorization": "ACTIVE",
               "last_activity_ms": 1000, "matched_before_window": 0, "matched_in_window": 0,
               "matched_received_at_ms": []}
        return {"observed_at_ms": 5000000, "clock_skew_ms": 0,
                "environment": {key: "ok" for key in ("containers", "db_target", "proxy_target", "schema")},
                "environment_fingerprint": "a" * 64,
                "window": {"start_ms": start_ms, "end_ms": end_ms} if start_ms is not None else None,
                "flows": {label: [deepcopy(row)] for label in records}, "identities_distinct": True,
                "receipts": {"issuer_total": 0, "issuer_before_window": 0, "issuer_in_window": 0,
                             "other_issuer_total": 0, "since_baseline": 0 if baseline else None},
                "log_capture": {"configured": False, "missing": True, "text": None, "size": 0, "inode": None},
                "truncated": False}


class CliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.record = self.root / "round.json"
        self.handle = "synthetic-handle-12345"
        self.record.write_text(json.dumps({"schema": "xiaozhi-identity-round-v1", "request_handle": self.handle}), encoding="utf-8")
        self.record.chmod(0o600)
        self.env = {"record_root": str(self.root)}

    def run_cli(self, extra=None, factory=FakeSource, scenario="login"):
        args = [scenario, "--env", str(self.root / "env.json"), "--record", str(self.record)] + (extra or [])
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch("identity_check.envcheck.load_environment", return_value=self.env):
            code = main(args, source_factory=factory, stdout=stdout, stderr=stderr)
        return code, stdout.getvalue(), stderr.getvalue()

    def test_login_json_is_machine_parseable_and_summary_separate(self):
        code, stdout, stderr = self.run_cli()
        self.assertEqual(code, 0)
        data = json.loads(stdout)
        self.assertEqual(data["result"], "pass")
        self.assertIn("通过", stderr)
        self.assertNotIn(self.handle, stdout + stderr)
        self.assertNotIn("a" * 64, stdout + stderr)

    def test_parameter_error_does_not_echo_bad_input(self):
        secret = "malicious-sensitive-token"
        stderr = io.StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as error:
            main([secret])
        self.assertEqual(error.exception.code, 2)
        self.assertNotIn(secret, stderr.getvalue())

    def test_exception_details_never_echo(self):
        class BrokenSource(FakeSource):
            def collect(self, *args, **kwargs):
                raise RuntimeError("synthetic-password 203.0.113.7 eyJtoken.foo.bar")
        code, stdout, stderr = self.run_cli(factory=BrokenSource)
        self.assertEqual(code, 4)
        self.assertEqual(json.loads(stdout)["error"], "CHECK_FAILED")
        self.assertNotIn("synthetic", stdout + stderr)

    def test_all_scenarios_do_not_publish_private_source_fields(self):
        class TaggedSource(FakeSource):
            def collect(self, *args, **kwargs):
                result = super().collect(*args, **kwargs)
                result["sid"] = "synthetic-private-sid"
                result["subject"] = "synthetic-private-subject"
                result["cookie"] = "synthetic-private-cookie"
                for flows in result["flows"].values():
                    for flow in flows:
                        flow["token"] = "synthetic-private-token"
                return result
        other = self.root / "other.json"
        other.write_text(json.dumps({"schema": "xiaozhi-identity-round-v1", "request_handle": "synthetic-other-handle"}), encoding="utf-8")
        other.chmod(0o600)
        for scenario in ("login", "logout", "two-account", "idle"):
            for failing in (False, True):
                with self.subTest(scenario=scenario, failing=failing):
                    args = [scenario, "--env", str(self.root / "env.json")]
                    if scenario == "two-account":
                        args += ["--record-a", str(self.record), "--record-b", str(other)]
                    else:
                        args += ["--record", str(self.record)]
                    if scenario in ("logout", "two-account"):
                        args += ["--window-start", "1970-01-01T00:01:00Z", "--window-end", "1970-01-01T00:02:00Z"]
                    out, err = io.StringIO(), io.StringIO()
                    with patch("identity_check.envcheck.load_environment", return_value=self.env):
                        if failing:
                            with patch.object(TaggedSource, "collect", side_effect=CheckError("MYSQL_SYNTAX_ERROR")):
                                code = main(args, source_factory=TaggedSource, stdout=out, stderr=err)
                            self.assertEqual(code, 4)
                        else:
                            code = main(args, source_factory=TaggedSource, stdout=out, stderr=err)
                            self.assertIn(code, (0, 1, 3))
                    json.loads(out.getvalue())
                    for value in (self.handle, "synthetic-other-handle", "synthetic-private-sid",
                                  "synthetic-private-subject", "synthetic-private-token", "synthetic-private-cookie"):
                        self.assertNotIn(value, out.getvalue() + err.getvalue())

    def test_missing_round_safe_error(self):
        self.record.unlink()
        code, stdout, stderr = self.run_cli()
        self.assertEqual(code, 4)
        self.assertNotIn(str(self.record), stdout + stderr)

    def test_cannot_overwrite_input_or_existing_result(self):
        code, stdout, _ = self.run_cli(["--output", str(self.record)])
        self.assertEqual(code, 4)
        self.assertEqual(json.loads(stdout)["error"], "OUTPUT_INPUT_COLLISION")
        path = self.root / "previous.json"
        path.write_text("keep", encoding="utf-8")
        code, _, _ = self.run_cli(["--output", str(path)])
        self.assertEqual(code, 4)
        self.assertEqual(path.read_text(encoding="utf-8"), "keep")

    def test_idle_without_original_browser_is_insufficient(self):
        code, stdout, _ = self.run_cli(scenario="idle")
        self.assertEqual(code, 3)
        report = json.loads(stdout)
        self.assertTrue(report["flows"]["A"][0]["database_idle_expired"])

    def test_successful_capture_is_not_business_success(self):
        path = self.root / "baseline.json"
        code, stdout, _ = self.run_cli(["--phase", "baseline", "--save-state", str(path)])
        self.assertEqual(code, 3)
        self.assertTrue(json.loads(stdout)["baseline_ready"])
        state = json.loads(path.read_text(encoding="utf-8"))
        self.assertNotIn(self.handle, json.dumps(state))
        self.assertEqual(state["schema"], "xiaozhi-identity-state-v1")

    @unittest.skipUnless(os.name == "posix", "POSIX mode bits cannot be checked on Windows")
    def test_result_file_is_private(self):
        path = self.root / "result.json"
        code, _, _ = self.run_cli(["--output", str(path)])
        self.assertEqual(code, 0)
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
