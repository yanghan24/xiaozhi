#!/usr/bin/env python3
"""Wait for a fixed deployment window before another capacity verification.

Read-only while waiting; never pauses, reverts or redeploys business services.
Earlier HTTP results and failed version-consistency gates remain intact.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

from final_capacity import inspect


def utc():
    return datetime.now(timezone.utc).isoformat()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--label", required=True)
    p.add_argument("--predecessor", required=True)
    p.add_argument("--quiet-seconds", type=int, default=2700)
    p.add_argument("--max-wait", type=int, default=14400)
    args = p.parse_args()
    root = args.root.resolve()
    if os.name != "posix" or os.geteuid() != 0 or "Ubuntu" not in Path("/etc/os-release").read_text():
        p.error("Run as root on the designated Ubuntu test host")
    if not all(re.fullmatch(r"[a-z0-9-]{1,40}", value) for value in (args.label, args.predecessor)):
        p.error("Use simple evidence directory labels")
    if not 60 <= args.quiet_seconds <= 3600 or not args.quiet_seconds <= args.max_wait <= 21600:
        p.error("Use bounded stabilization and waiting periods")
    assert not (root / "evidence/final-capacity-complete").exists()
    assert json.loads((root / "private/fault.json").read_text()) == {}
    output = root / "evidence" / ("stability-" + args.label)
    output.mkdir(exist_ok=False)
    context = {"started_at": utc(), "quiet_seconds": args.quiet_seconds, "max_wait_s": args.max_wait,
               "next_capacity_directory": args.label, "predecessor": args.predecessor,
               "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               "business_services_modified": False}
    (output / "context.json").write_text(json.dumps(context, indent=2) + "\n")
    started = time.monotonic()
    quiet_since, previous, changes, ready = started, None, 0, False
    with (output / "observations.jsonl").open("x") as observations:
        while time.monotonic() - started < args.max_wait:
            current = inspect()
            now = time.monotonic()
            running = all(value["running"] and not value["oom_killed"] for value in current.values())
            changed = previous is not None and current != previous
            if previous is None or changed or not running:
                changes += int(changed)
                quiet_since = now
            previous = current
            row = {"at": utc(), "containers": current, "running": running,
                   "changed": changed, "quiet_observed_s": now - quiet_since}
            observations.write(json.dumps(row) + "\n")
            observations.flush()
            prior_finished = (root / "evidence" / args.predecessor / "after.json").exists()
            if running and prior_finished and now - quiet_since >= args.quiet_seconds:
                ready = True
                break
            time.sleep(60)
    result = {"waiting_finished_at": utc(), "observed_version_changes": changes,
              "stable_window_observed": ready, "waiting_elapsed_s": time.monotonic() - started}
    if ready:
        child = subprocess.Popen([sys.executable, str(root / "scripts/weather43/final_capacity.py"),
                                  "--root", str(root), "--label", args.label],
                                 cwd=root, start_new_session=True)
        try:
            code = child.wait(timeout=2400)
        except subprocess.TimeoutExpired:
            # Only the process group created above, including its own HTTP generator.
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=10)
            code = 124
            result["status"] = "capacity_deadline_exceeded"
        result.update(capacity_exit_code=code,
                      final_capacity_marker_present=(root / "evidence/final-capacity-complete").exists())
    else:
        result["status"] = "stable_deployment_window_not_available"
    result["finished_at"] = utc()
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
    return 0 if ready and result["capacity_exit_code"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
