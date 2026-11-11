#!/usr/bin/env python3
"""Measure real anonymous model reads until the two-AQ publication audit ends.

Starts only after undisturbed capacity profiles. No retries or hosted APIs;
metadata and 4KiB ranges use the Ubuntu-owned egress with TLS verification.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import socket
import subprocess
import time
import urllib.request


MODELS = {"ncep_gfs013": "temperature_2m", "cams_global": "pm2_5"}
BASE = "https://openmeteo.s3.amazonaws.com"


def utc():
    return datetime.now(timezone.utc).isoformat()


def state():
    props = subprocess.check_output(
        ["systemctl", "show", "xiaozhi-weather43-egress", "--property=MainPID,ActiveState,UnitFileState,NRestarts"],
        text=True, timeout=5)
    sockets = subprocess.check_output(["ss", "-H", "-lnt"], text=True, timeout=5)
    return {"systemd": dict(line.split("=", 1) for line in props.splitlines() if "=" in line),
            "temporary_reverse_ssh_port_listening": any(re.search(r":17890\s", line) for line in sockets.splitlines())}


def recent_objects(root):
    objects = {}
    for path in (root / "evidence/relay").glob("source-relay.jsonl*"):
        for line in path.read_text().splitlines():
            row = json.loads(line)
            for model, variable in MODELS.items():
                pattern = r"/data/" + model + "/" + variable + r"/chunk_[0-9]+\.om"
                if row.get("status") == 206 and not row.get("error") and re.fullmatch(pattern, row.get("path", "")):
                    if model not in objects or row["at"] > objects[model]["observed_at"]:
                        objects[model] = {"path": row["path"], "observed_at": row["at"]}
    assert objects.keys() == MODELS.keys(), "Need actual local-engine source paths for both models"
    return objects


def read(client, path, byte_range=False):
    start = time.monotonic()
    row = {"at": utc(), "path": path, "range": "bytes=0-4095" if byte_range else None,
           "tls_verification": True, "authorization_sent": False, "ok": False}
    headers = {"User-Agent": "xiaozhi-issue43-network-soak/1"}
    if byte_range:
        headers["Range"] = row["range"]
    try:
        with client.open(urllib.request.Request(BASE + path, headers=headers), timeout=15) as response:
            row.update(http=response.status, etag=response.headers.get("ETag"),
                       content_range=response.headers.get("Content-Range"))
            if response.status != (206 if byte_range else 200):
                raise ValueError("unexpected_source_status")
            if byte_range and not (row["content_range"] or "").startswith("bytes 0-4095/"):
                raise ValueError("invalid_range")
            maximum = 4096 if byte_range else 262144
            body = response.read(maximum + 1)
            if len(body) > maximum or (byte_range and len(body) != 4096):
                raise ValueError("invalid_body_size")
            row.update(bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
            if not byte_range:
                metadata = json.loads(body)
                row.update(run_at=metadata["last_run_initialisation_time"],
                           published_at=metadata["last_run_availability_time"])
            row["ok"] = True
    except Exception as error:
        row.update(error=type(error).__name__, http=getattr(error, "code", row.get("http")))
    row["elapsed_s"] = time.monotonic() - start
    return row


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--interval", type=int, default=60)
    p.add_argument("--max-duration", type=int, default=86400)
    p.add_argument("--deadline", help="Optional absolute UTC cutoff; reaching it is not a successful availability result")
    args = p.parse_args()
    root = args.root.resolve()
    if os.name != "posix" or "Ubuntu" not in Path("/etc/os-release").read_text():
        p.error("Run on the designated Ubuntu server")
    if args.interval < 60 or not 60 <= args.max_duration <= 86400:
        p.error("Use a bounded, low-rate observation")
    deadline = datetime.fromisoformat(args.deadline).timestamp() if args.deadline else None
    if deadline is not None and not time.time() < deadline <= time.time() + 86400:
        p.error("Deadline must be in the next 24 hours")
    assert (root / "evidence/final-capacity-complete").exists()
    assert json.loads((root / "private/fault.json").read_text()) == {}
    objects = recent_objects(root)
    output = root / "evidence/network-soak"
    output.mkdir(exist_ok=False)
    context = {"started_at": utc(), "interval_s": args.interval, "max_duration_s": args.max_duration,
               "absolute_deadline": args.deadline,
               "objects_from_successful_actual_engine_reads": objects,
               "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               "retries": 0, "scope": "independent Ubuntu source availability; not a production SLA"}
    (output / "context.json").write_text(json.dumps(context, indent=2) + "\n")
    client = urllib.request.build_opener(urllib.request.ProxyHandler({"https": "http://127.0.0.1:7890"}))
    started, cycles, attempts, successes = time.monotonic(), 0, 0, 0
    endpoints = {}
    service_valid = True
    with (output / "requests.jsonl").open("x") as log:
        while True:
            cycle = {"at": utc(), "state": state(), "reads": []}
            service = cycle["state"]["systemd"]
            service_valid = service_valid and (service["ActiveState"] == "active" and
                                               service["UnitFileState"] == "enabled" and
                                               not cycle["state"]["temporary_reverse_ssh_port_listening"])
            try:
                cycle["local_dns_addresses"] = sorted({x[4][0] for x in socket.getaddrinfo(
                    "openmeteo.s3.amazonaws.com", 443, type=socket.SOCK_STREAM)})
            except OSError as error:
                cycle["local_dns_error"] = type(error).__name__
            for model in MODELS:
                for path, byte_range in (("/data/" + model + "/static/meta.json", False),
                                         (objects[model]["path"], True)):
                    row = read(client, path, byte_range)
                    row["model"] = model
                    cycle["reads"].append(row)
                    attempts += 1
                    successes += int(row["ok"])
                    key = model + ("/range" if byte_range else "/metadata")
                    endpoint = endpoints.setdefault(key, {"attempts": 0, "success": 0, "errors": Counter(),
                                                           "latencies": [], "failed_streak": 0,
                                                           "longest_consecutive_failed_samples": 0})
                    endpoint["attempts"] += 1
                    endpoint["success"] += int(row["ok"])
                    endpoint["latencies"].append(row["elapsed_s"])
                    endpoint["failed_streak"] = 0 if row["ok"] else endpoint["failed_streak"] + 1
                    endpoint["longest_consecutive_failed_samples"] = max(
                        endpoint["longest_consecutive_failed_samples"], endpoint["failed_streak"])
                    if not row["ok"]:
                        endpoint["errors"][row["error"]] += 1
            log.write(json.dumps(cycle) + "\n")
            log.flush()
            cycles += 1
            complete = (root / "evidence/update-observation/completed.json").exists()
            if complete or time.monotonic() - started >= args.max_duration or (deadline is not None and time.time() >= deadline):
                break
            time.sleep(max(0, started + cycles * args.interval - time.monotonic()))
    for endpoint in endpoints.values():
        latency = sorted(endpoint.pop("latencies"))
        endpoint.pop("failed_streak")
        endpoint["latency_s"] = {"p95_all_attempts": latency[math.ceil(len(latency) * .95) - 1],
                                 "max": max(latency)}
        endpoint["failure"] = endpoint["attempts"] - endpoint["success"]
    summary = {"started_at": context["started_at"], "finished_at": utc(),
               "elapsed_s": time.monotonic() - started, "cycles": cycles,
               "attempts": attempts, "success": successes, "failure": attempts - successes,
               "service_enabled_active_and_no_reverse_ssh_in_all_samples": service_valid,
               "endpoints": endpoints,
               "stop_reason": "observed_second_AQ_cache_update" if complete else "bounded_deadline",
               "limitations": ["Minute samples can miss shorter outages", "Small reads do not measure sustained bandwidth",
                               "Range object paths were real engine reads at the start; not a full model-run audit"]}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
