"""Pure evaluation core for the customer identity acceptance checks.

This module never performs IO.  It accepts already-normalised, already-safe
observations and returns a result made only from explicitly whitelisted keys.
Unknown input fields are never copied into the result.
"""
import re

from . import timeutil
from .codes import PUBLIC_CODES


SCENARIOS = frozenset(("login", "logout", "two-account", "idle"))
PHASES = frozenset(("baseline", "check"))
EXIT_CODES = {"pass": 0, "unmet": 1, "insufficient": 3, "error": 4}
BASELINE_SCHEMA = "xiaozhi-identity-state-v1"
IDLE_MS = 1800000
ENVIRONMENT_KEYS = ("containers", "db_target", "proxy_target", "schema")
STORED_STATUSES = frozenset((
    "AWAITING_IDENTITY", "EXCHANGING", "IDENTITY_VERIFIED",
    "SESSION_ENDED", "IDENTITY_FAILED", "IDENTITY_REFUSED",
))
AUTHORIZATION_STATUSES = frozenset((
    "ACTIVE", "REFRESHING", "REFRESH_UNCERTAIN", "REAUTH_REQUIRED", "NONE",
))
LOG_STATUSES = frozenset(("complete", "incomplete", "missing", "not_configured"))

# Reason and note codes are fixed public constants.  No free text is emitted.
SCENARIO_INVALID = "SCENARIO_INVALID"
PHASE_INVALID = "PHASE_INVALID"
OBSERVATION_INVALID = "OBSERVATION_INVALID"
TARGETS_INVALID = "TARGETS_INVALID"
TRUNCATED = "TRUNCATED"
BASELINE_ONLY = "BASELINE_ONLY"
BASELINE_REQUIRED = "BASELINE_REQUIRED"
BASELINE_INVALID = "BASELINE_INVALID"
BASELINE_TIME = "BASELINE_TIME"
BASELINE_TARGET_MISSING = "BASELINE_TARGET_MISSING"
BASELINE_TARGET_CHANGED = "BASELINE_TARGET_CHANGED"
BASELINE_STATUS = "BASELINE_STATUS"
BASELINE_AUTHORIZATION = "BASELINE_AUTHORIZATION"
BASELINE_DISTINCT = "BASELINE_DISTINCT"
WINDOW_REQUIRED = "WINDOW_REQUIRED"
WINDOW_NOT_STARTED = "WINDOW_NOT_STARTED"
WINDOW_OPEN = "WINDOW_OPEN"
TARGET_NOT_FOUND = "TARGET_NOT_FOUND"
TARGET_AMBIGUOUS = "TARGET_AMBIGUOUS"
NO_FLOWS = "NO_FLOWS"
NO_IDENTITY_TARGET = "NO_IDENTITY_TARGET"
IDENTITY_FAILED = "IDENTITY_FAILED"
IDENTITY_REFUSED = "IDENTITY_REFUSED"
FLOW_EXPIRED = "FLOW_EXPIRED"
FLOW_PENDING = "FLOW_PENDING"
SESSION_ENDED = "SESSION_ENDED"
FLOW_STATE_UNKNOWN = "FLOW_STATE_UNKNOWN"
AUTHORIZATION_NOT_ACTIVE = "AUTHORIZATION_NOT_ACTIVE"
AUTHORIZATION_REFRESHING = "AUTHORIZATION_REFRESHING"
AUTHORIZATION_UNKNOWN = "AUTHORIZATION_UNKNOWN"
MATCH_EVIDENCE_MISSING = "MATCH_EVIDENCE_MISSING"
PRIOR_RECEIPT_MATCH = "PRIOR_RECEIPT_MATCH"
RECEIPT_NOT_MATCHED = "RECEIPT_NOT_MATCHED"
RECEIPT_CONTINUITY = "RECEIPT_CONTINUITY"
SESSION_NOT_ENDED = "SESSION_NOT_ENDED"
SESSION_STATE_UNKNOWN = "SESSION_STATE_UNKNOWN"
LOG_EVIDENCE_MISSING = "LOG_EVIDENCE_MISSING"
LOG_INCOMPLETE = "LOG_INCOMPLETE"
LOG_MISSING = "LOG_MISSING"
LOG_NOT_CONFIGURED = "LOG_NOT_CONFIGURED"
LOG_COUNTS_MISSING = "LOG_COUNTS_MISSING"
LOG_NO_204 = "LOG_NO_204"
LOG_SOURCE_MISMATCH = "LOG_SOURCE_MISMATCH"
IDENTITY_PAIR_MISSING = "IDENTITY_PAIR_MISSING"
IDENTITY_PAIR_AMBIGUOUS = "IDENTITY_PAIR_AMBIGUOUS"
IDENTITIES_NOT_DISTINCT = "IDENTITIES_NOT_DISTINCT"
IDENTITY_MISSING = "IDENTITY_MISSING"
A_AUTHORIZATION_CHANGED = "A_AUTHORIZATION_CHANGED"
A_RECEIPT_MATCHED = "A_RECEIPT_MATCHED"
A_SESSION_ENDED = "A_SESSION_ENDED"
A_SESSION_STATE_UNKNOWN = "A_SESSION_STATE_UNKNOWN"
B_RECEIPT_NOT_MATCHED = "B_RECEIPT_NOT_MATCHED"
B_SESSION_NOT_ENDED = "B_SESSION_NOT_ENDED"
CLOCK_SKEW = "CLOCK_SKEW"
CLOCK_SKEW_EVIDENCE = "CLOCK_SKEW_EVIDENCE"
ACTIVITY_UNKNOWN = "ACTIVITY_UNKNOWN"
ACTIVITY_CHANGED = "ACTIVITY_CHANGED"
IDLE_NOT_REACHED = "IDLE_NOT_REACHED"
BROWSER_EVIDENCE_MISSING = "BROWSER_EVIDENCE_MISSING"
BROWSER_USER_REPORTED = "BROWSER_USER_REPORTED"
BROWSER_EVIDENCE_INVALID = "BROWSER_EVIDENCE_INVALID"
BROWSER_BINDING_MISMATCH = "BROWSER_BINDING_MISMATCH"
BROWSER_EVIDENCE_BEFORE_DEADLINE = "BROWSER_EVIDENCE_BEFORE_DEADLINE"
BROWSER_EVIDENCE_IN_FUTURE = "BROWSER_EVIDENCE_IN_FUTURE"
BROWSER_ACCESSIBLE = "BROWSER_ACCESSIBLE"
BROWSER_STATUS_UNEXPECTED = "BROWSER_STATUS_UNEXPECTED"
ENVIRONMENT_CONTAINERS = "ENVIRONMENT_CONTAINERS"
ENVIRONMENT_DB_TARGET = "ENVIRONMENT_DB_TARGET"
ENVIRONMENT_PROXY_TARGET = "ENVIRONMENT_PROXY_TARGET"
ENVIRONMENT_SCHEMA = "ENVIRONMENT_SCHEMA"
ENVIRONMENT_INVALID = "ENVIRONMENT_INVALID"
MULTIPLE_MATCHED_RECEIPTS = "MULTIPLE_MATCHED_RECEIPTS"
OTHER_ISSUER_RECEIPTS = "OTHER_ISSUER_RECEIPTS"
LOG_BOUNDARY_LINES = "LOG_BOUNDARY_LINES"
LOG_POST_400 = "LOG_POST_400"

