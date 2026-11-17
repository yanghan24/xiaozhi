"""Offline baseline and browser-evidence tests for identity_check.state.

The private baseline must bind the scenario, record fingerprints, environment
fingerprint, selected targets and baseline time.  Browser evidence must bind
the record fingerprint, selected flow and last activity, and must only claim an
original browser capture when that flag is literally true.

A malformed baseline is still a private input boundary: weakly validated enum,
boolean, target, record, identity and log-state fields can otherwise carry
arbitrary strings forward.  Those cases are asserted here and reported to main
rather than worked around in product code.
"""

import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from identity_check.errors import CheckError
from identity_check.state import (
    STATE_SCHEMA,
    bind_baseline,
    browser_evidence,
    load_baseline,
    make_state,
)


ENVIRONMENT = "a" * 64
RECORD = "b" * 64
OTHER = "c" * 64
OBSERVED = 1700000000000
FLOW_KEYS = ("label", "creation_order", "stored_status", "created_at_ms",
             "expires_at_ms", "sid_present", "identity_present", "authorization",
             "last_activity_ms")


def flow(**overrides):
    value = {
        "label": "F1",
        "creation_order": 1,
        "stored_status": "IDENTITY_VERIFIED",
        "created_at_ms": OBSERVED - 1000,
        "expires_at_ms": OBSERVED + 300000,
        "sid_present": True,
        "identity_present": True,
        "authorization": "ACTIVE",
        "last_activity_ms": OBSERVED - 500,
    }
    value.update(overrides)
    return value


def state_document(**overrides):
    value = {
        "schema": STATE_SCHEMA,
        "scenario": "login",
        "observed_at_ms": OBSERVED - 10000,
        "environment_fingerprint": ENVIRONMENT,
        "records": {"A": RECORD},
        "targets": {"A": "F1"},
        "identities_distinct": None,
        "flows": {"A": [flow()]},
        "issuer_total": 0,
        "log_state": {"inode": 7, "size": 100,
                      "last_timestamp_ms": OBSERVED - 20000, "configured": True},
    }
    value.update(overrides)
    return value


def record(fingerprint=RECORD):
    return SimpleNamespace(fingerprint=fingerprint)


def records(fingerprint=RECORD):
    return {"A": record(fingerprint)}


def observation(observed_at_ms=OBSERVED + 1000, window=None,
                environment_fingerprint=ENVIRONMENT):
    value = {"observed_at_ms": observed_at_ms,
             "environment_fingerprint": environment_fingerprint}
    if window is not None:
        value["window"] = window
    return value


def write_json(path, value, mode=0o600):
    path.write_text(json.dumps(value), encoding="utf-8")
    if os.name == "posix":
        os.chmod(str(path), mode)
    return path


def write_bytes(path, content, mode=0o600):
    path.write_bytes(content)
    if os.name == "posix":
        os.chmod(str(path), mode)
    return path


class StateTestBase(unittest.TestCase):
    def assert_code(self, code, function, *args, **kwargs):
        with self.assertRaises(CheckError) as caught:
            function(*args, **kwargs)
        self.assertEqual(caught.exception.code, code)
        self.assertEqual(str(caught.exception), code)
        return caught.exception


