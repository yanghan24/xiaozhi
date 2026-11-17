#!/usr/bin/env python3
"""Observe real source publications and the independent local cache over time."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time
import urllib.request


def utc():
    return datetime.now(timezone.utc).isoformat()


def collect_cache_runs(row, seen):
    payload = row.get("/v1/environment?city_id=changsha&peek=1", {}).get("data", {})
    # Include earlier observer history from before read-only peek was introduced.
    if not payload:
        payload = row.get("/v1/environment?city_id=changsha", {}).get("data", {})
    for kind, model in (("weather", "ncep_gfs013"), ("air_quality", "cams_global")):
        value = payload.get("components", {}).get(kind, {}).get("model_run_at")
        if value:
            run = int(datetime.fromisoformat(value).timestamp())
            if run not in seen[model]:
                seen[model].append(run)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--interval", type=int, default=300)
    p.add_argument("--proxy", default="http://127.0.0.1:7890")
    p.add_argument("--cache-url", default="http://127.0.0.1:18086")
    p.add_argument("--stop-after-aq-updates", type=int, default=0,
                   help="Stop collection after this many source AND cached AQ transitions plus a weather transition")
    p.add_argument("--samples", type=int, default=0, help="Bound a diagnostic run; zero means continuous observation")
    args = p.parse_args()
    if args.interval < 1 or args.samples < 0 or args.stop_after_aq_updates < 0:
        p.error("Invalid observation limits")
    args.output.mkdir(parents=True, exist_ok=True)
    source = urllib.request.build_opener(urllib.request.ProxyHandler({"https": args.proxy}))
    local = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    samples = args.output / "source-publications.jsonl"
    baseline, seen = {}, {"ncep_gfs013": [], "cams_global": []}
    cached_runs = {"ncep_gfs013": [], "cams_global": []}
    if samples.exists():
        for line in samples.read_text().splitlines():
            row = json.loads(line)
            collect_cache_runs(row, cached_runs)
            for name, data in row.get("models", {}).items():
                run = data.get("metadata", {}).get("last_run_initialisation_time")
                if run:
                    baseline.setdefault(name, run)
                    if run not in seen[name]:
                        seen[name].append(run)
    taken = 0
    while True:
        row = {"at": utc(), "models": {}, "route": "ubuntu_local_independent_proxy"}
        for model in seen:
            started = time.monotonic()
            try:
                url = "https://openmeteo.s3.amazonaws.com/data/" + model + "/static/meta.json"
                with source.open(url, timeout=30) as response:
                    body = response.read(262144)
                    metadata = json.loads(body)
                    row["models"][model] = {"http": response.status, "metadata": metadata,
                                             "etag": response.headers.get("ETag"),
                                             "last_modified": response.headers.get("Last-Modified"),
                                             "sha256": hashlib.sha256(body).hexdigest(),
                                             "elapsed_s": time.monotonic() - started}
                run = metadata["last_run_initialisation_time"]
                baseline.setdefault(model, run)
                if run not in seen[model]:
                    seen[model].append(run)
            except Exception as exc:
                row["models"][model] = {"error": type(exc).__name__, "elapsed_s": time.monotonic() - started}
        for suffix in ("/health", "/v1/environment?city_id=changsha&peek=1"):
            try:
                with local.open(args.cache_url + suffix, timeout=5) as response:
                    row[suffix] = {"http": response.status, "data": json.load(response)}
            except Exception as exc:
                row[suffix] = {"error": type(exc).__name__}
        with samples.open("a", encoding="utf-8") as f:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")
            f.flush()
        collect_cache_runs(row, cached_runs)
        checkpoint = {"at": utc(), "baseline": baseline, "seen_runs": seen,
                      "new_runs_after_baseline": {k: len([x for x in v if x > baseline.get(k, x)])
                                                   for k, v in seen.items()},
                      "cached_runs": cached_runs,
                      "cached_new_runs_after_baseline": {k: len([x for x in v if x > baseline.get(k, x)])
                                                          for k, v in cached_runs.items()},
                      "note": "Source transitions alone do not prove cache promotion; inspect paired local samples"}
        temporary = args.output / "checkpoint.tmp"
        temporary.write_text(json.dumps(checkpoint, indent=2) + "\n")
        temporary.replace(args.output / "checkpoint.json")
        print(json.dumps(checkpoint), flush=True)
        taken += 1
        target = args.stop_after_aq_updates
        cached_response = row.get("/v1/environment?city_id=changsha&peek=1", {}).get("data", {})
        if (target and checkpoint["new_runs_after_baseline"]["cams_global"] >= target and
                checkpoint["cached_new_runs_after_baseline"]["cams_global"] >= target and
                checkpoint["cached_new_runs_after_baseline"]["ncep_gfs013"] >= 1 and cached_response.get("fresh")):
            (args.output / "completed.json").write_text(json.dumps(checkpoint, indent=2) + "\n")
            return
        if args.samples and taken >= args.samples:
            return
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
