"""Logout scenario evaluation tests for T01 and T04-T09/T18/T19/T23."""
import unittest

import identity_check.evaluate as evaluator
from tests import helpers


class LogoutTests(unittest.TestCase):
    def _passing_flow(self, matched=1, before=0):
        return helpers.flow(
            "F1", 1, "SESSION_ENDED", matched_before_window=before,
            matched_in_window=matched,
            matched_received_at_ms=[helpers.BASE_MS + 150000] if matched else [],
        )

    def _observation(self, flow=None, **kwargs):
        values = {
            "flows": {"A": [flow if flow is not None else self._passing_flow()]},
            "window": dict(helpers.WINDOW),
            "observed_at_ms": helpers.BASE_MS + 1000000,
            "receipts": {
                "issuer_total": 1,
                "issuer_before_window": 0,
                "issuer_in_window": 1,
                "other_issuer_total": 0,
            },
            "log": {"status": "complete", "post_204_in_window": 1,
                    "post_400_in_window": 0, "boundary_lines": 0},
        }
        values.update(kwargs)
        return helpers.observation(**values)

    def test_other_issuer_history_is_reported_not_counted(self):
        flow = self._passing_flow()
        data = self._observation(
            flow=flow,
            receipts={"issuer_total": 1, "issuer_before_window": 0,
                      "issuer_in_window": 1, "other_issuer_total": 32},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["receipts"]["other_issuer_total"], 32)
        self.assertEqual(result["receipts"]["issuer_total"], 1)
        self.assertIn("OTHER_ISSUER_RECEIPTS", result["notes"])

    def test_no_matching_receipt_is_unmet(self):
        flow = helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                            matched_before_window=0, matched_in_window=0)
        data = self._observation(
            flow=flow,
            receipts={"issuer_total": 0, "issuer_before_window": 0,
                      "issuer_in_window": 0, "other_issuer_total": 0},
            log={"status": "complete", "post_204_in_window": 0,
                 "post_400_in_window": 0, "boundary_lines": 0},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "unmet")
        self.assertEqual(result["exit_code"], 1)
        self.assertIn("RECEIPT_NOT_MATCHED", result["reasons"])
        self.assertIn("SESSION_NOT_ENDED", result["reasons"])
        self.assertIn("LOG_NO_204", result["reasons"])

    def test_multiple_matching_receipts_pass_with_note(self):
        flow = self._passing_flow(matched=2)
        flow["matched_received_at_ms"] = [
            helpers.BASE_MS + 150000, helpers.BASE_MS + 160000
        ]
        result = evaluator.evaluate("logout", self._observation(flow=flow))
        self.assertEqual(result["result"], "pass")
        self.assertIn("MULTIPLE_MATCHED_RECEIPTS", result["notes"])

    def test_prior_matching_receipt_blocks_the_round(self):
        result = evaluator.evaluate(
            "logout", self._observation(flow=self._passing_flow(before=1))
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("PRIOR_RECEIPT_MATCH", result["reasons"])

    def test_sid_priority_oracle_differs_sid_matches_sub(self):
        raw_flow = {
            "issuer": "issuer-one",
            "sid": "sid-one",
            "subject": "subject-one",
            "created_at_ms": helpers.BASE_MS,
        }
        item = helpers.receipt(sid="sid-two", subject="subject-one",
                               issued_at_ms=helpers.BASE_MS + 100,
                               received_at_ms=helpers.BASE_MS + 200)
        self.assertFalse(helpers.backchannel_matches(raw_flow, item))
        self.assertEqual(
            helpers.receipt_counts(raw_flow, [item], helpers.WINDOW),
            (0, 0, []),
        )
        flow = helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                            matched_before_window=0, matched_in_window=0)
        data = self._observation(
            flow=flow,
            receipts={"issuer_total": 0, "issuer_before_window": 0,
                      "issuer_in_window": 0, "other_issuer_total": 0},
            log={"status": "complete", "post_204_in_window": 0,
                 "post_400_in_window": 0, "boundary_lines": 0},
        )
        self.assertEqual(evaluator.evaluate("logout", data)["result"], "unmet")

    def test_missing_sid_matches_by_issuer_and_subject(self):
        raw_flow = {
            "issuer": "issuer-one",
            "sid": "sid-one",
            "subject": "subject-one",
            "created_at_ms": helpers.BASE_MS,
        }
        item = helpers.receipt(sid=None, subject="subject-one",
                               issued_at_ms=helpers.BASE_MS + 100,
                               received_at_ms=helpers.WINDOW["start_ms"] + 1000)
        self.assertTrue(helpers.backchannel_matches(raw_flow, item))
        before, in_window, received = helpers.receipt_counts(
            raw_flow, [item], helpers.WINDOW
        )
        self.assertEqual((before, in_window), (0, 1))
        self.assertEqual(received, [helpers.WINDOW["start_ms"] + 1000])
        flow = helpers.flow("F1", 1, "SESSION_ENDED",
                            matched_before_window=before,
                            matched_in_window=in_window,
                            matched_received_at_ms=received)
        result = evaluator.evaluate("logout", self._observation(flow=flow))
        self.assertEqual(result["result"], "pass")

    def test_millisecond_boundaries_and_callback_equivalence(self):
        issued = helpers.BASE_MS + 150000
        cases = (
            (issued + 999, issued + 999, True),
            (issued + 1000, issued + 1000, False),
            (issued + 999, issued + 998, False),
        )
        for created, received, expected in cases:
            with self.subTest(created=created, received=received):
                raw_flow = {
                    "issuer": "issuer-one",
                    "sid": "sid-one",
                    "subject": "subject-one",
                    "created_at_ms": created,
                }
                item = helpers.receipt(
                    sid="sid-one", subject="subject-one",
                    issued_at_ms=issued, received_at_ms=received,
                )
                self.assertEqual(helpers.backchannel_matches(raw_flow, item), expected)
                self.assertEqual(helpers.callback_matches(raw_flow, item), expected)

    def test_receipt_without_complete_log_204_is_insufficient(self):
        data = self._observation(
            log={"status": "complete", "post_204_in_window": 0,
                 "post_400_in_window": 1, "boundary_lines": 0},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("LOG_SOURCE_MISMATCH", result["reasons"])
        self.assertIn("LOG_POST_400", result["notes"])

    def test_incomplete_log_cannot_support_no_notification(self):
        for status, reason in (
            ("missing", "LOG_MISSING"),
            ("not_configured", "LOG_NOT_CONFIGURED"),
            ("incomplete", "LOG_INCOMPLETE"),
        ):
            with self.subTest(status=status):
                flow = helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                                    matched_before_window=0, matched_in_window=0)
                data = self._observation(
                    flow=flow,
                    receipts={"issuer_total": 0, "issuer_before_window": 0,
                              "issuer_in_window": 0, "other_issuer_total": 0},
                    log={"status": status, "post_204_in_window": 0,
                         "post_400_in_window": 0, "boundary_lines": 0,
                         "reasons": ["LOG_ROTATED"]},
                )
                result = evaluator.evaluate("logout", data)
                self.assertEqual(result["result"], "insufficient")
                self.assertIn(reason, result["reasons"])
                self.assertNotIn("LOG_NO_204", result["reasons"])
                self.assertIn("LOG_ROTATED", result["notes"])

    def test_complete_log_without_count_is_insufficient(self):
        flow = helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                            matched_before_window=0, matched_in_window=0)
        data = self._observation(
            flow=flow,
            receipts={"issuer_total": 0, "issuer_before_window": 0,
                      "issuer_in_window": 0, "other_issuer_total": 0},
            log={"status": "complete", "post_204_in_window": None,
                 "post_400_in_window": 0, "boundary_lines": 0},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("LOG_COUNTS_MISSING", result["reasons"])

    def test_open_window_downgrades_unmet(self):
        flow = helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                            matched_before_window=0, matched_in_window=0)
        data = self._observation(
            flow=flow,
            observed_at_ms=helpers.WINDOW["start_ms"] + 50000,
            receipts={"issuer_total": 0, "issuer_before_window": 0,
                      "issuer_in_window": 0, "other_issuer_total": 0},
            log={"status": "complete", "post_204_in_window": 0,
                 "post_400_in_window": 0, "boundary_lines": 0},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("WINDOW_OPEN", result["reasons"])
        self.assertFalse(result["window"]["closed"])

    def test_window_not_started_is_insufficient(self):
        flow = helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                            matched_before_window=0, matched_in_window=0)
        data = self._observation(
            flow=flow,
            observed_at_ms=helpers.WINDOW["start_ms"] - 1,
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("WINDOW_NOT_STARTED", result["reasons"])

    def test_window_is_required(self):
        flow = self._passing_flow()
        data = helpers.observation(
            flows={"A": [flow]},
            observed_at_ms=helpers.BASE_MS + 1000000,
            receipts={"issuer_total": 1, "issuer_in_window": 1},
            log={"status": "complete", "post_204_in_window": 1},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("WINDOW_REQUIRED", result["reasons"])

    def test_baseline_continuity_pass_and_failure(self):
        baseline = helpers.baseline(
            "logout", {"A": [helpers.state_flow("F1", 1)]}, {"A": "F1"},
            observed_at_ms=helpers.BASE_MS, issuer_total=5,
        )
        data = self._observation(
            receipts={"issuer_total": 7, "issuer_before_window": 1,
                      "issuer_in_window": 1, "other_issuer_total": 0,
                      "since_baseline": 2},
        )
        result = evaluator.evaluate("logout", data, baseline=baseline)
        self.assertEqual(result["result"], "pass")
        self.assertTrue(result["baseline_ready"])

        data["receipts"]["since_baseline"] = 1
        result = evaluator.evaluate("logout", data, baseline=baseline)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("RECEIPT_CONTINUITY", result["reasons"])

    def test_baseline_target_must_have_been_verified(self):
        baseline = helpers.baseline(
            "logout", {"A": [helpers.state_flow("F1", 1, "SESSION_ENDED")]},
            {"A": "F1"}, observed_at_ms=helpers.BASE_MS, issuer_total=0,
        )
        data = self._observation(
            receipts={"issuer_total": 0, "since_baseline": 0},
        )
        result = evaluator.evaluate("logout", data, baseline=baseline)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_STATUS", result["reasons"])
        self.assertFalse(result["baseline_ready"])

    def test_baseline_target_creation_order_must_match(self):
        baseline = helpers.baseline(
            "logout", {"A": [helpers.state_flow("F1", 1)]}, {"A": "F1"},
            observed_at_ms=helpers.BASE_MS, issuer_total=0,
        )
        flow = helpers.flow("F1", 2, "SESSION_ENDED", matched_in_window=1,
                            matched_received_at_ms=[helpers.BASE_MS + 150000])
        data = self._observation(
            flow=flow,
            receipts={"issuer_total": 0, "since_baseline": 0},
        )
        result = evaluator.evaluate("logout", data, baseline=baseline)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_TARGET_CHANGED", result["reasons"])

    def test_invalid_baseline_flag_blocks(self):
        data = self._observation(baseline_valid=False)
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_INVALID", result["reasons"])

    def test_continuity_mismatch_blocks_a_no_notification_claim(self):
        baseline = helpers.baseline(
            "logout", {"A": [helpers.state_flow("F1", 1)]}, {"A": "F1"},
            observed_at_ms=helpers.BASE_MS, issuer_total=5,
        )
        flow = helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                            matched_before_window=0, matched_in_window=0)
        data = self._observation(
            flow=flow,
            receipts={"issuer_total": 7, "issuer_before_window": 0,
                      "issuer_in_window": 0, "other_issuer_total": 0,
                      "since_baseline": 1},
            log={"status": "complete", "post_204_in_window": 0,
                 "post_400_in_window": 0, "boundary_lines": 0},
        )
        result = evaluator.evaluate("logout", data, baseline=baseline)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("RECEIPT_CONTINUITY", result["reasons"])

    def test_conclusive_unmet_outranks_a_missing_log_criterion(self):
        flow = self._passing_flow()
        flow["stored_status"] = "IDENTITY_VERIFIED"
        data = self._observation(
            flow=flow,
            log={"status": "missing", "post_204_in_window": 0,
                 "post_400_in_window": 0, "boundary_lines": 0},
        )
        result = evaluator.evaluate("logout", data)
        self.assertEqual(result["result"], "unmet")
        self.assertIn("SESSION_NOT_ENDED", result["reasons"])
        self.assertIn("LOG_MISSING", result["reasons"])

    def test_clock_skew_blocks_window_evidence(self):
        result = evaluator.evaluate(
            "logout", self._observation(clock_skew_ms=3000)
        )
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("CLOCK_SKEW", result["reasons"])

    def test_baseline_absent_is_ready(self):
        result = evaluator.evaluate("logout", self._observation())
        self.assertEqual(result["result"], "pass")
        self.assertTrue(result["baseline_ready"])


if __name__ == "__main__":
    unittest.main()