class MakeStateTest(StateTestBase):
    def test_make_state_binds_scenario_records_targets_and_filters_flows(self):
        observation_value = {
            "environment_fingerprint": ENVIRONMENT,
            "observed_at_ms": OBSERVED,
            "identities_distinct": True,
            "flows": {"A": [flow(private_value="not-copied")]},
            "receipts": {"issuer_total": 4},
            "log": {"state": {"inode": 2, "size": 3,
                              "last_timestamp_ms": 4, "configured": True}},
        }
        targets = {"A": "F1"}

        state = make_state("login", observation_value, {"A": record()}, targets)

        self.assertEqual(state["schema"], STATE_SCHEMA)
        self.assertEqual(state["scenario"], "login")
        self.assertEqual(state["observed_at_ms"], OBSERVED)
        self.assertEqual(state["environment_fingerprint"], ENVIRONMENT)
        self.assertEqual(state["records"], {"A": RECORD})
        self.assertEqual(state["targets"], targets)
        self.assertIs(state["identities_distinct"], True)
        self.assertEqual(state["issuer_total"], 4)
        self.assertEqual(state["log_state"], observation_value["log"]["state"])
        self.assertEqual(set(state["flows"]["A"][0]), set(FLOW_KEYS))
        self.assertNotIn("private_value", state["flows"]["A"][0])

    def test_make_state_defaults_optional_observation_fields(self):
        state = make_state("idle", {"environment_fingerprint": ENVIRONMENT,
                                    "observed_at_ms": OBSERVED,
                                    "flows": {"A": []}}, {}, {})

        self.assertEqual(state["records"], {})
        self.assertEqual(state["targets"], {})
        self.assertEqual(state["flows"], {"A": []})
        self.assertEqual(state["issuer_total"], 0)
        self.assertEqual(state["log_state"], {})
        self.assertIsNone(state["identities_distinct"])

    def test_make_state_requires_bound_environment_fingerprint(self):
        values = (None, "", "A" * 64, "a" * 63, "a" * 65, "g" * 64, 5, True)
        for value in values:
            with self.subTest(value=value):
                self.assert_code(
                    "STATE_ENVIRONMENT_UNBOUND", make_state, "login",
                    {"environment_fingerprint": value, "observed_at_ms": OBSERVED,
                     "flows": {"A": []}}, {}, {})


