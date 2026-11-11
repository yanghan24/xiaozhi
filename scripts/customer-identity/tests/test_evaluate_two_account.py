"""Two-account isolation tests for T10-T13 and summary/exit constraints."""
import unittest

import identity_check.evaluate as evaluator
from tests import helpers


class TwoAccountTests(unittest.TestCase):
    def _baseline(self, status_a="IDENTITY_VERIFIED", status_b="IDENTITY_VERIFIED",
                  auth_a="ACTIVE", auth_b="ACTIVE", distinct=True,
                  order_a=1, order_b=2, activity_a=helpers.BASE_MS,
                  activity_b=helpers.BASE_MS):
        return helpers.baseline(
            "two-account",
            {
                "A": [helpers.state_flow("F1", order_a, status_a, auth_a,
                                         last_activity_ms=activity_a)],
                "B": [helpers.state_flow("F2", order_b, status_b, auth_b,
                                         last_activity_ms=activity_b)],
            },
            {"A": "F1", "B": "F2"},
            observed_at_ms=helpers.BASE_MS,
            identities_distinct=distinct,
            issuer_total=5,
        )

    def _observation(self, status_a="IDENTITY_VERIFIED", status_b="SESSION_ENDED",
                     matched_a=0, matched_b=1, auth_a="ACTIVE",
                     auth_b="ACTIVE", **kwargs):
        values = {
            "flows": {
                "A": [helpers.flow(
                    "F1", 1, status_a, authorization=auth_a,
                    matched_before_window=0,
                    matched_in_window=matched_a,
                    matched_received_at_ms=(
                        [helpers.BASE_MS + 150000] if matched_a else []
                    ),
                )],
                "B": [helpers.flow(
                    "F2", 2, status_b, authorization=auth_b,
                    matched_before_window=0,
                    matched_in_window=matched_b,
                    matched_received_at_ms=(
                        [helpers.BASE_MS + 160000] if matched_b else []
                    ),
                )],
            },
            "window": dict(helpers.WINDOW),
            "observed_at_ms": helpers.BASE_MS + 1000000,
            "receipts": {
                "issuer_total": 6,
                "issuer_before_window": 0,
                "issuer_in_window": 2,
                "other_issuer_total": 3,
                "since_baseline": 1,
            },
            "log": {"status": "complete", "post_204_in_window": 1,
                    "post_400_in_window": 0, "boundary_lines": 0},
            "identity_pairs": [
                {"a_label": "F1", "b_label": "F2", "distinct": True}
            ],
            "baseline_valid": True,
        }
        values.update(kwargs)
        return helpers.observation(**values)

    def test_isolated_accounts_pass(self):
        result = evaluator.evaluate(
            "two-account", self._observation(), baseline=self._baseline()
        )
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["exit_code"], 0)
        self.assertTrue(result["identities_distinct"])
        self.assertTrue(result["baseline_ready"])
        self.assertEqual(result["selected_targets"], {"A": "F1", "B": "F2"})
        self.assertTrue(result["flows"]["A"][0]["target"])
        self.assertTrue(result["flows"]["B"][0]["target"])
        checks = {check["id"]: check for check in result["checks"]}
        self.assertEqual(checks["authorization_stable_a"]["result"], "pass")
        self.assertEqual(checks["authorization_a_info"]["value"], "ACTIVE")
        self.assertEqual(checks["authorization_b_info"]["value"], "ACTIVE")

    def test_receipt_continuity_mismatch_blocks_an_isolation_claim(self):
        data = self._observation(status_a="SESSION_ENDED", matched_a=0)
        data["receipts"] = {
            "issuer_total": 99,
            "issuer_before_window": 0,
            "issuer_in_window": 1,
            "other_issuer_total": 0,
            "since_baseline": 1,
        }
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("RECEIPT_CONTINUITY", result["reasons"])
        checks = {check["id"]: check for check in result["checks"]}
        self.assertEqual(checks["receipt_continuity_gate"]["kind"], "premise")
        self.assertEqual(checks["receipt_continuity_gate"]["result"], "insufficient")

    def test_clock_skew_blocks_window_evidence(self):
        result = evaluator.evaluate(
            "two-account", self._observation(clock_skew_ms=3000),
            baseline=self._baseline(),
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("CLOCK_SKEW", result["reasons"])

    def test_current_a_authorization_change_is_an_evidence_gate(self):
        result = evaluator.evaluate(
            "two-account", self._observation(auth_a="REAUTH_REQUIRED"),
            baseline=self._baseline(),
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("A_AUTHORIZATION_CHANGED", result["reasons"])
        checks = {check["id"]: check for check in result["checks"]}
        self.assertEqual(checks["authorization_stable_a"]["kind"], "premise")
        self.assertEqual(checks["authorization_stable_a"]["result"], "insufficient")
        self.assertEqual(checks["authorization_a_info"]["value"], "REAUTH_REQUIRED")
        self.assertEqual(checks["authorization_b_info"]["value"], "ACTIVE")

    def test_current_a_refreshing_authorization_is_insufficient(self):
        result = evaluator.evaluate(
            "two-account", self._observation(auth_a="REFRESHING"),
            baseline=self._baseline(),
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("AUTHORIZATION_REFRESHING", result["reasons"])
        checks = {check["id"]: check for check in result["checks"]}
        self.assertEqual(checks["authorization_stable_a"]["result"], "insufficient")

    def test_identical_subjects_are_insufficient(self):
        data = self._observation(
            identity_pairs=[
                {"a_label": "F1", "b_label": "F2", "distinct": False}
            ],
        )
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertFalse(result["identities_distinct"])
        self.assertIn("IDENTITIES_NOT_DISTINCT", result["reasons"])

    def test_direct_distinct_fallback_only_when_pairs_absent(self):
        data = self._observation(identity_pairs=None, identities_distinct=True)
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "pass")
        self.assertTrue(result["identities_distinct"])

        data = self._observation(identity_pairs=[], identities_distinct=True)
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("IDENTITY_PAIR_MISSING", result["reasons"])

    def test_baseline_is_required(self):
        result = evaluator.evaluate(
            "two-account", self._observation(baseline_valid=False), baseline=None
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_REQUIRED", result["reasons"])
        self.assertFalse(result["baseline_ready"])

    def test_baseline_browser_must_not_be_idle_expired(self):
        # Review P2: a browser whose 30-minute deadline was reached at the
        # baseline observation, or with no browser activity, is not a usable
        # "both sessions valid" baseline.
        cutoff = helpers.BASE_MS - evaluator.IDLE_MS
        for name, values in (
            ("a-expired", {"activity_a": cutoff}),
            ("b-expired", {"activity_b": cutoff}),
            ("a-missing", {"activity_a": None}),
            ("b-missing", {"activity_b": None}),
        ):
            with self.subTest(case=name):
                result = evaluator.evaluate(
                    "two-account", self._observation(),
                    baseline=self._baseline(**values),
                )
                self.assertEqual(result["result"], "insufficient")
                self.assertEqual(result["exit_code"], 3)
                checks = {check["id"]: check for check in result["checks"]}
                suffix = name[0]
                self.assertEqual(checks["baseline_browser_" + suffix]["result"],
                                 "insufficient")

    def test_baseline_browser_one_millisecond_before_deadline_is_usable(self):
        result = evaluator.evaluate(
            "two-account", self._observation(),
            baseline=self._baseline(
                activity_a=helpers.BASE_MS - evaluator.IDLE_MS + 1,
                activity_b=helpers.BASE_MS - evaluator.IDLE_MS + 1),
        )
        self.assertEqual(result["result"], "pass")
        checks = {check["id"]: check for check in result["checks"]}
        self.assertEqual(checks["baseline_browser_a"]["result"], "pass")
        self.assertEqual(checks["baseline_browser_b"]["result"], "pass")

    def test_baseline_phase_rejects_idle_expired_or_missing_browser(self):
        observed = helpers.BASE_MS + 1000000
        for name, activity in (("expired", observed - evaluator.IDLE_MS),
                               ("missing", None)):
            with self.subTest(case=name):
                data = helpers.observation(
                    flows={
                        "A": [helpers.flow("F1", 1, last_activity_ms=activity)],
                        "B": [helpers.flow("F2", 2, last_activity_ms=observed)],
                    },
                    window=None, observed_at_ms=observed,
                    identity_pairs=[{"a_label": "F1", "b_label": "F2",
                                     "distinct": True}],
                )
                result = evaluator.evaluate("two-account", data, phase="baseline")
                self.assertFalse(result["baseline_ready"])
                checks = {check["id"]: check for check in result["checks"]}
                self.assertEqual(checks["baseline_browser_a"]["result"],
                                 "insufficient")
                self.assertEqual(checks["baseline_browser_b"]["result"], "pass")

    def test_baseline_a_already_ended_is_insufficient(self):
        result = evaluator.evaluate(
            "two-account", self._observation(),
            baseline=self._baseline(status_a="SESSION_ENDED"),
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_STATUS", result["reasons"])
        self.assertFalse(result["baseline_ready"])

    def test_baseline_authorization_must_be_active(self):
        result = evaluator.evaluate(
            "two-account", self._observation(),
            baseline=self._baseline(auth_a="REAUTH_REQUIRED"),
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_AUTHORIZATION", result["reasons"])

    def test_baseline_must_have_distinct_accounts(self):
        result = evaluator.evaluate(
            "two-account", self._observation(),
            baseline=self._baseline(distinct=False),
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_DISTINCT", result["reasons"])

    def test_baseline_flow_identity_must_match(self):
        result = evaluator.evaluate(
            "two-account", self._observation(),
            baseline=self._baseline(order_a=99),
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_TARGET_CHANGED", result["reasons"])

    def test_a_session_ending_without_a_receipt_is_isolation_failure(self):
        data = self._observation(status_a="SESSION_ENDED", matched_a=0)
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "unmet")
        self.assertEqual(result["exit_code"], 1)
        self.assertIn("A_SESSION_ENDED", result["reasons"])

    def test_a_with_matching_receipt_is_insufficient(self):
        data = self._observation(status_a="SESSION_ENDED", matched_a=1)
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("A_RECEIPT_MATCHED", result["reasons"])

    def test_b_missing_receipt_is_unmet(self):
        data = self._observation(status_b="IDENTITY_VERIFIED", matched_b=0)
        data["log"]["post_204_in_window"] = 0
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "unmet")
        self.assertIn("B_RECEIPT_NOT_MATCHED", result["reasons"])
        self.assertIn("B_SESSION_NOT_ENDED", result["reasons"])

    def test_incomplete_log_blocks_no_notification_claim(self):
        data = self._observation(status_b="IDENTITY_VERIFIED", matched_b=0)
        data["log"] = {"status": "missing", "post_204_in_window": 0,
                       "post_400_in_window": 0, "boundary_lines": 0}
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("LOG_MISSING", result["reasons"])

    def test_identity_pair_is_looked_up_after_target_selection(self):
        baseline = helpers.baseline(
            "two-account",
            {
                "A": [
                    helpers.state_flow("F1", 1, "AWAITING_IDENTITY", "NONE",
                                       identity_present=False),
                    helpers.state_flow("F2", 2),
                ],
                "B": [helpers.state_flow("F3", 3)],
            },
            {"A": "F2", "B": "F3"},
            observed_at_ms=helpers.BASE_MS,
            identities_distinct=True,
            issuer_total=5,
        )
        data = self._observation(
            flows={
                "A": [
                    helpers.flow("F1", 1, "AWAITING_IDENTITY",
                                 identity_present=False, sid_present=False,
                                 authorization="NONE"),
                    helpers.flow("F2", 2, "IDENTITY_VERIFIED"),
                ],
                "B": [helpers.flow("F3", 3, "SESSION_ENDED",
                                   matched_in_window=1,
                                   matched_received_at_ms=[helpers.BASE_MS + 160000])],
            },
            identity_pairs=[
                {"a_label": "F1", "b_label": "F3", "distinct": False},
                {"a_label": "F2", "b_label": "F3", "distinct": True},
            ],
        )
        result = evaluator.evaluate("two-account", data, baseline=baseline)
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["selected_targets"], {"A": "F2", "B": "F3"})
        self.assertTrue(result["identities_distinct"])

    def test_open_window_downgrades_isolation_unmet(self):
        data = self._observation(status_a="SESSION_ENDED", matched_a=0)
        data["observed_at_ms"] = helpers.WINDOW["start_ms"] + 50000
        result = evaluator.evaluate(
            "two-account", data, baseline=self._baseline()
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("WINDOW_OPEN", result["reasons"])


if __name__ == "__main__":
    unittest.main()
