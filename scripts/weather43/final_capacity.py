#!/usr/bin/env python3
"""Validate the final cache version after isolated faults and egress restart."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.request


CONTAINERS = (
    "xiaozhi-pr26-api", "xiaozhi-pr26-voice",
    "xiaozhi-weather43-acceptance-openmeteo-1",
    "xiaozhi-weather43-acceptance-result-cache-1",
    "xiaozhi-weather43-acceptance-data-relay-1",
)


def utc():
    return datetime.now(timezone.utc).isoformat()


def inspect():
    result = {}
    for name in CONTAINERS:
        value = json.loads(subprocess.check_output(["docker", "inspect", name], text=True))[0]
        state = value["State"]
        result[name] = {"image_id": value["Image"], "started_at": state["StartedAt"],
                        "running": state["Running"], "oom_killed": state["OOMKilled"],
                        "restart_count": value["RestartCount"]}
    return result


def listen_counters():
    code = ("import json;lines=open('/proc/net/netstat').read().splitlines();"
            "rows=[x.split() for x in lines if x.startswith('TcpExt:')];"
            "v=dict(zip(rows[0][1:],map(int,rows[1][1:])));"
            "print(json.dumps({k:v.get(k) for k in "
            "['ListenOverflows','ListenDrops','TCPBacklogDrop']}))")
    return json.loads(subprocess.check_output(
        ["docker", "exec", CONTAINERS[3], "python", "-c", code], text=True))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    args = p.parse_args()
    root = args.root.resolve()
    if os.name != "posix" or os.geteuid() != 0 or "Ubuntu" not in Path("/etc/os-release").read_text():
        p.error("Run as root on the designated Ubuntu server")
    for marker in ("recovery-complete", "network-restart-complete"):
        if not (root / "evidence" / marker).exists():
            p.error("Complete independent recovery and network checks first")
    assert json.loads((root / "private/fault.json").read_text()) == {}
    names = subprocess.check_output(["docker", "ps", "--format", "{{.Names}}"], text=True).splitlines()
    assert not {"weather43-cold-engine", "weather43-recovery-fixture", "weather43-voice-probe"}.intersection(names)
    output = root / "evidence/final-v4"
    output.mkdir(exist_ok=False)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def get(path):
        with opener.open("http://127.0.0.1:18086/" + path, timeout=5) as response:
            return json.load(response)

    started = time.monotonic()
    while not get("v1/environment?city_id=1815577")["fresh"]:
        if time.monotonic() - started > 240:
            raise RuntimeError("Fresh source readiness deadline exceeded")
        time.sleep(2)
    initial = inspect()
    assert all(v["running"] and not v["oom_killed"] for v in initial.values())
    health = get("health")
    source_hash = hashlib.sha256((root / "scripts/weather43/cache_service.py").read_bytes()).hexdigest()
    assert health["runtime_source_sha256"] == source_hash and health["cache_entries"] == 700
    before = {"at": utc(), "containers": initial, "health": health,
              "cache_runtime_sha256": source_hash, "listen_counters": listen_counters(),
              "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (output / "before.json").write_text(json.dumps(before, indent=2) + "\n")
    results = []
    for devices in (350, 1000):
        for cities in (1, 35, 350):
            results.append((f"burst-{devices}-cities{cities}", "burst", devices, cities))
    results.append(("steady-350-cities350", "steady", 350, 350))
    for label, scenario, devices, cities in results:
        subprocess.run([sys.executable, str(root / "scripts/weather43/acceptance_load.py"),
                        "--cities-file", str(root / "private/cities.json"), "--cities", str(cities),
                        "--devices", str(devices), "--scenario", scenario,
                        "--duration", "1800", "--period", "300", "--timeout", "2",
                        "--data-dir", str(root / "private/model-data"),
                        "--output", str(output / label)], cwd=root, check=True, timeout=1900)
        summary = json.loads((output / label / "summary.json").read_text())
        counts = summary["counts"]
        assert counts["failure"] == counts["timeout"] == counts["loading_or_unavailable"] == 0
        if scenario == "steady":
            assert summary["offered"] == 2100 and summary["elapsed_s"] >= 1800
    after = {"at": utc(), "containers": inspect(), "health": get("health"),
             "listen_counters": listen_counters()}
    (output / "after.json").write_text(json.dumps(after, indent=2) + "\n")
    assert after["containers"] == initial
    assert after["health"]["runtime_source_sha256"] == source_hash
    assert after["listen_counters"] == before["listen_counters"]
    assert json.loads((root / "private/fault.json").read_text()) == {}
    (root / "evidence/final-capacity-complete").write_text(utc() + "\n")


if __name__ == "__main__":
    main()