_LABEL = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,31}$")
_INVALID = object()
_MISSING = object()


class _Flow(object):
    __slots__ = (
        "record", "label", "creation_order", "stored_status", "effective_status",
        "created_at_ms", "expires_at_ms", "identity_issued_at_ms",
        "sid_present", "identity_present", "authorization", "last_activity_ms",
        "matched_before_window", "matched_in_window", "matched_received_at_ms",
        "target",
    )

    def __init__(self):
        self.record = "A"
        self.label = "F1"
        self.creation_order = None
        self.stored_status = "UNKNOWN"
        self.effective_status = "UNKNOWN"
        self.created_at_ms = None
        self.expires_at_ms = None
        self.identity_issued_at_ms = None
        self.sid_present = False
        self.identity_present = False
        self.authorization = "UNKNOWN"
        self.last_activity_ms = None
        self.matched_before_window = None
        self.matched_in_window = None
        self.matched_received_at_ms = []
        self.target = False


class _Observation(object):
    __slots__ = (
        "invalid_reason", "observed_at_ms", "clock_skew_ms", "window",
        "environment", "environment_reasons", "flows", "receipts", "log",
        "truncated", "baseline_valid", "baseline_reasons", "identity_pairs",
        "identities_distinct",
    )

    def __init__(self):
        self.invalid_reason = None
        self.observed_at_ms = None
        self.clock_skew_ms = None
        self.window = None
        self.environment = dict((key, "error") for key in ENVIRONMENT_KEYS)
        self.environment_reasons = []
        self.flows = {"A": [], "B": []}
        self.receipts = {
            "issuer_total": None,
            "issuer_before_window": None,
            "issuer_in_window": None,
            "other_issuer_total": None,
            "since_baseline": None,
        }
        self.log = {
            "status": None,
            "post_204_in_window": None,
            "post_400_in_window": None,
            "boundary_lines": None,
            "reasons": [],
        }
        self.truncated = False
        self.baseline_valid = None
        self.baseline_reasons = []
        self.identity_pairs = None
        self.identities_distinct = None


class _Baseline(object):
    __slots__ = (
        "invalid", "observed_at_ms", "flows", "targets",
        "identities_distinct", "issuer_total",
    )

    def __init__(self):
        self.invalid = None
        self.observed_at_ms = None
        self.flows = {"A": [], "B": []}
        self.targets = {}
        self.identities_distinct = None
        self.issuer_total = None


def _integer(value, minimum=None, maximum=None):
    if type(value) is not int:
        return None
    if minimum is not None and value < minimum:
        return None
    if maximum is not None and value > maximum:
        return None
    return value


def _count(value):
    return _integer(value, 0)


def _ms(value):
    return _integer(value, 0, timeutil.MAX_MS)


def _enum(value, allowed, fallback="UNKNOWN"):
    return value if isinstance(value, str) and value in allowed else fallback


def _codes(value):
    if not isinstance(value, list):
        return []
    result = []
    for item in value[:32]:
        if isinstance(item, str) and item in PUBLIC_CODES:
            if item not in result:
                result.append(item)
    return result


def _read_flow(record, index, raw, observed_at_ms, baseline=False):
    if not isinstance(raw, dict):
        return None
    flow = _Flow()
    flow.record = record
    label = raw.get("label")
    if isinstance(label, str) and _LABEL.fullmatch(label):
        flow.label = label
    else:
        flow.label = "F%d" % (index + 1)
    flow.creation_order = _count(raw.get("creation_order"))
    status = raw.get("stored_status")
    if status is None:
        status = raw.get("status")
    flow.stored_status = _enum(status, STORED_STATUSES)
    flow.created_at_ms = _ms(raw.get("created_at_ms"))
    flow.expires_at_ms = _ms(raw.get("expires_at_ms"))
    flow.identity_issued_at_ms = _ms(raw.get("identity_issued_at_ms"))
    flow.sid_present = raw.get("sid_present") is True
    flow.identity_present = raw.get("identity_present") is True
    if baseline and "identity_present" not in raw:
        flow.identity_present = flow.stored_status in ("IDENTITY_VERIFIED", "SESSION_ENDED")
    flow.authorization = _enum(raw.get("authorization"), AUTHORIZATION_STATUSES)
    flow.last_activity_ms = _ms(raw.get("last_activity_ms"))
    if "matched_before_window" in raw:
        flow.matched_before_window = _count(raw.get("matched_before_window"))
    if "matched_in_window" in raw:
        flow.matched_in_window = _count(raw.get("matched_in_window"))
    received = raw.get("matched_received_at_ms")
    if isinstance(received, list):
        for value in received[:500]:
            stamp = _ms(value)
            if stamp is not None:
                flow.matched_received_at_ms.append(stamp)
    if (flow.stored_status in ("AWAITING_IDENTITY", "EXCHANGING")
            and flow.expires_at_ms is not None
            and observed_at_ms is not None
            and flow.expires_at_ms <= observed_at_ms):
        flow.effective_status = "FLOW_EXPIRED"
    else:
        flow.effective_status = flow.stored_status
    return flow


def _read_flows(raw, observed_at_ms, baseline=False):
    flows = {"A": [], "B": []}
    invalid = False
    if raw is None:
        return flows, invalid
    if not isinstance(raw, dict):
        return flows, True
    for record in ("A", "B"):
        items = raw.get(record)
        if items is None:
            continue
        if not isinstance(items, list):
            invalid = True
            continue
        for index, item in enumerate(items[:1000]):
            flow = _read_flow(record, index, item, observed_at_ms, baseline=baseline)
            if flow is None:
                invalid = True
                continue
            flows[record].append(flow)
    return flows, invalid


def _read_receipts(raw):
    result = {
        "issuer_total": None,
        "issuer_before_window": None,
        "issuer_in_window": None,
        "other_issuer_total": None,
        "since_baseline": None,
    }
    if isinstance(raw, dict):
        for key in result:
            result[key] = _count(raw.get(key))
    return result


def _read_log(raw):
    result = {
        "status": None,
        "post_204_in_window": None,
        "post_400_in_window": None,
        "boundary_lines": None,
        "reasons": [],
    }
    if not isinstance(raw, dict):
        return result
    status = raw.get("status")
    if isinstance(status, str) and status in LOG_STATUSES:
        result["status"] = status
    result["post_204_in_window"] = _count(raw.get("post_204_in_window"))
    result["post_400_in_window"] = _count(raw.get("post_400_in_window"))
    result["boundary_lines"] = _count(raw.get("boundary_lines"))
    result["reasons"] = _codes(raw.get("reasons"))
    return result


