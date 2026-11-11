"""Synthetic safe fixtures and a test-only receipt matching oracle.

The oracle here mirrors the documented SQL condition so scenario tests can
build matched counts from synthetic receipt rows.  It is deliberately not
imported by production code and says nothing about whether the real SQL query
is correct.
"""
BASE_MS = 1700000000000
WINDOW = {"start_ms": BASE_MS + 100000, "end_ms": BASE_MS + 200000}


def flow(label="F1", creation_order=1, status="IDENTITY_VERIFIED",
         created_at_ms=BASE_MS, expires_at_ms=BASE_MS + 300000,
         identity_issued_at_ms=None, sid_present=True, identity_present=True,
         authorization="ACTIVE", last_activity_ms=BASE_MS,
         matched_before_window=0, matched_in_window=0,
         matched_received_at_ms=None):
    if matched_received_at_ms is None:
        matched_received_at_ms = []
    return {
        "label": label,
        "creation_order": creation_order,
        "stored_status": status,
        "created_at_ms": created_at_ms,
        "expires_at_ms": expires_at_ms,
        "identity_issued_at_ms": identity_issued_at_ms,
        "sid_present": sid_present,
        "identity_present": identity_present,
        "authorization": authorization,
        "last_activity_ms": last_activity_ms,
        "matched_before_window": matched_before_window,
        "matched_in_window": matched_in_window,
        "matched_received_at_ms": list(matched_received_at_ms),
    }


def state_flow(label="F1", creation_order=1, status="IDENTITY_VERIFIED",
               authorization="ACTIVE", last_activity_ms=BASE_MS,
               identity_present=True):
    return {
        "label": label,
        "creation_order": creation_order,
        "status": status,
        "identity_present": identity_present,
        "authorization": authorization,
        "last_activity_ms": last_activity_ms,
    }


def baseline(scenario, flows, targets, observed_at_ms=BASE_MS,
             identities_distinct=None, issuer_total=0):
    return {
        "schema": "xiaozhi-identity-state-v1",
        "scenario": scenario,
        "observed_at_ms": observed_at_ms,
        "flows": flows,
        "targets": targets,
        "identities_distinct": identities_distinct,
        "issuer_total": issuer_total,
    }


def observation(flows=None, observed_at_ms=BASE_MS + 1000000,
                clock_skew_ms=0, window=None, receipts=None, log=None,
                environment=None, truncated=False, baseline_valid=True,
                baseline_reasons=None, identities_distinct=None,
                identity_pairs=None, browser_evidence=None):
    if flows is None:
        flows = {"A": []}
    default_receipts = {
        "issuer_total": 0,
        "issuer_before_window": 0,
        "issuer_in_window": 0,
        "other_issuer_total": 0,
        "since_baseline": None,
    }
    if receipts:
        default_receipts.update(receipts)
    default_log = {
        "status": "complete",
        "post_204_in_window": 0,
        "post_400_in_window": 0,
        "boundary_lines": 0,
        "reasons": [],
    }
    if log:
        default_log.update(log)
    default_environment = {
        "containers": "ok",
        "db_target": "ok",
        "proxy_target": "ok",
        "schema": "ok",
    }
    if environment:
        default_environment.update(environment)
    value = {
        "observed_at_ms": observed_at_ms,
        "clock_skew_ms": clock_skew_ms,
        "environment": default_environment,
        "flows": flows,
        "receipts": default_receipts,
        "log": default_log,
        "truncated": truncated,
        "baseline_valid": baseline_valid,
    }
    if window is not None:
        value["window"] = window
    if baseline_reasons is not None:
        value["baseline_reasons"] = baseline_reasons
    if identities_distinct is not None:
        value["identities_distinct"] = identities_distinct
    if identity_pairs is not None:
        value["identity_pairs"] = identity_pairs
    if browser_evidence is not None:
        value["browser_evidence"] = browser_evidence
    return value


def receipt(issuer="issuer-one", sid="sid-one", subject="subject-one",
            issued_at_ms=BASE_MS, received_at_ms=BASE_MS):
    return {
        "issuer": issuer,
        "sid": sid,
        "subject": subject,
        "issued_at_ms": issued_at_ms,
        "received_at_ms": received_at_ms,
    }


def _sid_or_subject_matches(raw_flow, item):
    if item.get("sid") is not None:
        return raw_flow.get("sid") is not None and item["sid"] == raw_flow["sid"]
    return raw_flow.get("subject") is not None and item.get("subject") == raw_flow["subject"]


def backchannel_matches(raw_flow, item):
    """Test-only model of the back-channel receipt SQL condition."""
    if item.get("issuer") != raw_flow.get("issuer"):
        return False
    if not _sid_or_subject_matches(raw_flow, item):
        return False
    return raw_flow["created_at_ms"] <= min(
        item["issued_at_ms"] + 999, item["received_at_ms"]
    )


def callback_matches(raw_flow, item):
    """Test-only model of the callback receipt lookup, including sid priority."""
    if item.get("issuer") != raw_flow.get("issuer"):
        return False
    if not _sid_or_subject_matches(raw_flow, item):
        return False
    return (item["issued_at_ms"] >= raw_flow["created_at_ms"] // 1000 * 1000
            and item["received_at_ms"] >= raw_flow["created_at_ms"])


def matching_receipts(raw_flow, items):
    return [item for item in items if backchannel_matches(raw_flow, item)]


def receipt_counts(raw_flow, items, window):
    """Return (before, in_window, received_at_ms) for synthetic matched items."""
    before = 0
    in_window = 0
    received = []
    for item in matching_receipts(raw_flow, items):
        if item["received_at_ms"] < window["start_ms"]:
            before += 1
        elif item["received_at_ms"] < window["end_ms"]:
            in_window += 1
            received.append(item["received_at_ms"])
    return before, in_window, received