class LoadBaselineTest(StateTestBase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "baseline.json"

    def test_valid_baseline_roundtrips(self):
        document = state_document()
        write_json(self.path, document)

        self.assertEqual(load_baseline(self.path), document)

    def test_schema_mismatch_has_its_own_code(self):
        write_json(self.path, state_document(schema="other-schema"))

        self.assert_code("STATE_FORMAT_UNSUPPORTED", load_baseline, self.path)

    def test_shape_validation(self):
        def mutate(mutator):
            document = state_document()
            mutator(document)
            return document

        cases = {
            "observed-missing": lambda d: d.pop("observed_at_ms"),
            "observed-bool": lambda d: d.update({"observed_at_ms": True}),
            "observed-string": lambda d: d.update({"observed_at_ms": "1700"}),
            "observed-float": lambda d: d.update({"observed_at_ms": 1.5}),
            "observed-negative": lambda d: d.update({"observed_at_ms": -1}),
            "scenario-unknown": lambda d: d.update({"scenario": "other"}),
            "records-list": lambda d: d.update({"records": []}),
            "records-value": lambda d: d["records"].update({"A": "A" * 64}),
            "environment-missing": lambda d: d.pop("environment_fingerprint"),
            "environment-uppercase": lambda d: d.update(
                {"environment_fingerprint": "A" * 64}),
            "targets-list": lambda d: d.update({"targets": []}),
            "issuer-bool": lambda d: d.update({"issuer_total": True}),
            "issuer-negative": lambda d: d.update({"issuer_total": -1}),
            "issuer-float": lambda d: d.update({"issuer_total": 1.0}),
            "flows-list": lambda d: d.update({"flows": []}),
            "flows-extra-record": lambda d: d["flows"].update({"C": []}),
            "flows-too-many": lambda d: d["flows"].update(
                {"A": [flow() for _ in range(51)]}),
            "flow-not-object": lambda d: d["flows"].update({"A": ["F1"]}),
            "flow-extra-key": lambda d: d["flows"]["A"][0].update({"extra": "x"}),
            "flow-missing-key": lambda d: d["flows"]["A"][0].pop("last_activity_ms"),
            "flow-label-f0": lambda d: d["flows"]["A"][0].update({"label": "F0"}),
            "flow-label-f100": lambda d: d["flows"]["A"][0].update({"label": "F100"}),
            "flow-order-zero": lambda d: d["flows"]["A"][0].update(
                {"creation_order": 0}),
            "flow-order-bool": lambda d: d["flows"]["A"][0].update(
                {"creation_order": True}),
            "flow-created-negative": lambda d: d["flows"]["A"][0].update(
                {"created_at_ms": -1}),
            "flow-expires-bool": lambda d: d["flows"]["A"][0].update(
                {"expires_at_ms": True}),
            "flow-activity-string": lambda d: d["flows"]["A"][0].update(
                {"last_activity_ms": "1700"}),
        }
        for name, mutator in cases.items():
            with self.subTest(case=name):
                write_json(self.path, mutate(mutator))
                self.assert_code("STATE_INVALID", load_baseline, self.path)

    def test_duplicate_json_keys_are_rejected(self):
        write_bytes(self.path, ('{"schema":"%s","schema":"replaced"}'
                                % STATE_SCHEMA).encode("utf-8"))

        self.assert_code("INPUT_DUPLICATE_FIELD", load_baseline, self.path)

    def test_injected_strings_in_weakly_validated_fields_are_rejected(self):
        """Malformed baseline fields must not carry arbitrary strings forward.

        Each case is a real gap in ``load_baseline``: enum, bool, target,
        record-key, identity and log-state values are copied without type or
        allowlist checks.  Reported to main for a product fix.
        """
        def mutate(mutator):
            document = state_document()
            mutator(document)
            return document

        cases = {
            "flow-stored-status": lambda d: d["flows"]["A"][0].update(
                {"stored_status": "SENSITIVE_STATUS"}),
            "flow-authorization": lambda d: d["flows"]["A"][0].update(
                {"authorization": "SENSITIVE_AUTHORIZATION"}),
            "flow-sid-present": lambda d: d["flows"]["A"][0].update(
                {"sid_present": "yes"}),
            "flow-identity-present": lambda d: d["flows"]["A"][0].update(
                {"identity_present": 1}),
            "targets-value": lambda d: d["targets"].update(
                {"A": "SENSITIVE TARGET"}),
            "targets-extra-key": lambda d: d["targets"].update({"C": "F1"}),
            "records-extra-key": lambda d: d["records"].update(
                {"INJECTED": RECORD}),
            "unknown-top-level": lambda d: d.update(
                {"INJECTED_FIELD": "SENSITIVE_VALUE"}),
            "identities-distinct": lambda d: d.update(
                {"identities_distinct": "yes"}),
            "log-state-type": lambda d: d["log_state"].update(
                {"inode": "SENSITIVE_INODE"}),
            "log-state-size": lambda d: d["log_state"].update(
                {"size": "SENSITIVE_SIZE"}),
            "log-state-last-timestamp": lambda d: d["log_state"].update(
                {"last_timestamp_ms": "SENSITIVE_TIME"}),
            "log-state-configured": lambda d: d["log_state"].update(
                {"configured": "yes"}),
            "log-state-extra": lambda d: d["log_state"].update(
                {"prefix": "SENSITIVE_PREFIX"}),
        }
        for name, mutator in cases.items():
            with self.subTest(case=name):
                write_json(self.path, mutate(mutator))
                self.assert_code("STATE_INVALID", load_baseline, self.path)


class BaselineBindingTest(StateTestBase):
    def setUp(self):
        self.baseline = state_document()
        self.records = records()
        self.targets = {"A": "F1"}

    def bind(self, observation_value, baseline=None, scenario="login",
             records_value=None, targets=None):
        return bind_baseline(baseline if baseline is not None else self.baseline,
                             scenario,
                             records_value if records_value is not None else self.records,
                             observation_value,
                             targets if targets is not None else self.targets)

    def test_baseline_binds_valid_scenario_records_environment_targets_and_time(self):
        observation_value = observation(
            OBSERVED + 500, {"start_ms": OBSERVED, "end_ms": OBSERVED + 1000})

        self.assertTrue(self.bind(observation_value))
        self.assertTrue(observation_value["baseline_valid"])
        self.assertEqual(observation_value["baseline_reasons"], [])

    def test_baseline_time_must_precede_window_or_observation(self):
        cases = (
            ("window-equal", OBSERVED - 10000,
             {"start_ms": OBSERVED - 10000, "end_ms": OBSERVED}, False),
            ("window-after", OBSERVED - 10000,
             {"start_ms": OBSERVED - 9999, "end_ms": OBSERVED}, True),
            ("observed-equal", OBSERVED - 10000, None, False),
            ("observed-after", OBSERVED - 9999, None, True),
        )
        for name, observed_at_ms, window, expected in cases:
            with self.subTest(case=name):
                observation_value = observation(observed_at_ms, window)
                self.assertIs(self.bind(observation_value), expected)
                self.assertEqual(
                    "BASELINE_TIME_INVALID" in observation_value["baseline_reasons"],
                    not expected)

    def test_scenario_record_environment_and_target_changes_are_reported(self):
        cases = (
            ("scenario", {"scenario": "logout"}, {},
             {}, "BASELINE_SCENARIO_CHANGED"),
            ("record", {}, {"records_value": records(OTHER)},
             {}, "BASELINE_RECORD_CHANGED"),
            ("environment", {}, {},
             {"environment_fingerprint": OTHER}, "BASELINE_ENVIRONMENT_CHANGED"),
            ("target", {}, {"targets": {"A": "F2"}},
             {}, "BASELINE_TARGET_CHANGED"),
            ("target-extra-current", {}, {"targets": {"A": "F1", "B": "F2"}},
             {}, "BASELINE_TARGET_CHANGED"),
        )
        for name, baseline_override, bind_kwargs, observation_kwargs, expected_reason in cases:
            with self.subTest(case=name):
                baseline = state_document(**baseline_override)
                observation_value = observation(**observation_kwargs)
                self.assertFalse(self.bind(observation_value, baseline=baseline,
                                           **bind_kwargs))
                self.assertIn(expected_reason, observation_value["baseline_reasons"])

    def test_multiple_reasons_are_sorted_and_not_raw_values(self):
        baseline = state_document(scenario="logout", records={"A": OTHER},
                                  environment_fingerprint=OTHER,
                                  targets={"A": "F2"},
                                  observed_at_ms=OBSERVED)
        observation_value = observation(
            OBSERVED, {"start_ms": OBSERVED, "end_ms": OBSERVED + 1000})

        self.assertFalse(self.bind(observation_value, baseline=baseline))
        self.assertEqual(observation_value["baseline_reasons"], [
            "BASELINE_ENVIRONMENT_CHANGED",
            "BASELINE_RECORD_CHANGED",
            "BASELINE_SCENARIO_CHANGED",
            "BASELINE_TARGET_CHANGED",
            "BASELINE_TIME_INVALID",
        ])
        text = json.dumps(observation_value)
        self.assertNotIn(OTHER, text)
        self.assertNotIn(RECORD, text)


@unittest.skipUnless(os.name == "posix",
                     "POSIX 0600 permission bits cannot be enforced on Windows")
class BaselinePermissionTest(StateTestBase):
    def test_baseline_must_be_private(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "baseline.json"

        write_json(path, state_document(), mode=0o644)
        self.assert_code("INPUT_NOT_PRIVATE", load_baseline, path)

        write_json(path, state_document(), mode=0o600)
        self.assertEqual(load_baseline(path)["schema"], STATE_SCHEMA)


class BrowserEvidenceTest(StateTestBase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "browser-evidence.json"
        self.record = record()
        self.flow = {"label": "F1", "last_activity_ms": OBSERVED}

    def evidence(self, **overrides):
        value = {
            "schema": "xiaozhi-browser-evidence-v1",
            "kind": "captured",
            "observed_at": "2023-11-14T22:13:20.000Z",
            "http_status": 401,
            "error": "IDENTITY_REQUIRED",
            "original_browser": True,
            "record_fingerprint": RECORD,
            "flow": "F1",
            "last_activity_ms": OBSERVED,
        }
        value.update(overrides)
        return value

    def evaluate(self, value, flow=None):
        write_json(self.path, value)
        return browser_evidence(self.path, self.record, "F1",
                                self.flow if flow is None else flow)

    def test_baseline_browser_evidence_binds_captured_original_response(self):
        result = self.evaluate(self.evidence())

        self.assertEqual(result, {
            "kind": "captured",
            "observed_at_ms": OBSERVED,
            "http_status": 401,
            "error": "IDENTITY_REQUIRED",
            "original_browser": True,
            "binding_matches": True,
        })

    def test_browser_evidence_accepts_equivalent_numeric_offset(self):
        result = self.evaluate(self.evidence(
            observed_at="2023-11-15T06:13:20.000+08:00"))

        self.assertEqual(result["observed_at_ms"], OBSERVED)

    def test_original_browser_requires_literal_true(self):
        cases = (
            ("true", True, True),
            ("absent", None, False),
            ("string", "true", False),
            ("integer", 1, False),
            ("false", False, False),
        )
        for name, value, expected in cases:
            with self.subTest(case=name):
                evidence_value = self.evidence()
                if value is None:
                    evidence_value.pop("original_browser")
                else:
                    evidence_value["original_browser"] = value
                self.assertIs(self.evaluate(evidence_value)["original_browser"], expected)

    def test_browser_binding_requires_record_flow_and_activity_match(self):
        cases = (
            ("record", {"record_fingerprint": OTHER}),
            ("flow", {"flow": "F2"}),
            ("activity", {"last_activity_ms": OBSERVED + 1}),
            ("record-missing", {"record_fingerprint": None}),
            ("flow-missing", {"flow": None}),
            ("activity-missing", {"last_activity_ms": None}),
        )
        for name, override in cases:
            with self.subTest(case=name):
                evidence_value = self.evidence()
                for key, value in override.items():
                    if value is None:
                        evidence_value.pop(key)
                    else:
                        evidence_value[key] = value
                self.assertFalse(self.evaluate(evidence_value)["binding_matches"])

    def test_browser_binding_requires_typed_last_activity(self):
        """A JSON float/bool or an omitted field must not satisfy the binding."""
        cases = (
            ("float", {"last_activity_ms": float(OBSERVED)}, None),
            ("bool", {"last_activity_ms": True}, {"label": "F1", "last_activity_ms": 1}),
            ("both-missing", {"last_activity_ms": None}, {"label": "F1",
                                                          "last_activity_ms": None}),
        )
        for name, override, flow in cases:
            with self.subTest(case=name):
                evidence_value = self.evidence()
                for key, value in override.items():
                    if value is None:
                        evidence_value.pop(key)
                    else:
                        evidence_value[key] = value
                self.assertFalse(self.evaluate(evidence_value, flow)["binding_matches"])

    def test_kind_status_and_error_are_allowlisted(self):
        self.assertEqual(self.evaluate(self.evidence(kind="user_reported"))["kind"],
                         "user_reported")
        for status in (100, 599):
            with self.subTest(status=status):
                self.assertEqual(self.evaluate(self.evidence(http_status=status))["http_status"],
                                 status)
        for status in (99, 600, True, "401"):
            with self.subTest(status=status):
                self.assert_code("BROWSER_EVIDENCE_INVALID", self.evaluate,
                                 self.evidence(http_status=status))
        self.assertIsNone(self.evaluate(self.evidence(error=None))["error"])
        for error in ("OTHER", ""):
            with self.subTest(error=error):
                self.assert_code("BROWSER_EVIDENCE_INVALID", self.evaluate,
                                 self.evidence(error=error))
        self.assert_code("BROWSER_EVIDENCE_INVALID", self.evaluate,
                         self.evidence(kind="reported"))

    def test_schema_and_observed_at_are_strict(self):
        self.assert_code("BROWSER_EVIDENCE_FORMAT_UNSUPPORTED", self.evaluate,
                         self.evidence(schema="other"))
        for name, value in (
            ("missing", None),
            ("naive", "2023-11-14T22:13:20"),
            ("float", 1700000000000),
            ("bad-offset", "2023-11-14T22:13:20+24:00"),
        ):
            with self.subTest(case=name):
                evidence_value = self.evidence()
                if value is None:
                    evidence_value.pop("observed_at")
                else:
                    evidence_value["observed_at"] = value
                self.assert_code("BROWSER_EVIDENCE_INVALID", self.evaluate,
                                 evidence_value)

    def test_errors_do_not_echo_secret_or_path(self):
        secret = "SyntheticBrowserSecret"
        evidence_value = self.evidence(observed_at="not-a-time-" + secret)

        error = self.assert_code("BROWSER_EVIDENCE_INVALID", self.evaluate,
                                 evidence_value)

        for text in (str(error), repr(error)):
            self.assertNotIn(secret, text)
            self.assertNotIn(str(self.path), text)


@unittest.skipUnless(os.name == "posix",
                     "POSIX 0600 permission bits cannot be enforced on Windows")
class BrowserPermissionTest(StateTestBase):
    def test_browser_evidence_must_be_private(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "browser-evidence.json"
        value = {
            "schema": "xiaozhi-browser-evidence-v1", "kind": "captured",
            "observed_at": "2023-11-14T22:13:20.000Z", "http_status": 401,
            "error": "IDENTITY_REQUIRED", "original_browser": True,
            "record_fingerprint": RECORD, "flow": "F1",
            "last_activity_ms": OBSERVED,
        }
        write_json(path, value, mode=0o644)
        self.assert_code("INPUT_NOT_PRIVATE", browser_evidence, path, record(),
                         "F1", {"label": "F1", "last_activity_ms": OBSERVED})


if __name__ == "__main__":
    unittest.main()