def _read_environment(raw):
    statuses = dict((key, "error") for key in ENVIRONMENT_KEYS)
    reasons = []
    if not isinstance(raw, dict):
        return statuses, [ENVIRONMENT_INVALID]
    mapping = {
        "containers": ENVIRONMENT_CONTAINERS,
        "db_target": ENVIRONMENT_DB_TARGET,
        "proxy_target": ENVIRONMENT_PROXY_TARGET,
        "schema": ENVIRONMENT_SCHEMA,
    }
    for key in ENVIRONMENT_KEYS:
        if raw.get(key) == "ok":
            statuses[key] = "ok"
        else:
            reasons.append(mapping[key])
    return statuses, reasons


def _read_window(raw):
    if raw is None:
        return None
    if not isinstance(raw, dict):
        return _INVALID
    start = _ms(raw.get("start_ms"))
    end = _ms(raw.get("end_ms"))
    if start is None or end is None or start >= end:
        return _INVALID
    return (start, end)


def _read_pairs(raw):
    """None means the key was absent; a list (possibly empty) means supplied."""
    if raw is None:
        return None
    if not isinstance(raw, list):
        return []
    pairs = []
    for item in raw[:2500]:
        if not isinstance(item, dict):
            continue
        a_label = item.get("a_label")
        b_label = item.get("b_label")
        distinct = item.get("distinct")
        if (isinstance(a_label, str) and _LABEL.fullmatch(a_label)
                and isinstance(b_label, str) and _LABEL.fullmatch(b_label)
                and type(distinct) is bool):
            pairs.append((a_label, b_label, distinct))
    return pairs


def _read_observation(raw):
    observation = _Observation()
    if not isinstance(raw, dict):
        observation.invalid_reason = OBSERVATION_INVALID
        return observation
    observation.observed_at_ms = _ms(raw.get("observed_at_ms"))
    if observation.observed_at_ms is None:
        observation.invalid_reason = OBSERVATION_INVALID
    observation.clock_skew_ms = _integer(
        raw.get("clock_skew_ms"), -timeutil.MAX_MS, timeutil.MAX_MS
    )
    window = _read_window(raw.get("window"))
    if window is _INVALID:
        observation.invalid_reason = observation.invalid_reason or OBSERVATION_INVALID
    else:
        observation.window = window
    observation.environment, observation.environment_reasons = _read_environment(
        raw.get("environment")
    )
    observation.flows, flows_invalid = _read_flows(
        raw.get("flows"), observation.observed_at_ms
    )
    if flows_invalid:
        observation.invalid_reason = observation.invalid_reason or OBSERVATION_INVALID
    observation.receipts = _read_receipts(raw.get("receipts"))
    observation.log = _read_log(raw.get("log"))
    observation.truncated = raw.get("truncated") is True
    if type(raw.get("baseline_valid")) is bool:
        observation.baseline_valid = raw.get("baseline_valid")
    observation.baseline_reasons = _codes(raw.get("baseline_reasons"))
    observation.identity_pairs = _read_pairs(raw.get("identity_pairs"))
    if type(raw.get("identities_distinct")) is bool:
        observation.identities_distinct = raw.get("identities_distinct")
    return observation


def _read_targets(raw):
    if raw is None:
        return {}, None
    if not isinstance(raw, dict):
        return {}, TARGETS_INVALID
    targets = {}
    for record in ("A", "B"):
        value = raw.get(record)
        if value is None:
            continue
        if not isinstance(value, str) or not _LABEL.fullmatch(value):
            return {}, TARGETS_INVALID
        targets[record] = value
    return targets, None


def _read_baseline(raw, scenario):
    baseline = _Baseline()
    if not isinstance(raw, dict):
        baseline.invalid = BASELINE_INVALID
        return baseline
    schema = raw.get("schema")
    if schema is not None and schema != BASELINE_SCHEMA:
        baseline.invalid = BASELINE_INVALID
        return baseline
    if raw.get("scenario") != scenario:
        baseline.invalid = BASELINE_INVALID
        return baseline
    baseline.observed_at_ms = _ms(raw.get("observed_at_ms"))
    if baseline.observed_at_ms is None:
        baseline.invalid = BASELINE_INVALID
        return baseline
    baseline.flows, flows_invalid = _read_flows(
        raw.get("flows"), baseline.observed_at_ms, baseline=True
    )
    if flows_invalid:
        baseline.invalid = BASELINE_INVALID
        return baseline
    baseline.targets, target_error = _read_targets(raw.get("targets"))
    if target_error:
        baseline.invalid = BASELINE_INVALID
        return baseline
    if type(raw.get("identities_distinct")) is bool:
        baseline.identities_distinct = raw.get("identities_distinct")
    baseline.issuer_total = _count(raw.get("issuer_total"))
    return baseline


def _add_check(checks, check_id, kind, result, reason=None, value=_MISSING):
    check = {"id": check_id, "kind": kind, "result": result, "reason": reason}
    if value is not _MISSING:
        check["value"] = value
    checks.append(check)


def _add_note(notes, code):
    if code and code not in notes:
        notes.append(code)


def _select_target(flows, requested, fallback_single):
    if requested is not None:
        matches = [flow for flow in flows if flow.label == requested]
        if len(matches) == 1:
            return matches[0], None, [matches[0].label]
        if len(matches) > 1:
            return None, TARGET_AMBIGUOUS, [flow.label for flow in matches]
        return None, TARGET_NOT_FOUND, []
    candidates = [
        flow for flow in flows
        if flow.stored_status in ("IDENTITY_VERIFIED", "SESSION_ENDED")
        and flow.identity_present
    ]
    if len(candidates) == 1:
        return candidates[0], None, [candidates[0].label]
    if len(candidates) > 1:
        return None, TARGET_AMBIGUOUS, [flow.label for flow in candidates]
    if fallback_single and len(flows) == 1:
        return flows[0], None, [flows[0].label]
    if not flows:
        return None, NO_FLOWS, []
    return None, NO_IDENTITY_TARGET, [flow.label for flow in flows]


def _select_and_check(checks, flows, requested, fallback_single, check_id):
    flow, reason, candidates = _select_target(flows, requested, fallback_single)
    if flow is None:
        _add_check(checks, check_id, "premise", "insufficient", reason, candidates)
        return None
    flow.target = True
    _add_check(checks, check_id, "premise", "pass", None, flow.label)
    return flow


def _add_not_truncated(checks, observation):
    if observation.truncated:
        _add_check(checks, "not_truncated", "premise", "insufficient", TRUNCATED)
    else:
        _add_check(checks, "not_truncated", "premise", "pass")


def _add_clock_skew_check(checks, observation):
    skew = observation.clock_skew_ms
    if skew is None:
        _add_check(checks, "clock_skew", "premise", "insufficient",
                   CLOCK_SKEW_EVIDENCE)
    elif skew != 0:
        _add_check(checks, "clock_skew", "premise", "insufficient", CLOCK_SKEW, skew)
    else:
        _add_check(checks, "clock_skew", "premise", "pass")


