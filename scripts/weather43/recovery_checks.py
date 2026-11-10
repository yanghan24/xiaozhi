#!/usr/bin/env python3
"""Bounded Ubuntu-only fault and complete cold-cache acceptance experiments.

Only mutates the named isolated weather43 services, private fault file and new
fixture directories. Existing model/result data is retained. Never changes the
business voice/API service or host networking/firewall.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import json
import hashlib
import os
from pathlib import Path
import sqlite3
import subprocess
import time
import urllib.request

from load_local import docker_stats, snapshot


def utc():
    return datetime.now(timezone.utc).isoformat()


class Experiment:
    def __init__(self, args):
        self.args = args
        self.root = args.root.resolve()
        self.evidence = self.root / "evidence/recovery"
        self.evidence.mkdir(exist_ok=False)
        self.fault_path = self.root / "private/fault.json"
        self.compose = ["docker", "compose", "--env-file", "private/acceptance.env", "-p",
                        "xiaozhi-weather43-acceptance", "--project-directory", str(self.root),
                        "-f", "deploy/weather43/compose.acceptance.yaml"]
        self.fixture = "weather43-recovery-fixture"
        self.cold_engine = "weather43-cold-engine"
        self.notes = []
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.gateway = dict(line.strip().split("=", 1) for line in
                            (self.root / "private/acceptance.env").read_text().splitlines()
                            if line.strip() and not line.startswith("#"))["WEATHER43_GATEWAY"]

    def cmd(self, args):
        result = subprocess.run(args, cwd=self.root, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise RuntimeError("isolated_command_failed_" + args[0])
        return result.stdout

    def record(self, name, data):
        with (self.evidence / (name + ".json")).open("x") as output:
            json.dump({"at": utc(), **data}, output, ensure_ascii=False, indent=2)

    def fault(self, data):
        # Keep the bind-mounted inode. Only this trial relay reads this file.
        with self.fault_path.open("w") as output:
            json.dump(data, output)
            output.flush()
            os.fsync(output.fileno())
        self.notes.append({"at": utc(), "fault": data})

    def get(self, path="/health", port=18086):
        with self.opener.open("http://127.0.0.1:" + str(port) + path, timeout=2) as response:
            return {"http": response.status, "data": json.load(response)}

    def ready(self, port=18086):
        started = time.monotonic()
        while time.monotonic() - started < 60:
            try:
                value = self.get(port=port)
                if value["data"]["status"] == "ok":
                    return time.monotonic() - started
            except Exception:
                pass
            time.sleep(.2)
        raise TimeoutError("trial_gateway_readiness")

    def environment(self, port=18086):
        return self.get("/v1/environment?city_id=1815577&peek=1", port)

    def wait_status(self, weather, aq, seconds=360):
        started = time.monotonic()
        while time.monotonic() - started < seconds:
            data = self.environment()["data"]["components"]
            if data["weather"]["status"] == weather and data["air_quality"]["status"] == aq:
                return time.monotonic() - started
            time.sleep(1)
        raise TimeoutError("component_status_" + weather + "_" + aq)

    def load(self, name, devices=350, cities=350, port=18086, data_dir="private/model-data"):
        self.cmd(["python3", "scripts/weather43/acceptance_load.py", "--cities-file", "private/cities.json",
                  "--data-dir", str(data_dir), "--devices", str(devices), "--cities", str(cities),
                  "--port", str(port), "--scenario", "burst", "--output", str(self.evidence / name)])
        return json.loads((self.evidence / name / "summary.json").read_text())["counts"]

    def start_fixture(self, folder, engine="http://openmeteo:8080", short_timeout=False, prewarm=0,
                      copy_all=False, ttl=300):
        data = self.root / "private" / folder
        data.mkdir(exist_ok=False)
        os.chown(data, 65532, 65532)
        if short_timeout or copy_all:
            with sqlite3.connect(str(self.root / "private/result-data/results.sqlite3")) as source:
                rows = list(source.execute("SELECT city,kind,payload FROM results" +
                                           ("" if copy_all else " WHERE city='1815577'")))
            assert len(rows) == (700 if copy_all else 2)
            with sqlite3.connect(str(data / "results.sqlite3")) as target:
                target.execute("CREATE TABLE results(city TEXT,kind TEXT,payload TEXT,PRIMARY KEY(city,kind))")
                target.executemany("INSERT INTO results VALUES(?,?,?)", rows)
            os.chown(data / "results.sqlite3", 65532, 65532)
        self.cmd(["docker", "run", "-d", "--name", self.fixture, "--network", "xiaozhi-weather43-acceptance_default",
                  "--read-only", "--user", "65532:65532", "--memory", "256m", "--cpus", ".75",
                  "--ulimit", "nofile=8192:8192", "--cap-drop", "ALL", "--security-opt", "no-new-privileges",
                  "--tmpfs", "/tmp:size=16m", "-p", "127.0.0.1:18087:8081",
                  "-v", str(self.root / "scripts/weather43/cache_service.py") + ":/task/cache_service.py:ro",
                  "-v", str(self.root / "private/cities.json") + ":/task/cities.json:ro",
                  "-v", str(data) + ":/data", "--entrypoint", "python", self.args.runtime,
                  "/task/cache_service.py", "--cities", "/task/cities.json", "--data", "/data", "--source",
                  "http://" + self.gateway + ":18085", "--engine", engine, "--prewarm", str(prewarm),
                  "--ttl", str(ttl),
                  "--engine-timeout", "30", "--source-timeout", "15" if short_timeout else "30"])
        return self.ready(18087)

    def remove_fixture(self, name):
        self.cmd(["docker", "stop", "-t", "15", name])
        self.cmd(["docker", "rm", name])

    def stored_fingerprints(self, folder):
        with sqlite3.connect(str(self.root / "private" / folder / "results.sqlite3")) as database:
            values = list(database.execute("SELECT city,kind,payload FROM results ORDER BY city,kind"))
        fields = {city + "/" + kind: {name: json.loads(payload)[name]
                                       for name in ("hourly", "units", "fetched_at", "payload_sha256")}
                  for city, kind, payload in values}
        deadlines = [json.loads(payload)["next_check_at"] for _, _, payload in values]
        return {"entries": len(values), "data_and_fetch_time_sha256": hashlib.sha256(
                    json.dumps(fields, sort_keys=True).encode()).hexdigest(),
                "earliest_check_at": min(deadlines), "latest_check_at": max(deadlines)}

    def run(self):
        assert self.get()["data"]["cache_entries"] == 700
        assert self.environment()["data"]["complete"]
        self.record("before", {"health": self.get(), "response": self.environment()})
        try:
            self.start_fixture("concentrated-ttl-results", copy_all=True, ttl=10)
            started = time.monotonic()
            while self.get(port=18087)["data"]["pending"] and time.monotonic() - started < 60:
                time.sleep(.1)
            before = self.stored_fingerprints("concentrated-ttl-results")
            health_before = self.get(port=18087)
            # Real check deadlines expire; no wall clock or model timestamp is rewritten.
            time.sleep(max(0, before["earliest_check_at"] - time.time() + .05))
            first = self.load("concentrated-ttl-350-cities350", port=18087)
            second = self.load("concentrated-ttl-1000-cities350", devices=1000, port=18087)
            after = self.stored_fingerprints("concentrated-ttl-results")
            self.record("concentrated-ttl-revalidation", {"test_ttl_s": 10, "production_candidate_ttl_s": 300,
                                                          "before": before, "after": after,
                                                          "health_before": health_before, "health_after": self.get(port=18087)})
            assert before["data_and_fetch_time_sha256"] == after["data_and_fetch_time_sha256"]
            assert first["fresh"] == 350 and second["fresh"] == 1000
            assert self.get(port=18087)["data"]["metrics"]["engine_calls"] == 0
            self.remove_fixture(self.fixture)
            # The restored single-city result is truly read from disk; source stalls
            # keep its two refresh jobs pending while all clients request that key.
            self.fault({"delay_seconds": 40})
            ready = self.start_fixture("coalescing-results", short_timeout=True)
            counts = self.load("expired-same-city-350", cities=1, port=18087)
            health = self.get(port=18087)
            self.record("coalescing", {"ready_s": ready, "counts": counts, "health": health,
                                       "response": self.environment(18087)})
            assert counts["stale"] == 350
            assert health["data"]["metrics"]["scheduled"] == 2
            assert health["data"]["metrics"]["engine_calls"] == 0
            assert self.load("expired-same-city-1000", devices=1000, cities=1, port=18087)["stale"] == 1000
            assert self.get(port=18087)["data"]["metrics"]["scheduled"] == 2
            time.sleep(16)
            self.record("slow-expired-result", {"health": self.get(port=18087), "response": self.environment(18087)})
            self.remove_fixture(self.fixture)

            self.fault({"offline": True})
            elapsed = self.wait_status("stale", "stale")
            self.record("hot-source-outage", {"observed_after_s": elapsed, "health": self.get(),
                                              "response": self.environment()})
            assert self.load("hot-outage-350-cities350")["stale"] == 350
            start = time.monotonic()
            self.cmd(self.compose + ["restart", "openmeteo", "result-cache"])
            ready = self.ready()
            self.record("outage-restart", {"restart_and_readiness_s": time.monotonic() - start,
                                           "readiness_poll_s": ready, "response": self.environment(), "health": self.get()})
            assert self.load("outage-restart-1000-cities350", devices=1000)["stale"] == 1000

            self.start_fixture("empty-outage-results")
            counts = self.load("empty-outage-1000-cities350", devices=1000, port=18087)
            self.record("empty-source-outage", {"counts": counts, "health": self.get(port=18087),
                                                "response": self.environment(18087)})
            assert counts["loading_or_unavailable"] == 1000
            assert self.environment(18087)["data"]["components"]["weather"]["values"] is None
            self.remove_fixture(self.fixture)

            self.fault({"delay_seconds": 40, "domains": ["ncep_gfs013"]})
            self.cmd(self.compose + ["restart", "result-cache"])
            self.ready()
            elapsed = self.wait_status("stale", "fresh", seconds=90)
            self.record("independent-weather-slow", {"observed_after_s": elapsed, "health": self.get(),
                                                     "response": self.environment()})
            assert self.load("weather-slow-350-cities350")["stale"] == 350
            time.sleep(32)
            self.record("slow-source-deadline", {"health": self.get(), "response": self.environment()})
            self.fault({})
            elapsed = self.wait_status("fresh", "fresh", seconds=240)
            self.record("real-source-recovery", {"observed_after_s": elapsed, "health": self.get(),
                                                 "response": self.environment()})

            # Complete cold result AND model cache for 1,000 clients / 350 cities.
            # Stop only our original engine so two 8GiB limits do not overlap.
            self.cmd(self.compose + ["stop", "openmeteo"])
            cold = self.root / "private/full-cold-1000-model"
            cold.mkdir(exist_ok=False)
            original = (self.root / "private/model-data").stat()
            os.chown(cold, original.st_uid, original.st_gid)
            self.record("full-cold-before", {"model_files": len(list(cold.rglob("*"))),
                                              "main_engine_stopped": True,
                                              "result_directory_does_not_yet_exist": not (self.root / "private/full-cold-1000-results").exists()})
            self.cmd(["docker", "run", "-d", "--name", self.cold_engine, "--network",
                      "xiaozhi-weather43-acceptance_default", "--network-alias", self.cold_engine,
                      "--read-only", "--memory", "8g", "--memory-swap", "8g", "--cpus", "2",
                      "--pids-limit", "256", "--ulimit", "nofile=65536:65536", "--tmpfs", "/tmp:size=128m",
                      "-e", "REMOTE_DATA_DIRECTORY=http://" + self.gateway + ":18085/data/", "-e", "CACHE_SIZE=8GB",
                      "-v", str(cold) + ":/app/data", self.args.engine_image])
            self.start_fixture("full-cold-1000-results", engine="http://" + self.cold_engine + ":8080")
            self.record("full-cold-ready", {"health": self.get(port=18087)})
            assert self.get(port=18087)["data"]["cache_entries"] == 0
            started = time.monotonic()
            counts = self.load("full-cold-1000-cities350", devices=1000, port=18087, data_dir=cold)
            points = []
            while time.monotonic() - started < 1800:
                point = snapshot(cold)
                point["docker_stats"] = asyncio.run(docker_stats())
                point["health"] = self.get(port=18087)
                points.append(point)
                if point["memory_kib"]["MemAvailable"] < 2 * 1024 * 1024:
                    self.record("resource-stop", {"resource_sample": point})
                    raise RuntimeError("available_memory_below_2GiB")
                if point["health"]["data"]["cache_entries"] == 700:
                    break
                time.sleep(10)
            self.record("full-cold-warmup", {"first_ack_counts": counts, "complete_after_s": time.monotonic() - started,
                                             "health": self.get(port=18087), "resources": points})
            assert self.get(port=18087)["data"]["cache_entries"] == 700
            assert self.load("after-full-cold-1000-cities350", devices=1000, port=18087)["fresh"] == 1000
            self.remove_fixture(self.fixture)
            self.remove_fixture(self.cold_engine)
            self.cmd(self.compose + ["start", "openmeteo"])
            self.wait_status("fresh", "fresh", seconds=240)
            self.start_fixture("prewarm-results", prewarm=35)
            started = time.monotonic()
            while time.monotonic() - started < 240:
                health = self.get(port=18087)
                if health["data"]["cache_entries"] == 70:
                    break
                time.sleep(1)
            self.record("background-prewarm-no-environment-requests", {"elapsed_s": time.monotonic() - started,
                                                                     "health": health})
            assert health["data"]["cache_entries"] == 70 and health["data"]["metrics"]["requests"] == 0
            assert self.environment(18087)["data"]["fresh"]
            self.remove_fixture(self.fixture)
            self.record("after", {"health": self.get(), "response": self.environment()})
            (self.root / "evidence/recovery-complete").write_text(utc() + "\n")
        finally:
            self.fault({})
            # Recovery actions remain limited to our own experimental containers.
            for name in (self.fixture, self.cold_engine):
                names = self.cmd(["docker", "ps", "-a", "--format", "{{.Names}}"]).splitlines()
                if name in names:
                    self.remove_fixture(name)
            self.cmd(self.compose + ["start", "openmeteo", "result-cache"])
            self.record("experiment-timeline", {"events": self.notes})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--engine-image", required=True)
    args = parser.parse_args()
    if os.name != "posix" or os.geteuid() != 0 or "Ubuntu" not in Path("/etc/os-release").read_text():
        parser.error("Run only as root on the designated Ubuntu test server")
    if not (args.root / "evidence/coexistence-complete").exists():
        parser.error("Finish the undisturbed coexistence baseline before faults")
    Experiment(args).run()


if __name__ == "__main__":
    main()
