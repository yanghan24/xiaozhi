"""Login scenario evaluation tests (T02, T03 and status boundaries)."""
import json
import unittest

import identity_check.evaluate as evaluator
from tests import helpers


class LoginTests(unittest.TestCase):
    def test_verified_and_active_passes(self):
        data = helpers.observation(
            flows={"A": [helpers.flow()]},
            observed_at_ms=helpers.BASE_MS + 1000000,
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["selected_targets"], {"A": "F1"})
        self.assertEqual(len(result["flows"]["A"]), 1)
        self.assertTrue(result["flows"]["A"][0]["target"])
        self.assertEqual(result["reasons"], [])

    def test_three_flows_selects_the_only_identity_bearing_flow(self):
        data = helpers.observation(
            flows={"A": [
                helpers.flow("F1", 1, "IDENTITY_VERIFIED", authorization="ACTIVE"),
                helpers.flow("F2", 2, "AWAITING_IDENTITY", identity_present=False,
                             sid_present=False, authorization="NONE"),
                helpers.flow("F3", 3, "IDENTITY_REFUSED", identity_present=False,
                             sid_present=False, authorization="NONE"),
            ]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["selected_targets"], {"A": "F1"})
        self.assertEqual([flow["label"] for flow in result["flows"]["A"]],
                         ["F1", "F2", "F3"])
        self.assertTrue(result["flows"]["A"][0]["target"])
        self.assertFalse(result["flows"]["A"][1]["target"])
        self.assertFalse(result["flows"]["A"][2]["target"])

    def test_two_verified_flows_are_ambiguous(self):
        data = helpers.observation(
            flows={"A": [
                helpers.flow("F1", 1),
                helpers.flow("F2", 2),
            ]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertEqual(result["exit_code"], 3)
        self.assertIn("TARGET_AMBIGUOUS", result["reasons"])
        self.assertEqual(result["selected_targets"], {})
        self.assertFalse(any(flow["target"] for flow in result["flows"]["A"]))

    def test_explicit_target_resolves_multiple_verified_flows(self):
        data = helpers.observation(
            flows={"A": [
                helpers.flow("F1", 1),
                helpers.flow("F2", 2),
            ]},
        )
        result = evaluator.evaluate("login", data, targets={"A": "F2"})
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["selected_targets"], {"A": "F2"})
        self.assertFalse(result["flows"]["A"][0]["target"])
        self.assertTrue(result["flows"]["A"][1]["target"])

    def test_explicit_missing_target_is_insufficient(self):
        data = helpers.observation(flows={"A": [helpers.flow("F1")]})
        result = evaluator.evaluate("login", data, targets={"A": "F9"})
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("TARGET_NOT_FOUND", result["reasons"])
        self.assertEqual(result["selected_targets"], {})

    def test_single_failed_flow_is_evaluated(self):
        data = helpers.observation(
            flows={"A": [helpers.flow(
                status="IDENTITY_FAILED", identity_present=False,
                sid_present=False, authorization="NONE",
            )]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "unmet")
        self.assertEqual(result["exit_code"], 1)
        self.assertIn("IDENTITY_FAILED", result["reasons"])
        self.assertEqual(result["selected_targets"], {"A": "F1"})

    def test_expired_pending_flow_is_unmet(self):
        data = helpers.observation(
            observed_at_ms=helpers.BASE_MS + 400000,
            flows={"A": [helpers.flow(
                status="AWAITING_IDENTITY", expires_at_ms=helpers.BASE_MS + 400000,
                identity_present=False, sid_present=False, authorization="NONE",
            )]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "unmet")
        self.assertIn("FLOW_EXPIRED", result["reasons"])
        self.assertEqual(result["flows"]["A"][0]["effective_status"], "FLOW_EXPIRED")

    def test_pending_flow_is_insufficient(self):
        data = helpers.observation(
            observed_at_ms=helpers.BASE_MS + 100000,
            flows={"A": [helpers.flow(
                status="AWAITING_IDENTITY", expires_at_ms=helpers.BASE_MS + 400000,
                identity_present=False, sid_present=False, authorization="NONE",
            )]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("FLOW_PENDING", result["reasons"])

    def test_verified_flow_without_identity_evidence_is_insufficient(self):
        data = helpers.observation(
            flows={"A": [helpers.flow(
                status="IDENTITY_VERIFIED", identity_present=False,
                authorization="ACTIVE",
            )]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("IDENTITY_MISSING", result["reasons"])
        checks = {check["id"]: check for check in result["checks"]}
        self.assertEqual(checks["identity_present"]["kind"], "premise")
        self.assertEqual(checks["identity_present"]["result"], "insufficient")

    def test_session_ended_flow_is_insufficient_for_login(self):
        data = helpers.observation(
            flows={"A": [helpers.flow(status="SESSION_ENDED")]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("SESSION_ENDED", result["reasons"])

    def test_refreshing_authorization_is_insufficient(self):
        data = helpers.observation(
            flows={"A": [helpers.flow(authorization="REFRESHING")]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("AUTHORIZATION_REFRESHING", result["reasons"])

    def test_non_active_authorization_is_unmet(self):
        data = helpers.observation(
            flows={"A": [helpers.flow(authorization="REAUTH_REQUIRED")]},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "unmet")
        self.assertIn("AUTHORIZATION_NOT_ACTIVE", result["reasons"])

    def test_truncation_blocks_a_pass(self):
        data = helpers.observation(
            flows={"A": [helpers.flow()]},
            truncated=True,
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("TRUNCATED", result["reasons"])

    def test_no_flows_is_insufficient(self):
        result = evaluator.evaluate("login", helpers.observation(flows={"A": []}))
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("NO_FLOWS", result["reasons"])

    def test_environment_failure_is_an_execution_error(self):
        data = helpers.observation(
            flows={"A": [helpers.flow()]},
            environment={"containers": "error"},
        )
        result = evaluator.evaluate("login", data)
        self.assertEqual(result["result"], "error")
        self.assertEqual(result["exit_code"], 4)
        self.assertIn("ENVIRONMENT_CONTAINERS", result["reasons"])
        self.assertEqual(len(result["flows"]["A"]), 1)

    def test_unknown_fields_are_not_copied(self):
        raw_flow = helpers.flow()
        raw_flow["subject_hash"] = "raw-subject-hash-value"
        raw_flow["sid"] = "raw-sid-value"
        data = helpers.observation(flows={"A": [raw_flow]})
        data["debug"] = "raw-debug-value"
        data["flows"]["A"][0]["path"] = "C:/private/record.json"
        rendered = json.dumps(evaluator.evaluate("login", data), sort_keys=True)
        self.assertNotIn("raw-subject-hash-value", rendered)
        self.assertNotIn("raw-sid-value", rendered)
        self.assertNotIn("raw-debug-value", rendered)
        self.assertNotIn("private", rendered)


if __name__ == "__main__":
    unittest.main()