def _window_premises(checks, notes, observation):
    """Returns (window_usable, window_is_open)."""
    if observation.window is None:
        _add_check(checks, "window_available", "premise", "insufficient", WINDOW_REQUIRED)
        return False, False
    start, end = observation.window
    if observation.observed_at_ms is None or observation.observed_at_ms < start:
        _add_check(checks, "window_started", "premise", "insufficient", WINDOW_NOT_STARTED)
        return False, False
    _add_check(checks, "window_started", "premise", "pass")
    if observation.observed_at_ms < end:
        _add_note(notes, WINDOW_OPEN)
        return True, True
    return True, False


def _verdict(checks, window_open):
    if any(check["result"] == "error" for check in checks):
        return "error"
    if any(check["kind"] == "premise" and check["result"] in ("insufficient", "unmet")
           for check in checks):
        return "insufficient"
    criteria = [check for check in checks if check["kind"] == "criterion"]
    if any(check["result"] == "unmet" for check in criteria):
        return "insufficient" if window_open else "unmet"
    if any(check["result"] == "insufficient" for check in criteria):
        return "insufficient"
    return "pass"


def _public_flow(flow):
    return {
        "label": flow.label,
        "stored_status": flow.stored_status,
        "effective_status": flow.effective_status,
        "sid_present": flow.sid_present,
        "identity_present": flow.identity_present,
        "authorization": flow.authorization,
        "created_at_ms": flow.created_at_ms,
        "expires_at_ms": flow.expires_at_ms,
        "identity_issued_at_ms": flow.identity_issued_at_ms,
        "last_activity_ms": flow.last_activity_ms,
        "target": flow.target,
        "matched_before_window": flow.matched_before_window,
        "matched_in_window": flow.matched_in_window,
        "matched_received_at_ms": list(flow.matched_received_at_ms),
    }


def _public_flows(observation):
    return {
        "A": [_public_flow(flow) for flow in observation.flows["A"]],
        "B": [_public_flow(flow) for flow in observation.flows["B"]],
    }


def _public_window(observation):
    if observation.window is None:
        return None
    start, end = observation.window
    closed = (observation.observed_at_ms is not None
              and observation.observed_at_ms >= end)
    return {"start_ms": start, "end_ms": end, "closed": closed}


def _public_browser(browser):
    if browser is None:
        return None
    return {
        "kind": browser["kind"],
        "observed_at_ms": browser["observed_at_ms"],
        "http_status": browser["http_status"],
        "error": browser["error"],
        "original_browser": browser["original_browser"],
        "binding_matches": browser["binding_matches"],
    }


def _observation_notes(observation):
    notes = []
    for code in observation.baseline_reasons:
        _add_note(notes, code)
    for code in observation.log["reasons"]:
        _add_note(notes, code)
    if observation.log["boundary_lines"] is not None and observation.log["boundary_lines"] > 0:
        _add_note(notes, LOG_BOUNDARY_LINES)
    if observation.log["post_400_in_window"] is not None and observation.log["post_400_in_window"] > 0:
        _add_note(notes, LOG_POST_400)
    if observation.receipts["other_issuer_total"] is not None and observation.receipts["other_issuer_total"] > 0:
        _add_note(notes, OTHER_ISSUER_RECEIPTS)
    return notes


def _build_result(status, scenario, phase, observation, checks, selected_targets,
                  notes, extra_reasons, baseline_ready, identities_distinct, browser):
    reasons = []
    for check in checks:
        if check["result"] in ("unmet", "insufficient", "error"):
            _add_note(reasons, check.get("reason"))
    for code in extra_reasons:
        _add_note(reasons, code)
    if status == "pass":
        reasons = []
    all_notes = _observation_notes(observation)
    for code in notes:
        _add_note(all_notes, code)
    return {
        "scenario": scenario if isinstance(scenario, str) and scenario in SCENARIOS else "unknown",
        "phase": phase if isinstance(phase, str) and phase in PHASES else "unknown",
        "result": status,
        "exit_code": EXIT_CODES[status],
        "reasons": reasons,
        "notes": all_notes,
        "baseline_ready": bool(baseline_ready),
        "observed_at_ms": observation.observed_at_ms,
        "clock_skew_ms": observation.clock_skew_ms,
        "window": _public_window(observation),
        "environment": dict(
            (key, "ok" if observation.environment.get(key) == "ok" else "error")
            for key in ENVIRONMENT_KEYS
        ),
        "flows": _public_flows(observation),
        "selected_targets": dict(
            (record, label) for record, label in selected_targets.items()
            if record in ("A", "B") and isinstance(label, str) and _LABEL.fullmatch(label)
        ),
        "identities_distinct": identities_distinct if type(identities_distinct) is bool else None,
        "receipts": {
            "issuer_total": observation.receipts["issuer_total"],
            "issuer_before_window": observation.receipts["issuer_before_window"],
            "issuer_in_window": observation.receipts["issuer_in_window"],
            "other_issuer_total": observation.receipts["other_issuer_total"],
            "since_baseline": observation.receipts["since_baseline"],
        },
        "log": {
            "status": observation.log["status"],
            "post_204_in_window": observation.log["post_204_in_window"],
            "post_400_in_window": observation.log["post_400_in_window"],
            "boundary_lines": observation.log["boundary_lines"],
            "reasons": list(observation.log["reasons"]),
        },
        "browser_evidence": _public_browser(browser),
        "checks": checks,
    }


def _error_result(scenario, phase, observation, reason, extra_reasons=None):
    checks = []
    _add_check(checks, "execution", "premise", "error", reason)
    return _build_result(
        "error", scenario, phase, observation, checks, {},
        [], [reason] + list(extra_reasons or []), False, None, None
    )


def _finish(scenario, observation, checks, notes, selected_targets,
            baseline_ready, identities_distinct=None, browser=None,
            window_open=False):
    verdict = _verdict(checks, window_open)
    extra_reasons = []
    if (window_open and verdict == "insufficient"
            and any(check["kind"] == "criterion" and check["result"] == "unmet"
                    for check in checks)):
        extra_reasons.append(WINDOW_OPEN)
    return _build_result(
        verdict, scenario, "check", observation, checks, selected_targets,
        notes, extra_reasons, baseline_ready, identities_distinct, browser
    )


def _baseline_gate(checks, observation, baseline_raw, scenario, required):
    """Returns (usable, baseline-or-None).  Adds premise checks."""
    if baseline_raw is None:
        if required:
            _add_check(checks, "baseline_available", "premise", "insufficient",
                       BASELINE_REQUIRED)
            return False, None
        if observation.baseline_valid is False:
            _add_check(checks, "baseline_available", "premise", "insufficient",
                       BASELINE_INVALID)
            return False, None
        _add_check(checks, "baseline_available", "premise", "pass")
        return True, None
    if observation.baseline_valid is not True:
        _add_check(checks, "baseline_available", "premise", "insufficient",
                   BASELINE_INVALID)
        return False, None
    baseline = _read_baseline(baseline_raw, scenario)
    if baseline.invalid is not None:
        _add_check(checks, "baseline_available", "premise", "insufficient",
                   BASELINE_INVALID)
        return False, None
    _add_check(checks, "baseline_available", "premise", "pass")
    return True, baseline


