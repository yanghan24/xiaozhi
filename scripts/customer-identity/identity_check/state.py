"""Private baseline and browser-evidence bindings, never public identity hashes."""
import re

from .errors import CheckError
from .records import private_json, record_fingerprints
from .timeutil import millis, parse_timestamp


STATE_SCHEMA = "xiaozhi-identity-state-v1"
_FLOW_KEYS = ("label", "creation_order", "stored_status", "created_at_ms", "expires_at_ms",
              "sid_present", "identity_present", "authorization", "last_activity_ms")


def make_state(scenario, observation, records, targets):
    fingerprint = observation.get("environment_fingerprint")
    if not isinstance(fingerprint, str) or not re.fullmatch(r"[a-f0-9]{64}", fingerprint):
        raise CheckError("STATE_ENVIRONMENT_UNBOUND")
    return {"schema": STATE_SCHEMA, "scenario": scenario,
            "observed_at_ms": observation["observed_at_ms"],
            "environment_fingerprint": fingerprint,
            "records": record_fingerprints(records), "targets": targets,
            "identities_distinct": observation.get("identities_distinct"),
            "flows": {label: [{key: row.get(key) for key in _FLOW_KEYS} for row in rows]
                      for label, rows in observation["flows"].items()},
            "issuer_total": observation.get("receipts", {}).get("issuer_total", 0),
            "log_state": observation.get("log", {}).get("state", {})}


def load_baseline(path):
    data, _ = private_json(path)
    if data.get("schema") != STATE_SCHEMA:
        raise CheckError("STATE_FORMAT_UNSUPPORTED")
    try:
        millis(data["observed_at_ms"])
        if data.get("scenario") not in ("login", "logout", "two-account", "idle"):
            raise ValueError()
        expected = {"schema", "scenario", "observed_at_ms", "environment_fingerprint", "records",
                    "targets", "identities_distinct", "flows", "issuer_total", "log_state"}
        if set(data) != expected:
            raise ValueError()
        if not isinstance(data["records"], dict) or not isinstance(data["flows"], dict) or not isinstance(data["targets"], dict):
            raise ValueError()
        if set(data["records"]) - {"A", "B"} or set(data["targets"]) - {"A", "B"}:
            raise ValueError()
        if data["identities_distinct"] is not None and type(data["identities_distinct"]) is not bool:
            raise ValueError()
        for target in data["targets"].values():
            if not isinstance(target, str) or not re.fullmatch(r"F(?:[1-9]|[1-4][0-9]|50)", target):
                raise ValueError()
        log = data["log_state"]
        if not isinstance(log, dict) or set(log) - {"inode", "size", "last_timestamp_ms", "configured"}:
            raise ValueError()
        for key in ("inode", "size", "last_timestamp_ms"):
            if key in log and log[key] is not None and (type(log[key]) is not int or log[key] < 0):
                raise ValueError()
        if "configured" in log and type(log["configured"]) is not bool:
            raise ValueError()
        for fingerprint in list(data["records"].values()) + [data["environment_fingerprint"]]:
            if not isinstance(fingerprint, str) or not re.fullmatch(r"[a-f0-9]{64}", fingerprint):
                raise ValueError()
        if not isinstance(data["targets"], dict) or type(data["issuer_total"]) is not int or data["issuer_total"] < 0:
            raise ValueError()
        for label, rows in data["flows"].items():
            if label not in ("A", "B") or not isinstance(rows, list) or len(rows) > 50:
                raise ValueError()
            for row in rows:
                if not isinstance(row, dict) or set(row) != set(_FLOW_KEYS):
                    raise ValueError()
                if not re.fullmatch(r"F[1-9][0-9]?", row["label"]):
                    raise ValueError()
                if type(row["creation_order"]) is not int or row["creation_order"] < 1:
                    raise ValueError()
                if row["stored_status"] not in ("AWAITING_IDENTITY", "EXCHANGING", "IDENTITY_VERIFIED",
                        "SESSION_ENDED", "IDENTITY_FAILED", "IDENTITY_REFUSED"):
                    raise ValueError()
                if row["authorization"] not in ("ACTIVE", "REFRESHING", "REFRESH_UNCERTAIN", "REAUTH_REQUIRED", "NONE"):
                    raise ValueError()
                if type(row["sid_present"]) is not bool or type(row["identity_present"]) is not bool:
                    raise ValueError()
                millis(row["created_at_ms"])
                millis(row["expires_at_ms"])
                if row["last_activity_ms"] is not None:
                    millis(row["last_activity_ms"])
    except (KeyError, TypeError, ValueError):
        raise CheckError("STATE_INVALID") from None
    return data


def bind_baseline(data, scenario, records, observation, targets):
    reasons = []
    if data["scenario"] != scenario:
        reasons.append("BASELINE_SCENARIO_CHANGED")
    if data["records"] != record_fingerprints(records):
        reasons.append("BASELINE_RECORD_CHANGED")
    if data["environment_fingerprint"] != observation.get("environment_fingerprint"):
        reasons.append("BASELINE_ENVIRONMENT_CHANGED")
    boundary = (observation.get("window") or {}).get("start_ms", observation["observed_at_ms"])
    if data["observed_at_ms"] >= boundary:
        reasons.append("BASELINE_TIME_INVALID")
    for label, target in targets.items():
        if data["targets"].get(label) != target:
            reasons.append("BASELINE_TARGET_CHANGED")
    observation["baseline_valid"] = not reasons
    observation["baseline_reasons"] = sorted(set(reasons))
    return observation["baseline_valid"]


def browser_evidence(path, record, selected, flow):
    data, _ = private_json(path)
    if data.get("schema") != "xiaozhi-browser-evidence-v1":
        raise CheckError("BROWSER_EVIDENCE_FORMAT_UNSUPPORTED")
    try:
        when = parse_timestamp(data["observed_at"])
        status = data["http_status"]
        kind = data["kind"]
        if kind not in ("captured", "user_reported") or type(status) is not int or not 100 <= status <= 599:
            raise ValueError()
        error = data.get("error")
        if error is not None and error != "IDENTITY_REQUIRED":
            raise ValueError()
        original = data.get("original_browser") is True
        activity = data.get("last_activity_ms")
        bound = (type(activity) is int and activity >= 0
                 and type(flow.get("last_activity_ms")) is int
                 and data.get("record_fingerprint") == record.fingerprint
                 and data.get("flow") == selected
                 and activity == flow["last_activity_ms"])
        return {"kind": kind, "observed_at_ms": when, "http_status": status, "error": error,
                "original_browser": original, "binding_matches": bound}
    except (KeyError, TypeError, ValueError):
        raise CheckError("BROWSER_EVIDENCE_INVALID") from None
