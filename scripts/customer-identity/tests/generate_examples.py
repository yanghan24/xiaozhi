"""Generate public examples from synthetic observations, never live records."""
from copy import deepcopy
import json
from pathlib import Path

from identity_check.evaluate import evaluate
from identity_check.output import build_report, serialize


def observation(scenario):
    now = 1794448800123
    row = {"label": "F1", "creation_order": 1, "stored_status": "IDENTITY_VERIFIED",
           "created_at_ms": now - 2400000, "expires_at_ms": now - 2100000,
           "identity_issued_at_ms": now - 2399000, "sid_present": True,
           "identity_present": True, "authorization": "ACTIVE", "last_activity_ms": now - 2400000,
           "matched_before_window": 0, "matched_in_window": 0, "matched_received_at_ms": []}
    data = {"observed_at_ms": now, "clock_skew_ms": 0, "truncated": False,
            "window": None, "baseline_valid": True,
            "environment": {key: "ok" for key in ("containers", "db_target", "proxy_target", "schema")},
            "flows": {"A": [row]}, "identities_distinct": None,
            "receipts": {"issuer_total": 5, "issuer_before_window": 4, "issuer_in_window": 1,
                         "other_issuer_total": 32, "since_baseline": None},
            "log": {"status": "complete", "post_204_in_window": 1, "post_400_in_window": 0,
                    "boundary_lines": 0, "reasons": []}}
    if scenario in ("logout", "two-account"):
        data["window"] = {"start_ms": now - 300000, "end_ms": now - 60000}
        target = row if scenario == "logout" else deepcopy(row)
        target["stored_status"] = "SESSION_ENDED"
        target["matched_in_window"] = 1
        target["matched_received_at_ms"] = [now - 240000]
        if scenario == "two-account":
            target["creation_order"] = 2
            data["flows"]["B"] = [target]
            data["identities_distinct"] = True
    return data


def main():
    root = Path(__file__).resolve().parents[1] / "examples"
    root.mkdir(exist_ok=True)
    for scenario in ("login", "logout", "two-account", "idle"):
        data = observation(scenario)
        result = evaluate(scenario, data)
        report = build_report(scenario, "check", data, result)
        name = root / (scenario + ".json")
        name.write_text(serialize(report) + "\n", encoding="utf-8")
        print(scenario, result["result"], result["exit_code"])


if __name__ == "__main__":
    main()