def _baseline_target_flow(baseline, record):
    label = baseline.targets.get(record)
    if label is None:
        return None
    matches = [flow for flow in baseline.flows[record] if flow.label == label]
    if len(matches) != 1:
        return None
    return matches[0]


def _check_creation_order(checks, check_id, prior, current):
    if (prior is None or current is None or prior.creation_order is None
            or current.creation_order is None
            or prior.creation_order != current.creation_order):
        _add_check(checks, check_id, "premise", "insufficient",
                   BASELINE_TARGET_CHANGED)
        return False
    _add_check(checks, check_id, "premise", "pass")
    return True


def _logout_baseline_checks(checks, baseline, current, observation):
    if baseline is None:
        return True
    usable = True
    if observation.window is not None:
        if baseline.observed_at_ms >= observation.window[0]:
            _add_check(checks, "baseline_time", "premise", "insufficient",
                       BASELINE_TIME)
            usable = False
        else:
            _add_check(checks, "baseline_time", "premise", "pass")
    prior = _baseline_target_flow(baseline, "A")
    if prior is None:
        _add_check(checks, "baseline_target", "premise", "insufficient",
                   BASELINE_TARGET_MISSING)
        return False
    if not _check_creation_order(checks, "baseline_target", prior, current):
        usable = False
    if prior.stored_status != "IDENTITY_VERIFIED":
        _add_check(checks, "baseline_status", "premise", "insufficient",
                   BASELINE_STATUS, prior.stored_status)
        usable = False
    else:
        _add_check(checks, "baseline_status", "premise", "pass")
    return usable


def _idle_baseline_checks(checks, baseline, current, observation):
    if baseline is None:
        return True
    usable = True
    prior = _baseline_target_flow(baseline, "A")
    if prior is None:
        _add_check(checks, "baseline_target", "premise", "insufficient",
                   BASELINE_TARGET_MISSING)
        return False
    if not _check_creation_order(checks, "baseline_target", prior, current):
        usable = False
    if (prior.last_activity_ms is None or current.last_activity_ms is None
            or prior.last_activity_ms != current.last_activity_ms):
        _add_check(checks, "baseline_activity", "premise", "insufficient",
                   ACTIVITY_CHANGED)
        usable = False
    else:
        _add_check(checks, "baseline_activity", "premise", "pass")
    if baseline.observed_at_ms >= observation.observed_at_ms:
        _add_check(checks, "baseline_time", "premise", "insufficient",
                   BASELINE_TIME)
        usable = False
    else:
        _add_check(checks, "baseline_time", "premise", "pass")
    return usable


def _two_account_baseline_checks(checks, baseline, current_a, current_b, observation):
    usable = True
    if observation.window is not None:
        if baseline.observed_at_ms >= observation.window[0]:
            _add_check(checks, "baseline_time", "premise", "insufficient",
                       BASELINE_TIME)
            usable = False
        else:
            _add_check(checks, "baseline_time", "premise", "pass")
    for record, current in (("A", current_a), ("B", current_b)):
        suffix = record.lower()
        prior = _baseline_target_flow(baseline, record)
        if prior is None:
            _add_check(checks, "baseline_target_" + suffix, "premise",
                       "insufficient", BASELINE_TARGET_MISSING)
            usable = False
            continue
        if not _check_creation_order(checks, "baseline_target_" + suffix, prior, current):
            usable = False
        if prior.stored_status != "IDENTITY_VERIFIED":
            _add_check(checks, "baseline_status_" + suffix, "premise",
                       "insufficient", BASELINE_STATUS, prior.stored_status)
            usable = False
        else:
            _add_check(checks, "baseline_status_" + suffix, "premise", "pass")
        if prior.authorization != "ACTIVE":
            _add_check(checks, "baseline_authorization_" + suffix, "premise",
                       "insufficient", BASELINE_AUTHORIZATION, prior.authorization)
            usable = False
        else:
            _add_check(checks, "baseline_authorization_" + suffix, "premise", "pass")
    if baseline.identities_distinct is not True:
        _add_check(checks, "baseline_distinct", "premise", "insufficient",
                   BASELINE_DISTINCT)
        usable = False
    else:
        _add_check(checks, "baseline_distinct", "premise", "pass")
    return usable


def _resolve_distinct(observation, flow_a, flow_b):
    if observation.identity_pairs is not None:
        values = [
            distinct for a_label, b_label, distinct in observation.identity_pairs
            if a_label == flow_a.label and b_label == flow_b.label
        ]
        if not values:
            return None, IDENTITY_PAIR_MISSING
        if len(set(values)) != 1:
            return None, IDENTITY_PAIR_AMBIGUOUS
        return values[0], None
    if observation.identities_distinct is not None:
        return observation.identities_distinct, None
    return None, IDENTITY_PAIR_MISSING


def _add_distinct_check(checks, observation, flow_a, flow_b):
    value, reason = _resolve_distinct(observation, flow_a, flow_b)
    if value is None:
        _add_check(checks, "identities_distinct", "premise", "insufficient", reason)
        return None
    if value is True:
        _add_check(checks, "identities_distinct", "premise", "pass")
        return True
    _add_check(checks, "identities_distinct", "premise", "insufficient",
               IDENTITIES_NOT_DISTINCT)
    return False


def _log_reason(status):
    if status == "incomplete":
        return LOG_INCOMPLETE
    if status == "missing":
        return LOG_MISSING
    if status == "not_configured":
        return LOG_NOT_CONFIGURED
    return LOG_EVIDENCE_MISSING


def _add_log_check(checks, observation, matched_in_window):
    status = observation.log["status"]
    post_204 = observation.log["post_204_in_window"]
    if status == "complete":
        if post_204 is None:
            _add_check(checks, "entry_log_204", "criterion", "insufficient",
                       LOG_COUNTS_MISSING)
        elif post_204 >= 1:
            _add_check(checks, "entry_log_204", "criterion", "pass", None, post_204)
        elif matched_in_window is None:
            _add_check(checks, "entry_log_204", "criterion", "insufficient",
                       MATCH_EVIDENCE_MISSING)
        elif matched_in_window > 0:
            _add_check(checks, "entry_log_204", "criterion", "insufficient",
                       LOG_SOURCE_MISMATCH, post_204)
        else:
            _add_check(checks, "entry_log_204", "criterion", "unmet", LOG_NO_204,
                       post_204)
    else:
        _add_check(checks, "entry_log_204", "criterion", "insufficient",
                   _log_reason(status))


