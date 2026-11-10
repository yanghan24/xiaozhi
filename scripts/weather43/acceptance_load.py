#!/usr/bin/env python3
"""Record real unified environment HTTP load on the designated Ubuntu host."""
import argparse
import asyncio
from datetime import datetime
import json
import math
from pathlib import Path
import resource
import time
import urllib.parse

from load_local import docker_stats, percentile, request, require_ubuntu, snapshot, utc


def check_payload(data, city_id):
    if str(data["city"]["id"]) != str(city_id):
        raise ValueError("cross_city_response")
    for kind, expected in (("weather", {"temperature_2m": "°C", "relative_humidity_2m": "%", "weather_code": "wmo code"}),
                           ("air_quality", {"pm2_5": "μg/m³", "pm10": "μg/m³"})):
        component = data["components"][kind]
        if component["values"] is None:
            if component["status"] not in ("loading", "unavailable") or component["valid_at"] is not None:
                raise ValueError("fabricated_missing_data")
            continue
        if component["status"] not in ("fresh", "stale") or component["units"] != expected:
            raise ValueError("invalid_status_or_units")
        if component["model_estimate"] is not True:
            raise ValueError("missing_model_estimate_label")
        validity = datetime.fromisoformat(component["valid_at"]).timestamp()
        if not 0 <= time.time() - validity < 7200:
            raise ValueError("invalid_current_hour")
        for field in expected:
            value = component["values"][field]
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError("invalid_field")
    complete = all(c["values"] is not None for c in data["components"].values())
    fresh = all(c["status"] == "fresh" for c in data["components"].values())
    if data["complete"] != complete or data["fresh"] != fresh:
        raise ValueError("invalid_aggregate_state")


async def run(args, output):
    cities = json.loads(args.cities_file.read_text())["cities"][:args.cities]
    readiness_start = time.monotonic()
    while True:
        try:
            status, body = await asyncio.wait_for(request("127.0.0.1", args.port, "/health"), 2)
            if status == 200 and json.loads(body).get("status") == "ok":
                break
        except Exception:
            pass
        if time.monotonic() - readiness_start >= 60:
            raise RuntimeError("Gateway did not become ready within 60 seconds")
        await asyncio.sleep(.5)
    readiness_s = time.monotonic() - readiness_start
    start = time.monotonic()
    stopped = asyncio.Event()
    samples = []

    async def sample():
        while not stopped.is_set():
            point = snapshot(args.data_dir)
            point["docker_stats"] = await docker_stats()
            samples.append(point)
            try:
                await asyncio.wait_for(stopped.wait(), 3)
            except asyncio.TimeoutError:
                pass

    async def call(index, delay):
        scheduled = start + delay
        await asyncio.sleep(max(0, scheduled - time.monotonic()))
        dispatched = time.monotonic()
        city = cities[index % args.devices % len(cities)]
        path = "/v1/environment?" + urllib.parse.urlencode({"city_id": city["id"]})
        row = {"index": index, "city_id": city["id"], "scheduled_after_s": delay,
               "generator_lateness_s": dispatched - scheduled, "at": utc()}
        try:
            status, body = await asyncio.wait_for(request("127.0.0.1", args.port, path), args.timeout)
            row.update(http=status, bytes=len(body))
            if status not in (200, 202):
                raise ValueError("unexpected_http_status")
            data = json.loads(body)
            check_payload(data, city["id"])
            outcome = "fresh" if data["fresh"] else ("stale" if data["complete"] else "loading_or_unavailable")
            row.update(outcome=outcome, payload=data)
        except asyncio.TimeoutError:
            row["outcome"] = "timeout"
        except Exception as exc:
            row.update(outcome="failure", error=type(exc).__name__, detail=str(exc)[:120])
        row["end_to_end_s"] = time.monotonic() - scheduled
        row["socket_s"] = time.monotonic() - dispatched
        output.write(json.dumps(row, ensure_ascii=False) + "\n")
        output.flush()
        return {k: v for k, v in row.items() if k != "payload"}

    monitor = asyncio.create_task(sample())
    try:
        if args.scenario == "burst":
            plan = [(i, 0) for i in range(args.devices)]
        else:
            plan = [(i + turn * args.devices, turn * args.period + i * args.period / args.devices)
                    for turn in range(math.ceil(args.duration / args.period)) for i in range(args.devices)
                    if turn * args.period + i * args.period / args.devices < args.duration]
        results = await asyncio.gather(*(call(i, delay) for i, delay in plan))
        if args.scenario == "steady":
            await asyncio.sleep(max(0, start + args.duration - time.monotonic()))
    finally:
        stopped.set()
        await monitor
    latency = [r["end_to_end_s"] for r in results]
    return {"finished_at": utc(), "elapsed_s": time.monotonic() - start, "gateway_readiness_wait_s": readiness_s,
            "offered": len(results),
            "counts": {name: sum(r["outcome"] == name for r in results)
                       for name in ("fresh", "stale", "loading_or_unavailable", "failure", "timeout")},
            "latency_s": {"p50": percentile(latency, .5), "p95": percentile(latency, .95),
                          "p99": percentile(latency, .99), "max": max(latency)},
            "resources": samples, "valid_coordinate_count": len(cities)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cities-file", required=True, type=Path)
    p.add_argument("--cities", type=int, choices=(1, 10, 35, 350), required=True)
    p.add_argument("--devices", type=int, choices=(350, 1000), required=True)
    p.add_argument("--scenario", choices=("burst", "steady"), required=True)
    p.add_argument("--duration", type=int, default=1800)
    p.add_argument("--period", type=int, default=300)
    p.add_argument("--timeout", type=float, default=2)
    p.add_argument("--port", type=int, default=18086)
    p.add_argument("--data-dir", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    require_ubuntu(p)
    if resource.getrlimit(resource.RLIMIT_NOFILE)[0] < args.devices + 128:
        p.error("Increase this test shell's nofile limit to 8192")
    if not 0 < args.timeout <= 30 or not 1 <= args.duration <= 3600 or not 1 <= args.period <= 600:
        p.error("Unbounded or invalid test parameters")
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = {"started_at": utc(), "settings": vars(args), "scope": "unified_environment_feasibility_gateway",
                "generator_nofile": resource.getrlimit(resource.RLIMIT_NOFILE),
                "limitations": ["No physical devices or production device authentication",
                                "Generator shares the host; sampled peaks may be missed",
                                "Fresh, stale and incomplete responses are counted separately"]}
    with (args.output / "requests.jsonl").open("x", encoding="utf-8") as output:
        manifest.update(asyncio.run(run(args, output)))
    with (args.output / "summary.json").open("x", encoding="utf-8") as output:
        json.dump(manifest, output, ensure_ascii=False, default=str, indent=2)
    print(json.dumps({k: manifest[k] for k in ("scope", "offered", "counts", "latency_s", "elapsed_s")}))
    return 0 if manifest["counts"]["failure"] + manifest["counts"]["timeout"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
