"""Idle and browser-evidence tests for T14-T17."""
import unittest

import identity_check.evaluate as evaluator
from tests import helpers


DEADLINE = helpers.BASE_MS + evaluator.IDLE_MS


class IdleTests(unittest.TestCase):
    def _flow(self, last_activity_ms=helpers.BASE_MS):
        return helpers.flow("F1", 1, "IDENTITY_VERIFIED", authorization="ACTIVE",
                            last_activity_ms=last_activity_ms)

    def _browser(self, observed_at_ms=DEADLINE, http_status=401,
                 error="IDENTITY_REQUIRED", original_browser=True,
                 binding_matches=True, kind="captured"):
        return {
            "kind": kind,
            "observed_at_ms": observed_at_ms,
            "http_status": http_status,
            "error": error,
            "original_browser": original_browser,
            "binding_matches": binding_matches,
        }

    def _observation(self, last_activity_ms=helpers.BASE_MS,
                     observed_at_ms=DEADLINE, **kwargs):
        values = {
            "flows": {"A": [self._flow(last_activity_ms)]},
            "observed_at_ms": observed_at_ms,
            "clock_skew_ms": 0,
            "browser_evidence": self._browser(),
        }
        values.update(kwargs)
        return helpers.observation(**values)

    def _run(self, data, baseline=None):
        browser = data.pop("browser_evidence", None)
        return evaluator.evaluate("idle", data, baseline=baseline,
                                  browser_evidence=browser)

    def test_deadline_equality_is_expired(self):
        result = self._run(self._observation())
        self.assertEqual(result["result"], "pass")
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["browser_evidence"]["http_status"], 401)

    def test_one_millisecond_before_deadline_is_not_expired(self):
        data = self._observation(observed_at_ms=DEADLINE - 1)
        data["browser_evidence"] = self._browser(observed_at_ms=DEADLINE)
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("IDLE_NOT_REACHED", result["reasons"])

    def test_captured_401_after_deadline_passes(self):
        data = self._observation(
            observed_at_ms=DEADLINE + 5000,
            browser_evidence=self._browser(
                observed_at_ms=DEADLINE + 1000, http_status=401
            ),
        )
        self.assertEqual(self._run(data)["result"], "pass")

    def test_200_after_deadline_is_unmet(self):
        data = self._observation(
            observed_at_ms=DEADLINE + 5000,
            browser_evidence=self._browser(
                observed_at_ms=DEADLINE + 1000, http_status=200,
                error=None,
            ),
        )
        result = self._run(data)
        self.assertEqual(result["result"], "unmet")
        self.assertIn("BROWSER_ACCESSIBLE", result["reasons"])

    def test_missing_browser_evidence_is_insufficient(self):
        data = helpers.observation(flows={"A": [self._flow()]},
                                   observed_at_ms=DEADLINE)
        result = evaluator.evaluate("idle", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_EVIDENCE_MISSING", result["reasons"])
        self.assertEqual(result["flows"]["A"][0]["last_activity_ms"],
                         helpers.BASE_MS)

    def test_user_reported_browser_evidence_cannot_pass(self):
        data = self._observation(
            browser_evidence=self._browser(kind="user_reported")
        )
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_USER_REPORTED", result["reasons"])

    def test_browser_evidence_requires_original_and_binding(self):
        data = self._observation(
            browser_evidence=self._browser(original_browser=False)
        )
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_EVIDENCE_INVALID", result["reasons"])

        data = self._observation(
            browser_evidence=self._browser(binding_matches=False)
        )
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_BINDING_MISMATCH", result["reasons"])

    def test_evidence_before_deadline_is_insufficient(self):
        data = self._observation(
            browser_evidence=self._browser(observed_at_ms=DEADLINE - 1)
        )
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_EVIDENCE_BEFORE_DEADLINE", result["reasons"])

    def test_evidence_beyond_the_observation_grace_is_insufficient(self):
        data = self._observation(
            observed_at_ms=DEADLINE,
            browser_evidence=self._browser(observed_at_ms=DEADLINE + 1001),
        )
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_EVIDENCE_IN_FUTURE", result["reasons"])

    def test_evidence_without_timestamp_is_invalid(self):
        data = self._observation(
            browser_evidence=self._browser(observed_at_ms=None)
        )
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_EVIDENCE_INVALID", result["reasons"])

    def test_unexpected_browser_status_is_insufficient(self):
        data = self._observation(
            browser_evidence=self._browser(http_status=401, error=None)
        )
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BROWSER_STATUS_UNEXPECTED", result["reasons"])

    def test_clock_skew_blocks_even_with_browser_evidence(self):
        data = self._observation(clock_skew_ms=3000)
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("CLOCK_SKEW", result["reasons"])

        data = self._observation(clock_skew_ms=None)
        result = self._run(data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("CLOCK_SKEW_EVIDENCE", result["reasons"])

    def test_baseline_activity_change_blocks(self):
        baseline = helpers.baseline(
            "idle",
            {"A": [helpers.state_flow(
                "F1", 1, "IDENTITY_VERIFIED", "ACTIVE",
                last_activity_ms=helpers.BASE_MS - 1000,
            )]},
            {"A": "F1"},
            observed_at_ms=helpers.BASE_MS,
            issuer_total=0,
        )
        result = self._run(self._observation(), baseline=baseline)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("ACTIVITY_CHANGED", result["reasons"])
        self.assertFalse(result["baseline_ready"])

    def test_unchanged_baseline_activity_passes(self):
        baseline = helpers.baseline(
            "idle",
            {"A": [helpers.state_flow("F1", 1)]},
            {"A": "F1"},
            observed_at_ms=helpers.BASE_MS,
            issuer_total=0,
        )
        result = self._run(self._observation(), baseline=baseline)
        self.assertEqual(result["result"], "pass")
        self.assertTrue(result["baseline_ready"])

    def test_baseline_must_predate_the_observation(self):
        baseline = helpers.baseline(
            "idle",
            {"A": [helpers.state_flow("F1", 1)]},
            {"A": "F1"},
            observed_at_ms=DEADLINE,
            issuer_total=0,
        )
        result = self._run(self._observation(), baseline=baseline)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("BASELINE_TIME", result["reasons"])

    def test_activity_is_required(self):
        data = helpers.observation(
            flows={"A": [helpers.flow("F1", 1, "IDENTITY_VERIFIED",
                                      last_activity_ms=None)]},
            observed_at_ms=DEADLINE,
        )
        result = evaluator.evaluate("idle", data)
        self.assertEqual(result["result"], "insufficient")
        self.assertIn("ACTIVITY_UNKNOWN", result["reasons"])


if __name__ == "__main__":
    unittest.main()
