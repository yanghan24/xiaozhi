import io
import json
import os
import subprocess
import time
import unittest
from pathlib import Path
from unittest import mock

from identity_check import envcheck, source, sql
from identity_check.errors import CheckError
from identity_check.records import RoundInput


NOW = time.time_ns() // 1000000
HANDLE_A = "A" * 16
HANDLE_B = "B" * 16
ENV_SUMMARY = {"containers": "ok", "db_target": "ok",
               "proxy_target": "ok", "schema": "ok"}


def _env():
    return {
        "schema": envcheck.ENV_SCHEMA,
        "issuer": "https://issuer.example/oidc",
        "record_root": "/home/test/records",
        "db": {"container": "db1", "database": "dbname",
               "network_alias": "mysql"},
        "identity_container": "identity1",
        "entry": {"container": "entry1", "notify_log": "/var/log/nginx/notify.log",
                  "config_file": "/srv/entry/nginx.conf", "marker": None},
    }


def _record(label, handle):
    return RoundInput(label, handle, "f" * 64, Path("/private/record.json"))


def _flow(record="A", order=1, status="IDENTITY_VERIFIED", identity=True,
          sid=True, authorization="ACTIVE", before=0, in_window=0,
          created=None, expires=None, issued=None, activity=None,
          issuer_matches=1):
    return {
        "kind": "flow", "record": record, "order": order, "status": status,
        "created_at_ms": NOW - 50000 if created is None else created,
        "expires_at_ms": NOW + 600000 if expires is None else expires,
        "identity_issued_at_ms": NOW - 40000 if issued is None else issued,
        "sid_present": 1 if sid else 0,
        "identity_present": 1 if identity else 0,
        "issuer_matches": issuer_matches,
        "authorization": authorization,
        "last_activity_ms": NOW - 30000 if activity is None else activity,
        "matched_before_window": before,
        "matched_in_window": in_window,
    }


def _rows(flows=None, receipts=None, matched=None, pairs=None, receipt_window=None,
          observed=None):
    result = [{"kind": "observed", "value": NOW if observed is None else observed}]
    result.extend(flows if flows is not None else [_flow()])
    result.append(receipts if receipts is not None else {
        "kind": "receipts", "issuer_total": 5, "issuer_before_window": 1,
        "issuer_in_window": 1, "other_issuer_total": 32})
    result.append({"kind": "since_baseline", "value": 2})
    result.extend(matched or [])
    result.extend({"kind": "receipt_window", "received_at_ms": NOW - 2000}
                  for _ in range(receipt_window or 0))
    result.extend(pairs or [])
    return result


