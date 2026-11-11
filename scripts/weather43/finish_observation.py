#!/usr/bin/env python3
"""Finish this bounded experiment on Ubuntu even if workstation SSH disconnects.

No Git operations or messages. Does not deploy/restart business services. A
deadline produces failed/incomplete evidence rather than a fabricated pass.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import time


def utc():
    return datetime.now(timezone.utc).isoformat()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", required=True, type=Path)
    p.add_argument("--deadline", required=True, help="Absolute UTC experiment deadline")
    p.add_argument("--label", default="acceptance50-r1")
    args = p.parse_args()
    assert os.name == "posix" and os.geteuid() == 0
    root = args.root.resolve()
    deadline = datetime.fromisoformat(args.deadline).timestamp()
    assert time.time() < deadline <= time.time() + 12 * 3600
    out = root / "evidence/finalization50"
    out.mkdir(exist_ok=False)
    commands = []

    def command(label, argv):
        result = subprocess.run(argv, cwd=root, capture_output=True, text=True, timeout=600)
        # Only known safe audit tools are executed. Do not copy private logs.
        record = {"at": utc(), "label": label, "returncode": result.returncode,
                  "stdout": result.stdout[:4000], "stderr": result.stderr[:1000]}
        commands.append(record)
        with (out / (label + ".json")).open("x") as output:
            json.dump(record, output, indent=2)
        return result.returncode == 0

    context = {"started_at": utc(), "deadline": args.deadline,
               "source_update_requirement": "two real new CAMS runs after original baseline, plus weather update",
               "deadline_is_not_a_pass": True, "attempts_per_audit": 1,
               "cleanup_scope": "two exact synthetic fixtures and their own request/flow/session rows"}
    (out / "context.json").write_text(json.dumps(context, indent=2))
    while time.time() < deadline:
        if (root / "evidence/coexistence-finished").exists():
            break
        if (root / "evidence" / args.label / "failed.json").exists():
            (out / "capacity-attempt-failed.json").write_text(json.dumps(
                {"at": utc(), "outcome": "failed", "automatic_retry": False,
                 "remaining_planned_voice_phases_may_be_unrun": True}, indent=2))
            break
        time.sleep(20)
    if (root / "evidence/coexistence-finished").exists():
        command("voice-recount", ["python3", "scripts/weather43/coexist_report.py", "--root", str(root),
                                  "--label", args.label, "--output", str(root / "evidence/voice/account2/comparison.json")])
        # The workload controller has exited its final voice Docker child before
        # writing coexistence-finished. Never delete an active fixture.
        command("exact-fixture-cleanup", ["python3", "scripts/weather43/cleanup_fixtures.py", "--root", str(root), "--execute"])
    observation = root / "evidence/update-observation"
    coverage = None
    while time.time() < deadline:
        if (observation / "completed.json").exists():
            checkpoint = json.loads((observation / "completed.json").read_text())
            with sqlite3.connect("file:" + str(root / "private/result-data/results.sqlite3") + "?mode=ro", uri=True) as db:
                rows = list(db.execute("SELECT kind,payload FROM results"))
            coverage = {}
            for kind, model in (("weather", "ncep_gfs013"), ("air_quality", "cams_global")):
                entries = [json.loads(body) for k, body in rows if k == kind]
                target = max(checkpoint["seen_runs"][model])
                coverage[kind] = {"expected_entries": 350, "entries": len(entries), "target_run_at": target,
                                  "entries_at_target_run": sum(value["metadata"]["run_at"] == target for value in entries)}
            if all(value["entries"] == value["entries_at_target_run"] == 350 for value in coverage.values()):
                break
        time.sleep(30)
    (out / "coverage-at-end.json").write_text(json.dumps({"at": utc(), "coverage": coverage,
        "deadline_reached": time.time() >= deadline, "observer_completed": (observation / "completed.json").exists()}, indent=2))
    command("source-update-audit", ["python3", "scripts/weather43/summarize_updates.py", "--observation", str(observation),
                                    "--snapshots", str(root / "private/result-data/publication-snapshots"),
                                    "--output", str(observation / "final-audit.json")])
    command("full-raw-recount", ["python3", "scripts/weather43/metrics_report.py", "--evidence", str(root / "evidence"),
                                 "--interface", "ens160", "--disk-device", "sda",
                                 "--output", str(root / "evidence/metrics-final-observation.json")])
    result = {"finished_at": utc(), "deadline_reached": time.time() >= deadline, "commands": commands,
              "source_observation_completed": (observation / "completed.json").exists(),
              "coverage": coverage, "research_failure_is_allowed": True,
              "business_services_modified": False, "git_push_attempted": False}
    (out / "finished.json").write_text(json.dumps(result, indent=2))
    (out / "stage-summary.md").write_text(
        "# Weather43 local experiment stage\n\n" +
        "Finished: " + result["finished_at"] + "\n\n" +
        "Concurrency: 50 environment requests. Historical 350/1000 results remain separate.\n\n" +
        "Real source update observation completed: " + str(result["source_observation_completed"]) + "\n\n" +
        "Audit command outcomes:\n\n" + "\n".join("- " + row["label"] + ": exit " + str(row["returncode"]) for row in commands) +
        "\n\nA completed experiment or successful command does not imply a deployment recommendation. Review raw failures, voice ratios, cold overlap, source availability and sampled resources.\n")
    print(json.dumps({"finished_at": result["finished_at"], "source_observation_completed": result["source_observation_completed"]}))


if __name__ == "__main__":
    main()
