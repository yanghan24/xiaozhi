#!/usr/bin/env python3
"""Audit actual model-publication transitions against durable city snapshots."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path


def iso(value):
    return datetime.fromtimestamp(value, timezone.utc).isoformat()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--observation", type=Path, required=True)
    parser.add_argument("--snapshots", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new evidence filename")
    rows = [json.loads(line) for line in (args.observation / "source-publications.jsonl").read_text().splitlines()]
    baseline, sources = {}, {}
    for row in rows:
        for model, data in row.get("models", {}).items():
            metadata = data.get("metadata")
            if not metadata:
                continue
            run = metadata["last_run_initialisation_time"]
            baseline.setdefault(model, run)
            sources.setdefault((model, run, data.get("sha256")), {"first_observed_at": row["at"], "metadata": metadata,
                                              "etag": data.get("etag"), "sha256": data.get("sha256")})
    snapshots = {"weather": [], "air_quality": []}
    for path in args.snapshots.glob("1815577-*.json"):
        data = json.loads(path.read_text())
        data["snapshot_file"] = path.name
        snapshots[data["kind"]].append(data)
    report = {"source_baseline": baseline, "observation_samples": len(rows), "components": {}}
    passed = True
    for kind, model in (("weather", "ncep_gfs013"), ("air_quality", "cams_global")):
        ordered = sorted(snapshots[kind], key=lambda x: x["entry"]["metadata"]["run_at"])
        transitions = []
        observed_states, invalid_states = {}, []
        for row in rows:
            data = row.get("/v1/environment?city_id=changsha&peek=1", {}).get("data", {})
            component = data.get("components", {}).get(kind, {})
            status = component.get("status")
            if not status:
                continue
            observed_states[status] = observed_states.get(status, 0) + 1
            if (component.get("source_has_newer_run") or component.get("source_has_newer_version")) and status != "stale":
                invalid_states.append({"at": row["at"], "error": "superseded_result_reported_fresh"})
            if component.get("values") is not None:
                served = datetime.fromisoformat(data["served_at"]).timestamp()
                valid = datetime.fromisoformat(component["valid_at"]).timestamp()
                if valid != int(served) // 3600 * 3600:
                    invalid_states.append({"at": row["at"], "error": "invalid_current_hour"})
        for old, new in zip(ordered, ordered[1:]):
            a, b = old["entry"], new["entry"]
            before_run, after_run = a["metadata"]["run_at"], b["metadata"]["run_at"]
            common = sorted(set(a["hourly"]["time"]) & set(b["hourly"]["time"]))
            checks = {}
            for field in b["units"]:
                ai, bi = dict(zip(a["hourly"]["time"], a["hourly"][field])), dict(zip(b["hourly"]["time"], b["hourly"][field]))
                differences = [{"valid_at": iso(t), "old": ai[t], "new": bi[t]} for t in common if ai[t] != bi[t]]
                checks[field] = {"overlap_samples": len(common), "changed_values": len(differences),
                                 "first_changed_examples": differences[:3]}
            source = sources.get((model, after_run, b["metadata"]["sha256"]))
            raw = b.get("raw_engine_body")
            raw_matches = bool(raw and hashlib.sha256(raw.encode()).hexdigest() == b["payload_sha256"])
            promoted_after_publication = b["fetched_at"] >= b["metadata"]["published_at"] + 600
            same_query = a["query"] == b["query"]
            overlapping_values_changed = any(v["changed_values"] for k, v in checks.items() if k != "time")
            consistent = (after_run > before_run and bool(source) and raw_matches and promoted_after_publication and
                          same_query and a["units"] == b["units"] and bool(common) and overlapping_values_changed)
            if not consistent:
                passed = False
            transitions.append({"from_run": iso(before_run), "to_run": iso(after_run), "source": source,
                                "cached_fetched_at": iso(b["fetched_at"]), "snapshot": new["snapshot_file"],
                                "raw_engine_response_matches_sha256": raw_matches,
                                "same_coordinates_model_variables_and_query": same_query,
                                "publication_settling_grace_satisfied": promoted_after_publication,
                                "overlapping_values_changed": overlapping_values_changed,
                                "fields": checks, "consistent": consistent})
        new_runs = [s for s in ordered if s["entry"]["metadata"]["run_at"] > baseline.get(model, float("inf"))]
        minimum = 2 if kind == "air_quality" else 1
        passed = passed and len(new_runs) >= minimum and not invalid_states
        report["components"][kind] = {"snapshots": len(ordered), "new_runs_after_source_baseline": len(new_runs),
                                       "required_new_runs": minimum, "transitions": transitions,
                                       "observed_states": observed_states, "invalid_states": invalid_states}
    report["passed"] = passed
    report["limitations"] = ["Engine does not emit a model run identifier; source metadata, 10-minute settling and overlapping values are audited together",
                              "Differences compare identical valid hours so a moving forecast window alone cannot count as a model update",
                              "These are model estimates, not monitoring-station observations"]
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"passed": passed, "new_cached_runs": {k: v["new_runs_after_source_baseline"] for k, v in report["components"].items()}}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