def _add_absence_check(checks, observation, matched_in_window, continuity_ok):
    """Block a no-notify claim when the log or receipt continuity cannot support it."""
    if matched_in_window is None:
        _add_check(checks, "no_notify_supported", "premise", "insufficient",
                   MATCH_EVIDENCE_MISSING)
        return
    if matched_in_window > 0:
        _add_check(checks, "no_notify_supported", "premise", "pass")
        return
    status = observation.log["status"]
    post_204 = observation.log["post_204_in_window"]
    if status != "complete":
        _add_check(checks, "no_notify_supported", "premise", "insufficient",
                   _log_reason(status))
    elif post_204 is None:
        _add_check(checks, "no_notify_supported", "premise", "insufficient",
                   LOG_COUNTS_MISSING)
    elif post_204 >= 1:
        _add_check(checks, "no_notify_supported", "premise", "insufficient",
                   LOG_SOURCE_MISMATCH)
    elif continuity_ok is False:
        _add_check(checks, "no_notify_supported", "premise", "insufficient",
                   RECEIPT_CONTINUITY)
    else:
        _add_check(checks, "no_notify_supported", "premise", "pass")


def _add_continuity_check(checks, observation, baseline):
    if baseline is None:
        return None
    receipts = observation.receipts
    if (receipts["since_baseline"] is None or baseline.issuer_total is None
            or receipts["issuer_total"] is None
            or receipts["issuer_total"] - baseline.issuer_total != receipts["since_baseline"]):
        _add_check(checks, "receipt_continuity", "criterion", "insufficient",
                   RECEIPT_CONTINUITY)
        return False
    _add_check(checks, "receipt_continuity", "criterion", "pass")
    return True


def _add_receipt_info(checks, notes, flow, authorization=True):
    if flow is None:
        return
    matched = flow.matched_in_window
    if matched is not None and matched > 1:
        _add_note(notes, MULTIPLE_MATCHED_RECEIPTS)
    if authorization:
        _add_check(checks, "authorization", "info", "pass", None, flow.authorization)
    if matched is not None:
        _add_check(checks, "matched_receipts", "info", "pass", None, matched)


def _login(observation, targets):
    checks = []
    notes = []
    _add_not_truncated(checks, observation)
    flow = _select_and_check(
        checks, observation.flows["A"], targets.get("A"), True, "target_unique"
    )
    if flow is not None:
        status = flow.effective_status
        if status in ("IDENTITY_VERIFIED", "SESSION_ENDED"):
            if flow.identity_present:
                _add_check(checks, "identity_present", "premise", "pass")
            else:
                _add_check(checks, "identity_present", "premise", "insufficient",
                           IDENTITY_MISSING)
        if status == "IDENTITY_VERIFIED":
            _add_check(checks, "identity_verified", "criterion", "pass")
        elif status in ("IDENTITY_FAILED", "IDENTITY_REFUSED", "FLOW_EXPIRED"):
            _add_check(checks, "identity_verified", "criterion", "unmet", status)
        elif status in ("AWAITING_IDENTITY", "EXCHANGING"):
            _add_check(checks, "identity_verified", "criterion", "insufficient",
                       FLOW_PENDING)
        elif status == "SESSION_ENDED":
            _add_check(checks, "identity_verified", "criterion", "insufficient",
                       SESSION_ENDED)
        else:
            _add_check(checks, "identity_verified", "criterion", "insufficient",
                       FLOW_STATE_UNKNOWN)
        if status in ("IDENTITY_VERIFIED", "SESSION_ENDED") or flow.identity_present:
            authorization = flow.authorization
            if authorization == "ACTIVE":
                _add_check(checks, "authorization", "criterion", "pass")
            elif authorization == "REFRESHING":
                _add_check(checks, "authorization", "criterion", "insufficient",
                           AUTHORIZATION_REFRESHING)
            elif authorization in ("REFRESH_UNCERTAIN", "REAUTH_REQUIRED", "NONE"):
                _add_check(checks, "authorization", "criterion", "unmet",
                           AUTHORIZATION_NOT_ACTIVE, authorization)
            else:
                _add_check(checks, "authorization", "criterion", "insufficient",
                           AUTHORIZATION_UNKNOWN)
        else:
            _add_check(checks, "authorization", "info", "pass", None,
                       flow.authorization)
    selected = {"A": flow.label} if flow is not None else {}
    return _finish("login", observation, checks, notes, selected, True)


def _logout(observation, targets, baseline_raw):
    checks = []
    notes = []
    _add_not_truncated(checks, observation)
    _add_clock_skew_check(checks, observation)
    flow = _select_and_check(
        checks, observation.flows["A"], targets.get("A"), False, "target_unique"
    )
    window_ok, window_open = _window_premises(checks, notes, observation)
    baseline_ready = True
    baseline = None
    if flow is None:
        if baseline_raw is not None or observation.baseline_valid is False:
            baseline_ready = False
    else:
        baseline_ready, baseline = _baseline_gate(
            checks, observation, baseline_raw, "logout", False
        )
        if baseline is not None:
            baseline_ready = _logout_baseline_checks(
                checks, baseline, flow, observation
            ) and baseline_ready
        elif baseline_raw is not None:
            baseline_ready = False
        continuity_ok = _add_continuity_check(checks, observation, baseline)
        before = flow.matched_before_window
        if before is None:
            _add_check(checks, "no_prior_receipt", "premise", "insufficient",
                       MATCH_EVIDENCE_MISSING)
        elif before > 0:
            _add_check(checks, "no_prior_receipt", "premise", "insufficient",
                       PRIOR_RECEIPT_MATCH, before)
        else:
            _add_check(checks, "no_prior_receipt", "premise", "pass")
        matched = flow.matched_in_window
        if matched is None:
            _add_check(checks, "receipt_matched", "criterion", "insufficient",
                       MATCH_EVIDENCE_MISSING)
        elif matched >= 1:
            _add_check(checks, "receipt_matched", "criterion", "pass", None, matched)
        else:
            _add_check(checks, "receipt_matched", "criterion", "unmet",
                       RECEIPT_NOT_MATCHED, matched)
        status = flow.effective_status
        if status == "SESSION_ENDED":
            _add_check(checks, "session_ended", "criterion", "pass", None, status)
        elif status == "IDENTITY_VERIFIED":
            _add_check(checks, "session_ended", "criterion", "unmet",
                       SESSION_NOT_ENDED, status)
        else:
            _add_check(checks, "session_ended", "criterion", "insufficient",
                       SESSION_STATE_UNKNOWN, status)
        _add_log_check(checks, observation, matched)
        _add_absence_check(checks, observation, matched, continuity_ok)
        _add_receipt_info(checks, notes, flow)
    selected = {"A": flow.label} if flow is not None else {}
    return _finish(
        "logout", observation, checks, notes, selected, baseline_ready,
        window_open=window_open
    )


