import json
from pathlib import Path
import tempfile
import unittest

from identity_check.errors import CheckError
from identity_check import output


class RedactionTest(unittest.TestCase):
    def test_public_fields_are_allowlisted(self):
        private = "synthetic-subject-never-output"
        observation = {"observed_at_ms": 1000000, "environment": {}, "flows": {},
                       "subject": private, "environment_fingerprint": "a" * 64}
        result = {"result": "insufficient", "checks": [], "notes": [], "subject": private}
        rendered = output.serialize(output.build_report("login", "check", observation, result))
        self.assertNotIn(private, rendered)
        self.assertNotIn("a" * 64, rendered)

    def test_synthetic_sensitive_values_and_encoded_handle_fail_closed(self):
        values = ("eyJhbGciOiJIUzI1NiJ9.synthetic.signature", "a" * 64, "Q" * 43,
                  "203.0.113.7", "Bearer synthetic", "password=synthetic", "secret=synthetic")
        for value in values:
            with self.subTest(value_kind=values.index(value)):
                with self.assertRaises(CheckError):
                    output.guard(json.dumps({"data": value}))
        handle = "synthetic-handle-12345"
        for leaked in (handle, handle.encode().hex(), handle.encode().hex().upper()):
            with self.assertRaises(CheckError):
                output.guard(leaked, [handle])

    def test_freeform_check_values_are_not_copied(self):
        observation = {"observed_at_ms": 1000, "environment": {}, "flows": {}}
        result = {"result": "unmet", "checks": [{"id": "target_unique", "kind": "criterion", "result": "unmet",
                    "message": "synthetic cookie", "value": "synthetic token"}], "notes": []}
        rendered = output.serialize(output.build_report("login", "check", observation, result))
        self.assertNotIn("synthetic", rendered)

    def test_arbitrary_short_reason_or_note_is_not_a_public_code(self):
        observation = {"observed_at_ms": 1000, "environment": {}, "flows": {},
                       "log": {"status": "incomplete", "reasons": ["RAWSUBJECT01", "customer_alice"]}}
        with self.assertRaises(CheckError):
            output.build_report("logout", "check", observation, {"result": "insufficient", "notes": []})
        for field in ("notes", "checks"):
            result = {"result": "insufficient", "notes": [], "checks": []}
            if field == "notes":
                result[field] = ["RAWSUBJECT01"]
            else:
                result[field] = [{"id": "receipt_matched", "kind": "criterion", "result": "insufficient", "reason": "RAWSUBJECT01"}]
            with self.assertRaises(CheckError):
                output.build_report("login", "check", {"observed_at_ms": 1000, "flows": {}}, result)

    def test_all_synthetic_evaluation_results_serialize_with_public_codes(self):
        from identity_check import evaluate
        from tests.generate_examples import observation
        for scenario in ("login", "logout", "two-account", "idle"):
            source = observation(scenario)
            for phase in ("check", "baseline"):
                result = evaluate.evaluate(scenario, source, phase=phase)
                output.serialize(output.build_report(scenario, phase, source, result))
        for log_reason in ("RAWSUBJECT01", "customer_alice"):
            with self.subTest(log_reason=log_reason):
                source = observation("logout")
                source["log"]["reasons"] = [log_reason]
                result = evaluate.evaluate("logout", source)
                self.assertNotIn(log_reason, json.dumps(result))

    def test_unknown_stored_status_cannot_escape(self):
        observation = {"observed_at_ms": 1000, "flows": {"A": [{"stored_status": "sensitive-value", "authorization": "ACTIVE"}]}}
        with self.assertRaises(CheckError):
            output.build_report("login", "check", observation, {"result": "pass"})

    def test_error_code_never_replays_exception(self):
        error = CheckError("sensitive invalid diagnostic")
        rendered = output.serialize(output.error_report(error.code))
        self.assertNotIn("sensitive", rendered)
        self.assertEqual(json.loads(rendered)["error"], "CHECK_FAILED")

    def test_private_files_are_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            output.write_private(path, "original")
            with self.assertRaises(CheckError):
                output.write_private(path, "replacement")
            self.assertEqual(path.read_text(encoding="utf-8"), "original\n")