class Result(object):
    def __init__(self, returncode=0, stdout=b"", stderr=b""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


class SourceRunner(object):
    def __init__(self, rows, log_text=None, stat=(12345, 456), stat_missing=False,
                 rotated_sizes=None, cat_overflow=False, cat_returncode=0,
                 mysql_returncode=0, mysql_stderr=b"", mysql_stdout=None):
        self.rows = rows
        self.log_text = log_text if log_text is not None else (
            "2026-11-10T01:31:10+00:00 203.0.113.7 POST 204 204\n"
            "2026-11-10T01:31:11+00:00 2001:db8::1 POST 400 400\n")
        self.stat = stat
        self.stat_missing = stat_missing
        self.rotated_sizes = rotated_sizes or {}
        self.cat_overflow = cat_overflow
        self.cat_returncode = cat_returncode
        self.mysql_returncode = mysql_returncode
        self.mysql_stderr = mysql_stderr
        self.mysql_stdout = mysql_stdout
        self.mysql_calls = []
        self.run_calls = []
        self.cat_calls = 0
        self.ids = {"db1": "a" * 64, "identity1": "b" * 64, "entry1": "c" * 64}

    def begin(self):
        pass

    def mysql(self, script, container, timeout=None, max_stdout=None):
        self.mysql_calls.append((script, container))
        if "server_uuid_digest" in script:
            return Result(0, (json.dumps({"kind": "server_uuid_digest",
                                          "value": "d" * 64}) + "\n").encode())
        if self.mysql_stdout is not None:
            return Result(self.mysql_returncode, self.mysql_stdout, self.mysql_stderr)
        raw = "\n".join(json.dumps(row) for row in self.rows) + "\n"
        return Result(self.mysql_returncode, raw.encode("utf-8"), self.mysql_stderr)

    def run(self, argv, input_text=None, timeout=None, max_stdout=None):
        self.run_calls.append(list(argv))
        if argv[:2] == ["docker", "inspect"] and argv[3] == "{{.Id}}":
            return Result(0, (self.ids[argv[4]] + "\n").encode())
        if argv[:2] == ["docker", "exec"]:
            if argv[3:5] == ["stat", "-c"]:
                path = argv[6]
                if path != "/var/log/nginx/notify.log":
                    size = self.rotated_sizes.get(path)
                    if size is None:
                        return Result(1, b"")
                    return Result(0, ("99 %d\n" % size).encode())
                if self.stat_missing:
                    return Result(1, b"")
                return Result(0, ("%d %d\n" % self.stat).encode())
            if argv[3] == "cat":
                self.cat_calls += 1
                if self.cat_overflow:
                    raise source.StreamOverflow()
                if self.cat_returncode:
                    return Result(self.cat_returncode, b"")
                return Result(0, self.log_text.encode("utf-8"))
        return Result(1, b"")


def _collect(runner, records=None, configured=True, **kwargs):
    source_object = source.ServerSource(_env())
    source_object.runner = runner
    if records is None:
        records = {"A": _record("A", HANDLE_A)}
    with mock.patch.object(source.envcheck, "check_environment",
                           return_value=dict(ENV_SUMMARY)), \
            mock.patch.object(source.envcheck, "entry_log_configured",
                              return_value=configured):
        return source_object.collect(records, **kwargs)


class CollectLoginTest(unittest.TestCase):
    def test_orders_flows_and_normalizes_fields(self):
        runner = SourceRunner(_rows(flows=[
            _flow(order=2, status="SESSION_ENDED", in_window=2),
            _flow(order=1, status="AWAITING_IDENTITY", identity=False, sid=False,
                  authorization="NONE", in_window=1),
        ]))
        observation = _collect(runner)
        self.assertEqual([flow["label"] for flow in observation["flows"]["A"]],
                         ["F1", "F2"])
        first, second = observation["flows"]["A"]
        self.assertEqual(first["stored_status"], "AWAITING_IDENTITY")
        self.assertFalse(first["identity_present"])
        self.assertFalse(first["sid_present"])
        self.assertEqual(first["authorization"], "NONE")
        self.assertEqual(second["stored_status"], "SESSION_ENDED")
        self.assertTrue(second["identity_present"])
        self.assertEqual(second["matched_in_window"], 2)
        self.assertEqual(observation["window"], None)
        self.assertIsNone(observation["identities_distinct"])
        self.assertIsNone(observation["receipts"]["since_baseline"])
        self.assertFalse(observation["truncated"])
        self.assertEqual(observation["environment"], ENV_SUMMARY)
        self.assertRegex(observation["environment_fingerprint"], r"^[a-f0-9]{64}$")
        capture = observation["log_capture"]
        self.assertEqual(set(capture), {"configured", "missing", "text", "inode",
                                        "size", "overflow", "rotated",
                                        "changed_during_read"})
        self.assertTrue(capture["configured"])
        self.assertFalse(capture["missing"])
        self.assertIn("<ip>", capture["text"])
        self.assertNotIn("203.0.113.7", capture["text"])
        self.assertNotIn("2001:db8::1", capture["text"])
        self.assertIn("2026-11-10T01:31:10+00:00", capture["text"])
        self.assertEqual((capture["inode"], capture["size"]), (12345, 456))
        self.assertEqual(runner.cat_calls, 1)
        data_script = runner.mysql_calls[-1][0]
        self.assertNotIn(HANDLE_A, data_script)
        self.assertIn("LIMIT 51", data_script)
        self.assertNotIn("a" * 64, json.dumps(observation))
        self.assertNotIn("b" * 64, json.dumps(observation))
        self.assertNotIn("d" * 64, json.dumps(observation))
        self.assertNotIn(HANDLE_A, repr(source.ServerSource(_env())))

    def test_two_account_pair_matrix_and_distinct(self):
        flows = [
            _flow(record="A", order=10),
            _flow(record="B", order=20, status="SESSION_ENDED"),
        ]
        pairs = [{"kind": "pair", "a_order": 10, "b_order": 20, "distinct": 1}]
        observation = _collect(
            SourceRunner(_rows(flows=flows, pairs=pairs)),
            records={"A": _record("A", HANDLE_A), "B": _record("B", HANDLE_B)})
        self.assertEqual(observation["identity_pairs"],
                         [{"a_label": "F1", "b_label": "F1", "distinct": True}])
        self.assertTrue(observation["identities_distinct"])

        pairs = [{"kind": "pair", "a_order": 10, "b_order": 20, "distinct": 0}]
        observation = _collect(
            SourceRunner(_rows(flows=flows, pairs=pairs)),
            records={"A": _record("A", HANDLE_A), "B": _record("B", HANDLE_B)})
        self.assertFalse(observation["identities_distinct"])

        ambiguous = flows + [_flow(record="A", order=11)]
        observation = _collect(
            SourceRunner(_rows(flows=ambiguous, pairs=pairs)),
            records={"A": _record("A", HANDLE_A), "B": _record("B", HANDLE_B)})
        self.assertIsNone(observation["identities_distinct"])

    def test_flow_and_receipt_window_limits_mark_truncation(self):
        flows = [_flow(order=index) for index in range(1, 52)]
        observation = _collect(SourceRunner(_rows(flows=flows)))
        self.assertEqual(len(observation["flows"]["A"]), 50)
        self.assertEqual(observation["flows"]["A"][-1]["label"], "F50")
        self.assertTrue(observation["truncated"])

        observation = _collect(SourceRunner(_rows(receipt_window=501)),
                               start_ms=NOW - 60000, end_ms=NOW - 1000)
        self.assertTrue(observation["truncated"])

    def test_matched_timestamps_are_bounded_and_labelled(self):
        matched = [
            {"kind": "matched", "record": "A", "order": 2,
             "received_at_ms": NOW - 1000},
            {"kind": "matched", "record": "A", "order": 1,
             "received_at_ms": NOW - 3000},
            {"kind": "matched", "record": "A", "order": 1,
             "received_at_ms": NOW - 2000},
        ]
        observation = _collect(SourceRunner(_rows(
            flows=[_flow(order=1, in_window=2), _flow(order=2, in_window=1)],
            matched=matched)), start_ms=NOW - 60000, end_ms=NOW - 1000)
        self.assertEqual(observation["flows"]["A"][0]["matched_received_at_ms"],
                         [NOW - 3000, NOW - 2000])
        self.assertEqual(observation["flows"]["A"][1]["matched_received_at_ms"],
                         [NOW - 1000])
        self.assertEqual(observation["window"], {"start_ms": NOW - 60000,
                                                 "end_ms": NOW - 1000})

    def test_matched_limit_marks_truncated(self):
        matched = [{"kind": "matched", "record": "A", "order": 1,
                    "received_at_ms": NOW - 1000 - index}
                   for index in range(501)]
        observation = _collect(SourceRunner(_rows(
            flows=[_flow(order=1, in_window=501)], matched=matched)),
            start_ms=NOW - 60000, end_ms=NOW - 1000)
        self.assertTrue(observation["truncated"])
        self.assertEqual(len(observation["flows"]["A"][0]["matched_received_at_ms"]), 500)


class BaselineAndLogTest(unittest.TestCase):
    def test_baseline_delta_and_log_rotation(self):
        baseline = {"observed_at_ms": NOW - 120000, "issuer_total": 3,
                    "log_state": {"inode": 999, "size": 100}}
        observation = _collect(SourceRunner(_rows()), baseline=baseline,
                               start_ms=NOW - 60000, end_ms=NOW - 1000)
        self.assertEqual(observation["receipts"]["since_baseline"], 2)
        self.assertTrue(observation["log_capture"]["rotated"])

    def test_since_baseline_uses_received_after_baseline_not_total_delta(self):
        baseline = {"observed_at_ms": NOW - 120000, "issuer_total": 3,
                    "log_state": {"inode": 12345, "size": 456}}
        rows = _rows()
        rows[-1] = {"kind": "since_baseline", "value": 1}
        observation = _collect(SourceRunner(rows), baseline=baseline,
                               start_ms=NOW - 60000, end_ms=NOW - 1000)
        self.assertEqual(observation["receipts"]["issuer_total"], 5)
        self.assertEqual(observation["receipts"]["since_baseline"], 1)

    def test_baseline_query_failure_is_invalid_response(self):
        baseline = {"observed_at_ms": NOW - 120000, "issuer_total": 3,
                    "log_state": {"inode": 1, "size": 1}}
        rows = [row for row in _rows() if row.get("kind") != "since_baseline"]
        with self.assertRaises(CheckError) as caught:
            _collect(SourceRunner(rows), baseline=baseline)
        self.assertEqual(caught.exception.code, "DATABASE_RESPONSE_INVALID")

    def test_baseline_window_chronology_is_left_to_evaluator(self):
        baseline = {"observed_at_ms": NOW - 1000, "issuer_total": 1,
                    "log_state": {"inode": 12345, "size": 456}}
        observation = _collect(SourceRunner(_rows()), baseline=baseline,
                               start_ms=NOW - 60000, end_ms=NOW - 1000)
        self.assertEqual(observation["receipts"]["since_baseline"], 2)

    def test_invalid_baseline_shape_fails_closed(self):
        for baseline in (
                {"observed_at_ms": NOW - 120000, "issuer_total": -1,
                 "log_state": {"inode": 1, "size": 1}},
                {"observed_at_ms": NOW - 120000, "issuer_total": 1,
                 "log_state": "bad"},
                "not a dict"):
            with self.subTest(baseline=baseline):
                with self.assertRaises(CheckError) as caught:
                    _collect(SourceRunner(_rows()), baseline=baseline,
                             start_ms=NOW - 60000, end_ms=NOW - 1000)
                self.assertEqual(caught.exception.code, "BASELINE_INVALID")

    def test_missing_log_is_evidence_not_execution_error(self):
        observation = _collect(SourceRunner(_rows(), stat_missing=True))
        capture = observation["log_capture"]
        self.assertTrue(capture["configured"])
        self.assertTrue(capture["missing"])
        self.assertEqual(capture["text"], "")
        self.assertIsNone(capture["inode"])
        self.assertEqual(capture["size"], 0)
        self.assertFalse(capture["overflow"])

    def test_log_overflow_discards_text(self):
        observation = _collect(SourceRunner(_rows(), cat_overflow=True))
        capture = observation["log_capture"]
        self.assertTrue(capture["overflow"])
        self.assertEqual(capture["text"], "")
        self.assertFalse(capture["missing"])

        observation = _collect(SourceRunner(
            _rows(), stat=(1, source.LOG_STDOUT_LIMIT + 1)))
        capture = observation["log_capture"]
        self.assertTrue(capture["overflow"])
        self.assertEqual(capture["text"], "")

    def test_unconfigured_log_is_not_read(self):
        runner = SourceRunner(_rows())
        observation = _collect(runner, configured=False)
        capture = observation["log_capture"]
        self.assertFalse(capture["configured"])
        self.assertFalse(capture["missing"])
        self.assertEqual(capture["text"], "")
        self.assertFalse(any(call[:2] == ["docker", "exec"] for call in runner.run_calls))

    def test_log_redaction_keeps_iso_time(self):
        text = ("2026-11-10T01:31:10+00:00 203.0.113.7 POST 204 204\n"
                "2026-11-10T01:31:11+00:00 [fe80::1] POST 400 400\n"
                "2026-11-10T01:31:12+00:00 2001:db8::1 POST 204 204\n")
        redacted = source.redact_log_text(text)
        self.assertNotIn("203.0.113.7", redacted)
        self.assertNotIn("fe80::1", redacted)
        self.assertNotIn("2001:db8::1", redacted)
        self.assertIn("2026-11-10T01:31:10+00:00", redacted)
        self.assertIn("2026-11-10T01:31:11+00:00", redacted)


class FailureTest(unittest.TestCase):
    def test_mysql_error_never_echoes_raw_output(self):
        runner = SourceRunner(_rows(), mysql_returncode=1,
                              mysql_stderr=b"ERROR 1064 (42000): handle " + HANDLE_A.encode())
        with self.assertRaises(CheckError) as caught:
            _collect(runner)
        self.assertEqual(caught.exception.code, "MYSQL_SYNTAX_ERROR")
        rendered = str(caught.exception) + repr(caught.exception)
        self.assertNotIn(HANDLE_A, rendered)
        self.assertNotIn("1064", rendered)

    def test_malformed_response_and_unknown_enums_fail_closed(self):
        with self.assertRaises(CheckError) as caught:
            _collect(SourceRunner(_rows(), mysql_stdout=b"not json " + HANDLE_A.encode()))
        self.assertEqual(caught.exception.code, "DATABASE_RESPONSE_INVALID")
        self.assertNotIn(HANDLE_A, str(caught.exception))

        with self.assertRaises(CheckError) as caught:
            _collect(SourceRunner(_rows(flows=[_flow(status="SENSITIVE")])))
        self.assertEqual(caught.exception.code, "STATUS_UNKNOWN")
        with self.assertRaises(CheckError) as caught:
            _collect(SourceRunner(_rows(flows=[_flow(authorization="SENSITIVE")])))
        self.assertEqual(caught.exception.code, "AUTHORIZATION_UNKNOWN")

    def test_identity_flow_from_other_issuer_fails_closed(self):
        with self.assertRaises(CheckError) as caught:
            _collect(SourceRunner(_rows(flows=[_flow(issuer_matches=0)])))
        self.assertEqual(caught.exception.code, "ENV_ISSUER_MISMATCH")
        observation = _collect(SourceRunner(_rows(
            flows=[_flow(identity=False, issuer_matches=0)])))
        self.assertFalse(observation["flows"]["A"][0]["identity_present"])

    def test_records_and_window_validation(self):
        cases = (
            ({}, None),
            ({"B": _record("B", HANDLE_B)}, None),
            ({"A": _record("B", HANDLE_A)}, None),
            ({"A": _record("A", "invalid handle")}, None),
            ({"A": _record("A", HANDLE_A), "C": _record("C", HANDLE_B)}, None),
            ({"A": _record("A", HANDLE_A), "B": _record("B", HANDLE_A)}, None),
        )
        for records, _ in cases:
            with self.subTest(records=records):
                with self.assertRaises(CheckError):
                    _collect(SourceRunner(_rows()), records=records)
        for start, end in ((1, None), (10, 10), (-1, 10), (1.5, 2)):
            with self.subTest(start=start):
                with self.assertRaises(CheckError) as caught:
                    _collect(SourceRunner(_rows()), start_ms=start, end_ms=end)
                self.assertEqual(caught.exception.code, "WINDOW_INVALID")


class LocalRunnerTest(unittest.TestCase):
    def test_mysql_command_is_fixed_and_sql_uses_stdin(self):
        class CaptureRunner(source.LocalRunner):
            def __init__(self):
                super().__init__(timeout=20)
                self.called = None

            def run(self, argv, input_text=None, timeout=None, max_stdout=None):
                self.called = (argv, input_text, timeout, max_stdout)
                return source.RunResult(0, b"", b"")

        runner = CaptureRunner()
        script = "SELECT 1;"
        result = runner.mysql(script, "db1")
        self.assertEqual(result.returncode, 0)
        argv, input_text, timeout, max_stdout = runner.called
        self.assertEqual(argv, ["docker", "exec", "-i", "db1", "sh", "-c",
                                source.MYSQL_COMMAND])
        self.assertEqual(input_text, script)
        self.assertNotIn("--password", " ".join(argv))
        self.assertNotIn("MYSQL_PASSWORD=", " ".join(argv))
        self.assertIn("MYSQL_USER", source.MYSQL_COMMAND)
        with self.assertRaises(CheckError) as caught:
            runner.mysql(script, "bad container")
        self.assertEqual(caught.exception.code, "CONTAINER_INVALID")
        with self.assertRaises(CheckError) as caught:
            runner.mysql("DELETE FROM t;", "db1")
        self.assertEqual(caught.exception.code, "SQL_READ_ONLY_GUARD")

    def test_base_mysql_accepts_envcheck_max_stdout(self):
        runner = source.LocalRunner(timeout=20)
        seen = {}

        def fake_run(argv, input_text=None, timeout=None, max_stdout=None):
            seen["argv"], seen["input"], seen["max_stdout"] = argv, input_text, max_stdout
            return source.RunResult(0, b"", b"")

        runner.run = fake_run
        result = runner.mysql("SELECT 1;", "db1", timeout=5, max_stdout=1234)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(seen["input"], "SELECT 1;")
        self.assertEqual(seen["max_stdout"], 1234)
        self.assertEqual(seen["argv"][3:6], ["db1", "sh", "-c"])

    def test_local_runner_uses_no_shell_and_terminates_on_overflow_and_timeout(self):
        instances = []

        class FakeProcess(object):
            def __init__(self, argv, stdin=None, stdout=None, stderr=None, shell=None):
                self.argv = argv
                self.shell = shell
                self.killed = False
                self.returncode = 0
                self.stdout = io.BytesIO(self.stdout_data)
                self.stderr = io.BytesIO(b"warning")
                self.stdin = io.BytesIO()
                instances.append(self)

            stdout_data = b"ok"

            def wait(self, timeout=None):
                if self.timeout_error:
                    raise subprocess.TimeoutExpired(self.argv, timeout)
                return 0

            timeout_error = False

            def kill(self):
                self.killed = True

        with mock.patch.object(source.subprocess, "Popen", FakeProcess):
            result = source.LocalRunner(timeout=5).run(["docker", "version"],
                                                       max_stdout=64)
        self.assertEqual(result.stdout, b"ok")
        self.assertIs(instances[-1].shell, False)
        self.assertEqual(instances[-1].argv, ["docker", "version"])

        FakeProcess.stdout_data = b"x" * 100
        FakeProcess.timeout_error = False
        with mock.patch.object(source.subprocess, "Popen", FakeProcess):
            with self.assertRaises(source.StreamOverflow):
                source.LocalRunner(timeout=5).run(["docker", "version"], max_stdout=10)
        self.assertTrue(instances[-1].killed)

        FakeProcess.stdout_data = b"ok"
        FakeProcess.timeout_error = True
        with mock.patch.object(source.subprocess, "Popen", FakeProcess):
            with self.assertRaises(CheckError) as caught:
                source.LocalRunner(timeout=5).run(["docker", "version"])
        self.assertEqual(caught.exception.code, "COMMAND_TIMEOUT")
        self.assertTrue(instances[-1].killed)

    def test_local_runner_deadline_and_input_bounds(self):
        with mock.patch.object(source.subprocess, "Popen") as popen:
            runner = source.LocalRunner(timeout=5)
            runner._deadline = time.monotonic() - 1
            with self.assertRaises(CheckError) as caught:
                runner.run(["docker", "version"])
            self.assertEqual(caught.exception.code, "TOTAL_DEADLINE_EXCEEDED")
            self.assertFalse(popen.called)

            runner = source.LocalRunner(timeout=5)
            with self.assertRaises(CheckError) as caught:
                runner.run(["docker", "version"],
                           input_text="x" * (source.MAX_INPUT + 1))
            self.assertEqual(caught.exception.code, "COMMAND_INPUT_TOO_LARGE")
            self.assertFalse(popen.called)


if __name__ == "__main__":
    unittest.main()
