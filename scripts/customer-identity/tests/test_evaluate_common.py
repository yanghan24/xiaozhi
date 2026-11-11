"""Shared contract tests: baseline capture, result shape, errors, targets."""
import json
import unittest

import identity_check.evaluate as evaluator
from tests import helpers


RESULT_KEYS = {
    "scenario", "phase", "result", "exit_code", "reasons", "notes",
    "baseline_ready", "observed_at_ms", "clock_skew_ms", "window",
    "environment", "flows", "selected_targets", "identities_distinct",
    "receipts", "log", "browser_evidence", "checks",
}


class ContractTests(unittest.TestCase):
    def test_result_shape_and_json_serialisability(self):
        data = helpers.observation(flows={"A": [helpers.flow()]})
        result = evaluator.evaluate("login", data)
        self.assertEqual(set(result), RESULT_KEYS)
        json.dumps(result)
        self.assertEqual(result["phase"], "check")
        self.assertEqual(result["environment"],
                         {"containers": "ok", "db_target": "ok",
                          "proxy_target": "ok", "schema": "ok"})

    def test_exit_codes(self):
        login_pass = evaluator.evaluate(
            "login", helpers.observation(flows={"A": [helpers.flow()]})
        )
        login_unmet = evaluator.evaluate(
            "login",
            helpers.observation(flows={"A": [helpers.flow(
                status="IDENTITY_FAILED", identity_present=False,
                authorization="NONE",
            )]}),
        )
        login_insufficient = evaluator.evaluate(
            "login", helpers.observation(flows={"A": []})
        )
        login_error = evaluator.evaluate(
            "login",
            helpers.observation(
                flows={"A": [helpers.flow()]},
                environment={"schema": "error"},
            ),
        )
        self.assertEqual(
            [(item["result"], item["exit_code"]) for item in
             (login_pass, login_unmet, login_insufficient, login_error)],
            [("pass", 0), ("unmet", 1), ("insufficient", 3), ("error", 4)],
        )

    def test_invalid_scenario_phase_and_observation_are_safe_errors(self):
        for scenario, phase in (("unknown", "check"), ("login", "later"),
                                (["login"], "check")):
            with self.subTest(scenario=scenario, phase=phase):
                result = evaluator.evaluate(scenario, helpers.observation(), phase=phase)
                self.assertEqual(result["result"], "error")
                self.assertEqual(result["exit_code"], 4)
                expected_scenario = (scenario if isinstance(scenario, str)
                                     and scenario in evaluator.SCENARIOS else "unknown")
                expected_phase = (phase if isinstance(phase, str)
                                  and phase in evaluator.PHASES else "unknown")
                self.assertEqual(result["scenario"], expected_scenario)
                self.assertEqual(result["phase"], expected_phase)
        result = evaluator.evaluate("login", "not a mapping")
        self.assertEqual(result["result"], "error")
        self.assertIn("OBSERVATION_INVALID", result["reasons"])

    def test_missing_observation_time_is_an_error(self):
        data = helpers.observation(flows={"A": [helpers.flow()]})
        del data["observed_at_ms"]
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "error")
        self.assertIn("OBSERVATION_INVALID", result["reasons"])

    def test_invalid_targets_are_an_execution_error(self):
        result = evaluator.evaluate(
            "login", helpers.observation(flows={"A": [helpers.flow()]}),
            targets={"A": ["F1"]},
        )
        self.assertEqual(result["result"], "error")
        self.assertIn("TARGETS_INVALID", result["reasons"])

    def test_all_environment_failures_are_reported(self):
        data = helpers.observation(
            flows={"A": [helpers.flow()]},
            environment={
                "containers": "error", "db_target": "error",
                "proxy_target": "error", "schema": "error",
            },
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "error")
        for code in ("ENVIRONMENT_CONTAINERS", "ENVIRONMENT_DB_TARGET",
                     "ENVIRONMENT_PROXY_TARGET", "ENVIRONMENT_SCHEMA"):
            self.assertIn(code, result["reasons"])
        self.assertEqual(len(result["flows"]["A"]), 1)

    def test_creation_order_is_never_public(self):
        data = helpers.observation(flows={"A": [helpers.flow(creation_order=41)]})
        result = evaluator.evaluate("login", data)
        self.assertNotIn("creation_order", json.dumps(result))

    def test_baseline_phase_is_never_a_business_pass(self):
        data = helpers.observation(flows={"A": [helpers.flow()]})
        result = evaluator.evaluate("login", data, phase="baseline")
        self.assertEqual(result["result"], "insufficient")
        self.assertEqual(result["exit_code"], 3)
        self.assertIn("BASELINE_ONLY", result["reasons"])
        self.assertTrue(result["baseline_ready"])

    def test_logout_baseline_capture(self):
        data = helpers.observation(flows={"A": [helpers.flow()]})
        result = evaluator.evaluate("logout", data, phase="baseline")
        self.assertEqual(result["result"], "insufficient")
        self.assertTrue(result["baseline_ready"])
        self.assertEqual(result["selected_targets"], {"A": "F1"})
        self.assertIn("BASELINE_ONLY", result["reasons"])

    def test_logout_baseline_capture_requires_a_verified_target(self):
        data = helpers.observation(
            flows={"A": [helpers.flow(
                status="AWAITING_IDENTITY", identity_present=False,
                authorization="NONE",
            )]}
        )
        result = evaluator.evaluate("logout", data, phase="baseline")
        self.assertEqual(result["result"], "insufficient")
        self.assertFalse(result["baseline_ready"])
        self.assertIn("NO_IDENTITY_TARGET", result["reasons"])

    def test_two_account_baseline_capture(self):
        data = helpers.observation(
            flows={
                "A": [helpers.flow("F1", 1)],
                "B": [helpers.flow("F2", 2)],
            },
            identity_pairs=[
                {"a_label": "F1", "b_label": "F2", "distinct": True}
            ],
        )
        result = evaluator.evaluate("two-account", data, phase="baseline")
        self.assertEqual(result["result"], "insufficient")
        self.assertTrue(result["baseline_ready"])
        self.assertTrue(result["identities_distinct"])
        self.assertEqual(result["selected_targets"], {"A": "F1", "B": "F2"})
        self.assertIn("BASELINE_ONLY", result["reasons"])

    def test_two_account_baseline_capture_requires_distinct_accounts(self):
        data = helpers.observation(
            flows={
                "A": [helpers.flow("F1", 1)],
                "B": [helpers.flow("F2", 2)],
            },
            identity_pairs=[
                {"a_label": "F1", "b_label": "F2", "distinct": False}
            ],
        )
        result = evaluator.evaluate("two-account", data, phase="baseline")
        self.assertEqual(result["result"], "insufficient")
        self.assertFalse(result["baseline_ready"])
        self.assertIn("IDENTITIES_NOT_DISTINCT", result["reasons"])

    def test_baseline_phase_environment_error(self):
        data = helpers.observation(
            flows={"A": [helpers.flow()]},
            environment={"db_target": "error"},
        )
        result = evaluator.evaluate("logout", data, phase="baseline")
        self.assertEqual(result["result"], "error")
        self.assertEqual(result["exit_code"], 4)

    def test_target_labels_are_safe_and_ambiguous_labels_are_rejected(self):
        first = helpers.flow("F1", 1)
        second = helpers.flow("F1", 2)
        data = helpers.observation(flows={"A": [first, second]})
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("TARGET_AMBIGUOUS", result["reasons"])
        self.assertEqual(result["selected_targets"], {})

    def test_bad_flow_label_falls_back_to_position(self):
        data = helpers.observation(
            flows={"A": [helpers.flow("bad label <script>")]}
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["selected_targets"], {"A": "F1"})
        self.assertNotIn("<script>", json.dumps(result))

    def test_window_open_still_allows_a_conclusive_pass(self):
        flow = helpers.flow(
            "F1", 1, "SESSION_ENDED", matched_before_window=0,
            matched_in_window=1,
            matched_received_at_ms=[helpers.WINDOW["start_ms"] + 1],
        )
        data = helpers.observation(
            flows={"A": [flow]},
            window=dict(helpers.WINDOW),
            observed_at_ms=helpers.WINDOW["start_ms"] + 50000,
            receipts={"issuer_total": 1, "issuer_in_window": 1},
            log={"status": "complete", "post_204_in_window": 1,
                 "post_400_in_window": 0, "boundary_lines": 0},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "pass")
        self.assertIn("WINDOW_OPEN", result["notes"])
        self.assertFalse(result["window"]["closed"])

    def test_baseline_reasons_are_safe_codes(self):
        data = helpers.observation(
            flows={"A": [helpers.flow()]},
            baseline_reasons=[
                "BASELINE_RECORD_CHANGED", "NOT_A_PUBLIC_CODE",
                "raw leak with spaces",
            ],
        )
        result = evaluator.evaluate("login", data)
        self.assertIn("BASELINE_RECORD_CHANGED", result["notes"])
        self.assertNotIn("NOT_A_PUBLIC_CODE", result["notes"])
        self.assertNotIn("raw leak with spaces", json.dumps(result))


if __name__ == "__main__":
    unittest.main()