def _two_account(observation, targets, baseline_raw):
    checks = []
    notes = []
    _add_not_truncated(checks, observation)
    _add_clock_skew_check(checks, observation)
    baseline_ready, baseline = _baseline_gate(
        checks, observation, baseline_raw, "two-account", True
    )
    flow_a = _select_and_check(
        checks, observation.flows["A"], targets.get("A"), False, "target_unique_a"
    )
    flow_b = _select_and_check(
        checks, observation.flows["B"], targets.get("B"), False, "target_unique_b"
    )
    window_ok, window_open = _window_premises(checks, notes, observation)
    distinct = None
    if flow_a is None or flow_b is None:
        baseline_ready = False
    else:
        if baseline is not None:
            baseline_ready = _two_account_baseline_checks(
                checks, baseline, flow_a, flow_b, observation
            ) and baseline_ready
        distinct = _add_distinct_check(checks, observation, flow_a, flow_b)
        if distinct is not True:
            baseline_ready = False
        continuity_ok = _add_continuity_check(checks, observation, baseline)
        if continuity_ok is False:
            _add_check(checks, "receipt_continuity_gate", "premise",
                       "insufficient", RECEIPT_CONTINUITY)
        _add_check(checks, "authorization_a_info", "info", "pass", None,
                   flow_a.authorization)
        _add_check(checks, "authorization_b_info", "info", "pass", None,
                   flow_b.authorization)
        if flow_a.effective_status == "IDENTITY_VERIFIED":
            authorization_a = flow_a.authorization
            if authorization_a == "ACTIVE":
                _add_check(checks, "authorization_stable_a", "premise", "pass")
            elif authorization_a == "REFRESHING":
                _add_check(checks, "authorization_stable_a", "premise",
                           "insufficient", AUTHORIZATION_REFRESHING, authorization_a)
            elif authorization_a in ("REFRESH_UNCERTAIN", "REAUTH_REQUIRED", "NONE"):
                _add_check(checks, "authorization_stable_a", "premise",
                           "insufficient", A_AUTHORIZATION_CHANGED, authorization_a)
            else:
                _add_check(checks, "authorization_stable_a", "premise",
                           "insufficient", AUTHORIZATION_UNKNOWN, authorization_a)
        before_b = flow_b.matched_before_window
        if before_b is None:
            _add_check(checks, "no_prior_receipt_b", "premise", "insufficient",
                       MATCH_EVIDENCE_MISSING)
        elif before_b > 0:
            _add_check(checks, "no_prior_receipt_b", "premise", "insufficient",
                       PRIOR_RECEIPT_MATCH, before_b)
        else:
            _add_check(checks, "no_prior_receipt_b", "premise", "pass")
        matched_a = flow_a.matched_in_window
        if matched_a is None:
            _add_check(checks, "receipt_absent_a", "criterion", "insufficient",
                       MATCH_EVIDENCE_MISSING)
        elif matched_a > 0:
            _add_check(checks, "receipt_absent_a", "criterion", "insufficient",
                       A_RECEIPT_MATCHED, matched_a)
        else:
            _add_check(checks, "receipt_absent_a", "criterion", "pass")
        matched_b = flow_b.matched_in_window
        if matched_b is None:
            _add_check(checks, "receipt_matched_b", "criterion", "insufficient",
                       MATCH_EVIDENCE_MISSING)
        elif matched_b >= 1:
            _add_check(checks, "receipt_matched_b", "criterion", "pass", None,
                       matched_b)
        else:
            _add_check(checks, "receipt_matched_b", "criterion", "unmet",
                       B_RECEIPT_NOT_MATCHED, matched_b)
        status_a = flow_a.effective_status
        if status_a == "IDENTITY_VERIFIED":
            _add_check(checks, "session_held_a", "criterion", "pass", None, status_a)
        elif status_a == "SESSION_ENDED":
            if matched_a is None:
                _add_check(checks, "session_held_a", "criterion", "insufficient",
                           MATCH_EVIDENCE_MISSING)
            elif matched_a > 0:
                _add_check(checks, "session_held_a", "criterion", "insufficient",
                           A_RECEIPT_MATCHED)
            else:
                _add_check(checks, "session_held_a", "criterion", "unmet",
                           A_SESSION_ENDED, status_a)
        else:
            _add_check(checks, "session_held_a", "criterion", "insufficient",
                       A_SESSION_STATE_UNKNOWN, status_a)
        status_b = flow_b.effective_status
        if status_b == "SESSION_ENDED":
            _add_check(checks, "session_ended_b", "criterion", "pass", None, status_b)
        elif status_b == "IDENTITY_VERIFIED":
            _add_check(checks, "session_ended_b", "criterion", "unmet",
                       B_SESSION_NOT_ENDED, status_b)
        else:
            _add_check(checks, "session_ended_b", "criterion", "insufficient",
                       SESSION_STATE_UNKNOWN, status_b)
        _add_log_check(checks, observation, matched_b)
        _add_absence_check(checks, observation, matched_b, continuity_ok)
        _add_receipt_info(checks, notes, flow_b, authorization=False)
        if matched_a is not None and matched_a > 1:
            _add_note(notes, MULTIPLE_MATCHED_RECEIPTS)
    selected = {}
    if flow_a is not None:
        selected["A"] = flow_a.label
    if flow_b is not None:
        selected["B"] = flow_b.label
    return _finish(
        "two-account", observation, checks, notes, selected, baseline_ready,
        identities_distinct=distinct, window_open=window_open
    )


def _read_browser(raw):
    if not isinstance(raw, dict):
        return None
    kind = raw.get("kind")
    if kind not in ("captured", "user_reported"):
        kind = "unknown"
    http_status = _integer(raw.get("http_status"), 100, 599)
    error = raw.get("error") if raw.get("error") == "IDENTITY_REQUIRED" else None
    return {
        "kind": kind,
        "observed_at_ms": _ms(raw.get("observed_at_ms")),
        "http_status": http_status,
        "error": error,
        "original_browser": raw.get("original_browser") is True,
        "binding_matches": raw.get("binding_matches") is True,
    }


def _idle(observation, targets, baseline_raw, browser_raw):
    checks = []
    notes = []
    _add_not_truncated(checks, observation)
    flow = _select_and_check(
        checks, observation.flows["A"], targets.get("A"), False, "target_unique"
    )
    _add_clock_skew_check(checks, observation)
    baseline_ready = True
    baseline = None
    if flow is None:
        if baseline_raw is not None or observation.baseline_valid is False:
            baseline_ready = False
    else:
        baseline_ready, baseline = _baseline_gate(
            checks, observation, baseline_raw, "idle", False
        )
        if baseline is not None:
            baseline_ready = _idle_baseline_checks(
                checks, baseline, flow, observation
            ) and baseline_ready
        elif baseline_raw is not None:
            baseline_ready = False
    deadline = None
    if flow is not None:
        if flow.last_activity_ms is None:
            _add_check(checks, "idle_deadline", "criterion", "insufficient",
                       ACTIVITY_UNKNOWN)
        else:
            deadline = flow.last_activity_ms + IDLE_MS
            if deadline <= observation.observed_at_ms:
                _add_check(checks, "idle_deadline", "criterion", "pass", None, deadline)
            else:
                _add_check(checks, "idle_deadline", "criterion", "insufficient",
                           IDLE_NOT_REACHED, deadline)
        _add_check(checks, "last_activity", "info", "pass", None, flow.last_activity_ms)
        _add_check(checks, "idle_deadline_report", "info", "pass", None, deadline)
        _add_check(checks, "authorization", "info", "pass", None, flow.authorization)
        _add_check(checks, "flow_status", "info", "pass", None, flow.effective_status)
        _add_check(checks, "browser_evidence", "criterion",
                   *_browser_check(observation, browser_raw, deadline))
    else:
        _add_check(checks, "browser_evidence", "criterion", "insufficient",
                   BROWSER_EVIDENCE_MISSING)
    browser = _read_browser(browser_raw) if browser_raw is not None else None
    selected = {"A": flow.label} if flow is not None else {}
    return _finish(
        "idle", observation, checks, notes, selected, baseline_ready, browser=browser
    )


