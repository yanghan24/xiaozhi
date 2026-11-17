#!/usr/bin/env python3
"""Independently recount the four same-account real voice phases and overlap."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", required=True, type=Path)
    p.add_argument("--label", default="acceptance50-r1")
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    assert not args.output.exists()
    root = args.root.resolve()
    matrix = root / "evidence" / args.label
    manifest = json.loads((matrix / "manifest.json").read_text())
    phase_names = ("before-weather-load", "during-warm-50", "after-weather-load-50", "during-cold-50")
    phases = {}
    integrity = True
    timing_fields = ("stt_after_input", "llm_after_input", "first_audio_after_input", "complete_after_input")
    cold = json.loads((matrix / "cold-warmup.json").read_text())
    warm_at = datetime.fromisoformat(cold["warm_at"]) if cold["warm_at"] else None
    cold_start = datetime.fromisoformat(json.loads((matrix / "cold-50-first-ack/summary.json").read_text())["started_at"])
    overlap = {"attempt_started_before_fill_observed_complete": 0,
               "input_stopped_during_fill": 0, "first_audio_during_fill": 0,
               "complete_question_during_fill": 0}
    for phase in phase_names:
        folder = root / "evidence/voice/account2" / phase
        context = json.loads((folder / "context.json").read_text())
        summary = json.loads((folder / "summary.json").read_text())
        path = folder / "requests.jsonl"
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        success = sum(bool(row["success"]) for row in rows)
        timings = {}
        for field in timing_fields:
            values = [row["timing_s"][field] for row in rows if row["success"] and field in row["timing_s"]]
            timings[field] = {"samples": len(values), "p50": percentile(values, .5),
                              "p95": percentile(values, .95), "max": max(values) if values else None}
        matched = (len(rows) == summary["count"] and success == summary["success"] and
                   len(rows) - success == summary["failure"] and timings == summary["timing_s"])
        integrity = integrity and matched
        phases[phase] = {"count": len(rows), "success": success, "failure": len(rows) - success,
                         "complete_planned_sample": len(rows) == success == 10,
                         "raw_counts_and_latencies_match_summary": matched, "timing_s": timings,
                         "raw_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                         "context": context}
        if phase == "during-cold-50" and warm_at:
            for row in rows:
                at = datetime.fromisoformat(row["at"])
                timing = row["timing_s"]
                if cold_start <= at <= warm_at:
                    overlap["attempt_started_before_fill_observed_complete"] += 1
                sent = at + timedelta(seconds=timing.get("audio_sent", 0))
                if "audio_sent" in timing and cold_start <= sent <= warm_at:
                    overlap["input_stopped_during_fill"] += 1
                    if "first_audio_after_input" in timing and sent + timedelta(seconds=timing["first_audio_after_input"]) <= warm_at:
                        overlap["first_audio_during_fill"] += 1
                    if "complete_after_input" in timing and sent + timedelta(seconds=timing["complete_after_input"]) <= warm_at:
                        overlap["complete_question_during_fill"] += 1
    baseline = phases[phase_names[0]]
    after = phases[phase_names[2]]
    ratios = {}
    for phase in ("during-warm-50", "during-cold-50"):
        ratios[phase] = {}
        for field in timing_fields:
            measured = phases[phase]["timing_s"][field]["p95"]
            before = baseline["timing_s"][field]["p95"]
            recovered = after["timing_s"][field]["p95"]
            ratios[phase][field] = {"p95_ratio_to_before": measured / before if measured and before else None,
                                    "p95_ratio_to_after": measured / recovered if measured and recovered else None}
    scope_fields = ("device_id_sha256", "account_subject_sha256", "authorization_generation_sha256",
                    "product_voice_source_sha256", "model", "model_base")
    authority = json.loads((root / "evidence/voice/account2-authority-initial.json").read_text())
    scope_rows = [json.loads(path.read_text()) for path in matrix.glob("authority-*.json")]
    same_scope = len(scope_rows) == 7 and all(all(row[field] == authority[field] for field in scope_fields) for row in scope_rows)
    primary_ratio = ratios["during-warm-50"]["first_audio_after_input"]["p95_ratio_to_before"]
    limit = manifest["proposed_voice_p95_degradation_limit"]
    all_success = all(phase["complete_planned_sample"] for phase in phases.values())
    comparison_passed = bool(primary_ratio and primary_ratio <= 1 + limit)
    result = {"at": datetime.now(timezone.utc).isoformat(), "phases": phases, "ratios": ratios,
              "cold_overlap": {"start": cold_start.isoformat(), "fill_observed_complete": cold["warm_at"],
                               "counts": overlap, "observation_poll_interval_s": 5,
                               "cold_phase_ratios_include_post_fill_questions": True},
              "raw_integrity_passed": integrity, "all_real_questions_succeeded": all_success,
              "same_account_generation_and_product_source": same_scope,
              "primary_warm_p95_proposed_limit_passed": comparison_passed,
              "proposed_p95_degradation_limit": limit,
              "primary_warm_comparison_passed": integrity and all_success and same_scope and comparison_passed,
              "limitations": ["Ten serial questions per phase; p95 equals the worst sample, no statistical capacity/SLA claim",
                              "Cloud ASR/model/TTS latency varies independently of host contention",
                              "Cold fill can finish before all ten questions; actual overlap is reported separately",
                              "Real server protocol and decoded audio, not a physical microphone or speaker"]}
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("raw_integrity_passed", "all_real_questions_succeeded",
                     "same_account_generation_and_product_source", "primary_warm_p95_proposed_limit_passed")}))
    return 0 if integrity else 1


if __name__ == "__main__":
    raise SystemExit(main())
