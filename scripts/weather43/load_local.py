#!/usr/bin/env python3
"""Measure real weather/AQ requests on Ubuntu against a loopback Open-Meteo.

This measures the model engine, not the authenticated product service or speech
concurrency. Each offered device request sends one weather and one AQ request.
No mocks, hosted fallback, retries, or automatic cache clearing are provided.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import ipaddress
import json
import math
from pathlib import Path
import platform
import shutil
import time
import urllib.parse


# Representative WGS84 city centres, not an offline city geocoder.
CITIES = [
    ("Changsha", 28.2282, 112.9388), ("Beijing", 39.9042, 116.4074),
    ("Shanghai", 31.2304, 121.4737), ("Guangzhou", 23.1291, 113.2644),
    ("Shenzhen", 22.5431, 114.0579), ("Chengdu", 30.5728, 104.0668),
    ("Wuhan", 30.5928, 114.3055), ("Xian", 34.3416, 108.9398),
    ("Harbin", 45.8038, 126.5349), ("Urumqi", 43.8256, 87.6168),
]
VARIABLES = {"weather": {"temperature_2m": "°C", "relative_humidity_2m": "%", "weather_code": "wmo code"},
             "air_quality": {"pm2_5": "μg/m³", "pm10": "μg/m³"}}


def utc():
    return datetime.now(timezone.utc).isoformat()


def require_ubuntu(parser):
    path = Path("/etc/os-release")
    if platform.system() != "Linux" or not path.exists() or '\nID=ubuntu\n' not in '\n' + path.read_text() + '\n':
        parser.error("Run backend validation on the designated Ubuntu host")


def snapshot(data_dir):
    """Read host counters; these include load-generator overhead and other work."""
    report = {"at": utc(), "monotonic_s": time.monotonic(), "loadavg": Path("/proc/loadavg").read_text().strip(),
              "cpu_ticks": Path("/proc/stat").read_text().splitlines()[0],
              "disk_counters": Path("/proc/diskstats").read_text().splitlines(),
              "network_counters": Path("/proc/net/dev").read_text().splitlines()[2:]}
    report["memory_kib"] = {line.split(":")[0]: int(line.split()[1])
                            for line in Path("/proc/meminfo").read_text().splitlines()
                            if line.split(":")[0] in {"MemTotal", "MemAvailable", "SwapTotal", "SwapFree"}}
    usage = shutil.disk_usage(data_dir)
    report["disk_free_bytes"] = usage.free
    cache = data_dir / "cache.bin"
    if cache.exists():
        stat = cache.stat()
        report["cache_file_bytes"] = stat.st_size
        report["cache_allocated_bytes"] = stat.st_blocks * 512
    return report


async def docker_stats():
    try:
        process = await asyncio.create_subprocess_exec(
            "sudo", "-n", "docker", "stats", "--no-stream", "--format", "{{json .}}",
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
    except OSError as exc:
        return {"error": "docker_stats_unavailable", "type": type(exc).__name__}
    try:
        stdout, stderr = await asyncio.wait_for(process.communicate(), 6)
    except asyncio.TimeoutError:
        process.kill()
        await process.communicate()
        return {"error": "docker_stats_timeout"}
    if process.returncode:
        return {"error": "docker_stats_unavailable", "exit_code": process.returncode}
    try:
        return [json.loads(line) for line in stdout.decode().splitlines() if line]
    except ValueError:
        return {"error": "docker_stats_invalid_json"}


async def read_body(reader, headers):
    maximum = 1048576
    if "chunked" in headers.get("transfer-encoding", "").lower():
        chunks, size = [], 0
        while True:
            line = await reader.readline()
            length = int(line.split(b";", 1)[0], 16)
            if not length:
                return b"".join(chunks)
            size += length
            if size > maximum:
                raise ValueError("Response body too large")
            chunks.append(await reader.readexactly(length))
            if await reader.readexactly(2) != b"\r\n":
                raise ValueError("Invalid chunk framing")
    if "content-length" in headers:
        length = int(headers["content-length"])
        if not 0 <= length <= maximum:
            raise ValueError("Response body too large")
        return await reader.readexactly(length)
    chunks, size = [], 0
    while True:
        chunk = await reader.read(65536)
        if not chunk:
            return b"".join(chunks)
        chunks.append(chunk)
        size += len(chunk)
        if size > maximum:
            raise ValueError("Response body too large")


async def request(host, port, path):
    reader, writer = await asyncio.open_connection(host, port)
    try:
        writer.write(("GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + str(port) +
                      "\r\nAccept: application/json\r\nConnection: close\r\n\r\n").encode("ascii"))
        await writer.drain()
        raw_headers = await reader.readuntil(b"\r\n\r\n")
        lines = raw_headers.decode("iso-8859-1").split("\r\n")
        status = int(lines[0].split()[1])
        headers = {line.split(":", 1)[0].lower(): line.split(":", 1)[1].strip()
                   for line in lines[1:] if ":" in line}
        body = await read_body(reader, headers)
        return status, body
    finally:
        writer.close()
        await writer.wait_closed()


def validate(body, kind):
    data = json.loads(body)
    hourly, units = data["hourly"], data["hourly_units"]
    times = hourly["time"]
    if not times or any(not isinstance(t, (int, float)) for t in times):
        raise ValueError("Missing numeric model validity times")
    current_hour = int(time.time()) // 3600 * 3600
    if not any(abs(t - current_hour) <= 3600 for t in times):
        raise ValueError("Forecast does not cover the current hour")
    if data.get("utc_offset_seconds") != 0:
        raise ValueError("Expected UTC output for the probe")
    for name, expected_unit in VARIABLES[kind].items():
        values = hourly[name]
        if units.get(name) != expected_unit or len(values) != len(times):
            raise ValueError("Wrong units or time-series length for " + name)
        if any(v is None or isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
            raise ValueError("Missing/non-finite model field " + name)
        if name == "relative_humidity_2m" and any(not 0 <= v <= 100 for v in values):
            raise ValueError("Humidity outside 0..100")
        if name.startswith("pm") and any(v < 0 for v in values):
            raise ValueError("Negative PM concentration")
    return data


async def endpoint(args, kind, city):
    params = {"latitude": city[1], "longitude": city[2], "forecast_hours": 6,
              "timeformat": "unixtime", "timezone": "GMT", "elevation": "nan",
              "cell_selection": "nearest", "hourly": ",".join(VARIABLES[kind])}
    if kind == "weather":
        params["models"] = args.weather_model
        path = "/v1/forecast"
    else:
        params["domains"] = "cams_global"
        path = "/v1/air-quality"
    path += "?" + urllib.parse.urlencode(params)
    started = time.monotonic()
    record = {"kind": kind, "path": path}
    try:
        status, body = await asyncio.wait_for(request(args.host, args.port, path), args.timeout)
        record.update(http=status, bytes=len(body))
        if status != 200:
            raise ValueError("HTTP " + str(status) + ": " + body.decode("utf-8", "replace")[:180])
        record.update(data=validate(body, kind), outcome="success")
    except asyncio.TimeoutError:
        record["outcome"] = "timeout"
    except Exception as exc:
        record.update(outcome="failure", error=type(exc).__name__, detail=str(exc)[:240])
    record["elapsed_s"] = round(time.monotonic() - started, 6)
    return record


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


async def run(args, output):
    semaphore = asyncio.Semaphore(args.inflight)
    start = time.monotonic()
    resources = []
    stop = asyncio.Event()

    async def sample():
        while not stop.is_set():
            point = snapshot(args.data_dir)
            point["docker_stats"] = await docker_stats()
            resources.append(point)
            try:
                await asyncio.wait_for(stop.wait(), 1)
            except asyncio.TimeoutError:
                pass

    async def device(index):
        delay = args.spread * index / args.devices
        scheduled = start + delay
        await asyncio.sleep(max(0, scheduled - time.monotonic()))
        entered = time.monotonic()
        city = CITIES[0 if args.cities == 1 else index % len(CITIES)]
        async with semaphore:
            dispatched = time.monotonic()
            calls = await asyncio.gather(endpoint(args, "weather", city), endpoint(args, "air_quality", city))
        outcome = "success" if all(c["outcome"] == "success" for c in calls) else (
            "timeout" if any(c["outcome"] == "timeout" for c in calls) else "failure")
        record = {"index": index, "city": city[0], "scheduled_after_s": delay,
                  "generator_lateness_s": entered - scheduled, "queue_s": dispatched - entered,
                  "end_to_end_s": time.monotonic() - scheduled, "outcome": outcome, "calls": calls}
        output.write(json.dumps(record, ensure_ascii=False) + "\n")
        output.flush()
        return record

    sampler = asyncio.create_task(sample())
    try:
        rows = await asyncio.gather(*(device(i) for i in range(args.devices)))
    finally:
        stop.set()
        await sampler
    resources.append(snapshot(args.data_dir))
    elapsed = time.monotonic() - start
    calls = [call for row in rows for call in row["calls"]]
    latencies = [row["end_to_end_s"] for row in rows]
    counts = {k: sum(row["outcome"] == k for row in rows) for k in ("success", "failure", "timeout")}
    return {"finished_at": utc(), "elapsed_s": elapsed, "devices_offered": len(rows), "counts": counts,
            "http_calls": len(calls), "http_counts": {k: sum(c["outcome"] == k for c in calls)
                                                     for k in ("success", "failure", "timeout")},
            "requests_per_s": len(rows) / elapsed,
            "all_device_latency_s": {"p50": percentile(latencies, .5), "p95": percentile(latencies, .95),
                                     "p99": percentile(latencies, .99), "max": max(latencies)},
            "resources": resources}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18084)
    parser.add_argument("--devices", type=int, default=1)
    parser.add_argument("--inflight", type=int, default=1, help="Max in-flight device requests, each with two HTTP calls")
    parser.add_argument("--cities", type=int, choices=(1, 10), default=1)
    parser.add_argument("--weather-model", choices=("gfs013", "icon_global"), default="gfs013")
    parser.add_argument("--spread", type=float, default=0, help="Spread arrivals over this many seconds; zero is a burst")
    parser.add_argument("--timeout", type=float, default=30, help="Per-endpoint socket deadline, excluding admission queue")
    parser.add_argument("--data-dir", type=Path, default=Path(".runtime/weather43/model-data"))
    parser.add_argument("--label", required=True, help="Record the actual cache state and scenario")
    parser.add_argument("--output-dir", required=True, type=Path, help="New evidence directory")
    args = parser.parse_args()
    require_ubuntu(parser)
    if not ipaddress.ip_address(args.host).is_loopback:
        parser.error("Only a local self-hosted endpoint may be load tested")
    if not 1 <= args.devices <= 1000 or not 1 <= args.inflight <= args.devices or not 1 <= args.port <= 65535:
        parser.error("Invalid port or concurrency; maximum 1000 devices")
    if not 0 < args.timeout <= 120 or not 0 <= args.spread <= 3600:
        parser.error("Invalid timeout or arrival spread")
    import resource
    descriptors = resource.getrlimit(resource.RLIMIT_NOFILE)
    if descriptors[0] < 2 * args.inflight + 128:
        parser.error("Load generator needs nofile >= %d; increase this shell's limit before retrying" %
                     (2 * args.inflight + 128))
    if not args.data_dir.is_dir():
        parser.error("Data directory must already exist")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    manifest = {"started_at": utc(), "system": platform.platform(), "label": args.label,
                "scope": "self_hosted_model_engine", "settings": vars(args), "generator_nofile": descriptors,
                "limitations": ["Not a product authentication, result-cache, IP location, device or voice test",
                                "10 representative coordinates; no 350/1000 distinct-city coverage",
                                "Cold cache must be independently established, never inferred from label",
                                "Host counters include the load generator and other workloads",
                                "HTTP deadline excludes the measured admission queue; no product SLA gate",
                                "Model validity time does not prove a recent model run or source update"]}
    with (args.output_dir / "requests.jsonl").open("x", encoding="utf-8") as output:
        manifest.update(asyncio.run(run(args, output)))
    with (args.output_dir / "summary.json").open("x", encoding="utf-8") as output:
        json.dump(manifest, output, ensure_ascii=False, default=str, indent=2)
        output.write("\n")
    print(json.dumps({"counts": manifest["counts"], "elapsed_s": manifest["elapsed_s"],
                      "scope": manifest["scope"], "output_dir": str(args.output_dir)}))
    return 0 if manifest["counts"]["success"] == args.devices else 1


if __name__ == "__main__":
    raise SystemExit(main())