def _browser_check(observation, browser_raw, deadline):
    if browser_raw is None:
        return "insufficient", BROWSER_EVIDENCE_MISSING, None
    browser = _read_browser(browser_raw)
    if browser is None:
        return "insufficient", BROWSER_EVIDENCE_INVALID, None
    if browser["kind"] == "user_reported":
        return "insufficient", BROWSER_USER_REPORTED, None
    if browser["kind"] != "captured":
        return "insufficient", BROWSER_EVIDENCE_INVALID, None
    if not browser["original_browser"]:
        return "insufficient", BROWSER_EVIDENCE_INVALID, None
    if not browser["binding_matches"]:
        return "insufficient", BROWSER_BINDING_MISMATCH, None
    observed = browser["observed_at_ms"]
    if observed is None:
        return "insufficient", BROWSER_EVIDENCE_INVALID, None
    if deadline is None:
        return "insufficient", ACTIVITY_UNKNOWN, None
    if observed < deadline:
        return "insufficient", BROWSER_EVIDENCE_BEFORE_DEADLINE, observed
    if observation.observed_at_ms is not None and observed > observation.observed_at_ms + 1000:
        return "insufficient", BROWSER_EVIDENCE_IN_FUTURE, observed
    if browser["http_status"] == 401 and browser["error"] == "IDENTITY_REQUIRED":
        return "pass", None, browser["http_status"]
    if browser["http_status"] == 200:
        return "unmet", BROWSER_ACCESSIBLE, browser["http_status"]
    return "insufficient", BROWSER_STATUS_UNEXPECTED, browser["http_status"]


def _baseline_phase(observation, targets, scenario):
    checks = []
    notes = []
    _add_not_truncated(checks, observation)
    ready = not observation.truncated
    selected = {}
    distinct = None
    if scenario == "two-account":
        flow_a = _select_and_check(
            checks, observation.flows["A"], targets.get("A"), False, "target_unique_a"
        )
        flow_b = _select_and_check(
            checks, observation.flows["B"], targets.get("B"), False, "target_unique_b"
        )
        if flow_a is not None:
            selected["A"] = flow_a.label
        if flow_b is not None:
            selected["B"] = flow_b.label
        if flow_a is None or flow_b is None:
            ready = False
        else:
            distinct = _add_distinct_check(checks, observation, flow_a, flow_b)
            if distinct is not True:
                ready = False
            for record, flow in (("A", flow_a), ("B", flow_b)):
                suffix = record.lower()
                if flow.effective_status != "IDENTITY_VERIFIED":
                    _add_check(checks, "baseline_status_" + suffix, "premise",
                               "insufficient", BASELINE_STATUS, flow.effective_status)
                    ready = False
                else:
                    _add_check(checks, "baseline_status_" + suffix, "premise", "pass")
                if flow.authorization != "ACTIVE":
                    _add_check(checks, "baseline_authorization_" + suffix, "premise",
                               "insufficient", BASELINE_AUTHORIZATION,
                               flow.authorization)
                    ready = False
                else:
                    _add_check(checks, "baseline_authorization_" + suffix, "premise",
                               "pass")
    elif scenario == "logout":
        flow = _select_and_check(
            checks, observation.flows["A"], targets.get("A"), False, "target_unique"
        )
        if flow is None:
            ready = False
        else:
            selected["A"] = flow.label
            if flow.effective_status != "IDENTITY_VERIFIED":
                _add_check(checks, "baseline_status", "premise", "insufficient",
                           BASELINE_STATUS, flow.effective_status)
                ready = False
            else:
                _add_check(checks, "baseline_status", "premise", "pass")
    elif scenario == "idle":
        flow = _select_and_check(
            checks, observation.flows["A"], targets.get("A"), False, "target_unique"
        )
        if flow is None:
            ready = False
        else:
            selected["A"] = flow.label
            if flow.last_activity_ms is None:
                _add_check(checks, "activity", "premise", "insufficient",
                           ACTIVITY_UNKNOWN)
                ready = False
            else:
                _add_check(checks, "activity", "premise", "pass")
    return _build_result(
        "insufficient", scenario, "baseline", observation, checks, selected,
        notes, [BASELINE_ONLY], ready, distinct, None
    )


def _evaluate(scenario, observation, targets, baseline, browser_evidence, phase):
    if not isinstance(scenario, str) or scenario not in SCENARIOS:
        return _error_result(scenario, phase, _Observation(), SCENARIO_INVALID)
    if not isinstance(phase, str) or phase not in PHASES:
        return _error_result(scenario, phase, _Observation(), PHASE_INVALID)
    if not isinstance(observation, dict):
        return _error_result(scenario, phase, _Observation(), OBSERVATION_INVALID)
    normalised = _read_observation(observation)
    if normalised.invalid_reason is not None:
        return _error_result(scenario, phase, normalised, OBSERVATION_INVALID)
    if normalised.environment_reasons:
        return _error_result(
            scenario, phase, normalised, normalised.environment_reasons[0],
            normalised.environment_reasons[1:]
        )
    target_map, target_error = _read_targets(targets)
    if target_error is not None:
        return _error_result(scenario, phase, normalised, target_error)
    if phase == "baseline":
        return _baseline_phase(normalised, target_map, scenario)
    if scenario == "login":
        return _login(normalised, target_map)
    if scenario == "logout":
        return _logout(normalised, target_map, baseline)
    if scenario == "two-account":
        return _two_account(normalised, target_map, baseline)
    return _idle(normalised, target_map, baseline, browser_evidence)


def evaluate(scenario, observation, targets=None, baseline=None,
             browser_evidence=None, phase="check"):
    """Evaluate one normalised observation without performing IO.

    Returns a result dictionary with ``result`` in pass/unmet/insufficient/error
    and the matching ``exit_code`` 0/1/3/4.  A baseline capture returns
    ``insufficient`` with reason ``BASELINE_ONLY`` and ``baseline_ready`` tells
    the caller whether the capture is usable as a baseline; it is never a
    business pass.
    """
    try:
        return _evaluate(scenario, observation, targets, baseline,
                         browser_evidence, phase)
    except Exception:
        # Never expose exception text; external data must not leak through
        # error handling.
        return _error_result(scenario, phase, _Observation(), OBSERVATION_INVALID)
