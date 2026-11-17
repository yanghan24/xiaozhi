#!/usr/bin/env python3
"""Bounded 50-way Ubuntu acceptance; preserve failures and continue independent checks.

Adopts the already completed enterprise-account baseline. Only this experiment's
weather containers are changed. Business API/voice image and start-time changes
invalidate comparisons. No automatic retries, purchases, or business deployment.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import traceback

from final_capacity import inspect, listen_counters
from load_local import docker_stats, snapshot
from metrics_report import docker_memory
from recovery_checks import Experiment


def utc():
    return datetime.now(timezone.utc).isoformat()


class Acceptance:
    def __init__(self, args):
        self.args = args
        self.root = args.root.resolve()
        self.out = self.root / "evidence" / args.label
        self.out.mkdir(exist_ok=False)
        self.voice_root = self.root / "evidence/voice/account2"
        self.initial = inspect()
        self.business = {k: v for k, v in self.initial.items() if k in
                         ("xiaozhi-pr26-api", "xiaozhi-pr26-voice")}
        self.errors = []
        self.phases = []
        self.exp = Experiment(argparse.Namespace(root=self.root, label=args.label + "-cold",
                              runtime=args.runtime, engine_image=args.engine_image))
        self.children = []
        self.faults = self.root / "private/fault.json"

    def write(self, name, value):
        path = self.out / (name + ".json")
        with path.open("x") as output:
            json.dump({"at": utc(), **value}, output, indent=2)

    def check_business(self):
        current = inspect()
        business = {k: current[k] for k in self.business}
        if business != self.business:
            self.write("business-version-change-" + str(len(self.phases)),
                       {"before": self.business, "after": business})
            raise RuntimeError("business_deployment_changed_comparison_invalid")
        assert all(v["running"] and not v["oom_killed"] for v in business.values())
        return current

    def authority(self, phase):
        fixture = json.loads((self.root / "private/voice-test-device-account2.json").read_text())
        key = Path("/home/jt/.config/xiaozhi-weather43/enterprise-model-key").read_text().strip()
        code = (self.root / "private/account2_authority_context.py").read_text()
        result = subprocess.run(["docker", "exec", "-i", "xiaozhi-pr26-voice", "python", "-c", code],
                                input=json.dumps({"fixture": fixture, "supplied_key": key}),
                                capture_output=True, text=True, timeout=60)
        # Unfiltered product diagnostics may contain secrets; keep them private.
        for suffix, body in (("stdout", result.stdout), ("stderr", result.stderr)):
            path = self.root / "private" / (self.args.label + "-" + phase + "." + suffix)
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "w") as output:
                output.write(body)
        assert result.returncode == 0, "authority_probe_failed"
        lines = [line.split(" ", 1)[1] for line in result.stdout.splitlines()
                 if line.startswith("WEATHER43_ACCOUNT2_SCOPE ")]
        assert len(lines) == 1
        value = json.loads(lines[0])
        initial = json.loads((self.root / "evidence/voice/account2-authority-initial.json").read_text())
        for field in ("device_id_sha256", "account_subject_sha256", "authorization_generation_sha256",
                      "product_voice_source_sha256", "model", "model_base"):
            assert value[field] == initial[field], "voice_authority_scope_changed_" + field
        assert value["use_permission"] == "allowed"
        self.write("authority-" + phase, value)

    def start(self, command, label):
        log = (self.root / "private" / (self.args.label + "-" + label + ".log")).open("xb")
        process = subprocess.Popen(command, cwd=self.root, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        log.close()
        self.children.append(process)
        return process

    def load(self, label, scenario="burst", cities=50, port=18086, data=None, city_file=None):
        return self.start(["python3", "scripts/weather43/acceptance_load.py", "--cities-file",
                           str(city_file or self.root / "private/cities.json"), "--cities", str(cities),
                           "--devices", "50", "--scenario", scenario, "--duration", "1800",
                           "--period", "5" if scenario == "waves" else "300", "--timeout", "2",
                           "--port", str(port), "--data-dir", str(data or self.root / "private/model-data"),
                           "--output", str(self.out / label)], label)

    def load_summary(self, label, process, cold=False):
        assert process.wait(timeout=1950) == 0, "load_process_failed_" + label
        value = json.loads((self.out / label / "summary.json").read_text())
        counts = value["counts"]
        assert counts["failure"] == counts["timeout"] == 0, "weather_http_failed"
        assert value["peak_active_requests"] <= 50
        if not cold:
            assert counts["loading_or_unavailable"] == 0
        if value["settings"]["scenario"] == "waves":
            assert value["offered"] == 18000 and value["elapsed_s"] >= 1800
        assert all(x["memory_kib"]["MemAvailable"] >= 2 * 1024 * 1024
                   for x in value["resources"]), "sampled_host_memory_below_2GiB"
        self.phases.append({"phase": label, "counts": counts, "offered": value["offered"],
                            "latency_s": value["latency_s"], "peak_active_requests": value["peak_active_requests"]})
        return value

    def voice(self, phase):
        self.check_business()
        self.authority(phase + "-before")
        process = self.start(["docker", "run", "--rm", "--user", "0:0", "--name", "weather43-voice-probe",
                              "--network", "host", "--read-only", "--memory", "384m", "--cpus", "1",
                              "--cap-drop", "ALL", "--security-opt", "no-new-privileges", "--tmpfs", "/tmp:size=64m",
                              "--entrypoint", "python", "-v", str(self.root / "scripts/weather43/voice_coexist.py") + ":/probe.py:ro",
                              "-v", str(self.root / "private/voice-test-device-account2.json") + ":/private/device.json:ro",
                              "-v", self.args.audio + ":/private/input.wav:ro",
                              "-v", str(self.voice_root) + ":/evidence:rw", self.args.runtime,
                              "/probe.py", "--device", "/private/device.json", "--audio", "/private/input.wav",
                              "--connect-host", "192.168.168.114", "--output", "/evidence/" + phase,
                              "--phase", "account2-" + phase, "--count", "10", "--interval", "5", "--stop-on-failure"], phase)
        assert process.wait(timeout=1400) == 0, "voice_process_failed"
        result = json.loads((self.voice_root / phase / "summary.json").read_text())
        self.authority(phase + "-after")
        self.check_business()
        self.phases.append({"phase": phase, "voice_summary": result})
        # Preserve a failed voice phase, then continue weather-only independent checks.
        if result["failure"] or result["count"] != 10:
            self.errors.append({"phase": phase, "error": "real_voice_failed", "summary": result})
        return result

    def cold(self):
        exp = self.exp
        cities = json.loads((self.root / "private/cities.json").read_text())
        cities["cities"] = cities["cities"][:50]
        city_file = self.root / "private" / (self.args.label + "-cities50.json")
        with city_file.open("x") as output:
            json.dump(cities, output)
        exp.cmd(exp.compose + ["stop", "openmeteo"])
        cold = exp.private_directory("model50")
        cold.mkdir(exist_ok=False)
        original = (self.root / "private/model-data").stat()
        os.chown(cold, original.st_uid, original.st_gid)
        self.write("cold-before", {"model_files": len(list(cold.rglob("*"))),
                                   "cities": 50, "result_directory_exists": exp.private_directory("results50").exists(),
                                   "primary_engine_stopped": True})
        try:
            exp.cmd(["docker", "run", "-d", "--name", exp.cold_engine, "--network",
                     "xiaozhi-weather43-acceptance_default", "--network-alias", exp.cold_engine,
                     "--read-only", "--memory", "8g", "--memory-swap", "8g", "--cpus", "2",
                     "--pids-limit", "256", "--ulimit", "nofile=65536:65536", "--tmpfs", "/tmp:size=128m",
                     "-e", "REMOTE_DATA_DIRECTORY=http://" + exp.gateway + ":18085/data/", "-e", "CACHE_SIZE=8GB",
                     "-v", str(cold) + ":/app/data", self.args.engine_image])
            exp.start_fixture("results50", engine="http://" + exp.cold_engine + ":8080",
                              cities_file=str(city_file.relative_to(self.root)))
            assert exp.get(port=18087)["data"]["cache_entries"] == 0
            started = time.monotonic()
            first = self.load("cold-50-first-ack", port=18087, data=cold, city_file=city_file)
            self.load_summary("cold-50-first-ack", first, cold=True)
            load = self.load("cold-50-waves-30min", "waves", port=18087, data=cold, city_file=city_file)
            # Voice begins during actual cold fetch, without artificial source delays.
            voice = self.start(["python3", str(Path(__file__)), "--voice-child", "during-cold-50",
                                "--root", str(self.root), "--label", self.args.label,
                                "--runtime", self.args.runtime, "--engine-image", self.args.engine_image,
                                "--audio", self.args.audio], "cold-voice-supervisor")
            points = []
            warm_at = None
            while time.monotonic() - started < 600:
                point = snapshot(cold)
                point["docker_stats"] = asyncio.run(docker_stats())
                point["health"] = exp.get(port=18087)
                points.append(point)
                assert point["memory_kib"]["MemAvailable"] >= 2 * 1024 * 1024, "host_memory_below_2GiB"
                if point["health"]["data"]["cache_entries"] == 100:
                    warm_at = utc()
                    break
                time.sleep(5)
            self.write("cold-warmup", {"warm_at": warm_at, "complete_after_s": time.monotonic() - started,
                                       "points": points, "health": exp.get(port=18087),
                                       "cgroup": docker_memory([exp.cold_engine, exp.fixture])})
            assert voice.wait(timeout=1400) == 0, "cold_voice_supervisor_failed"
            cold_voice = json.loads((self.out / "cold-voice-result.json").read_text())
            self.errors.extend(cold_voice["errors"])
            self.phases.append({"phase": "during-cold-50", "voice_summary": cold_voice["voice_summary"]})
            self.load_summary("cold-50-waves-30min", load, cold=True)
            assert warm_at is not None, "cold_fill_exceeded_600_seconds"
            final = self.load("cold-50-warm-final", port=18087, data=cold, city_file=city_file)
            self.load_summary("cold-50-warm-final", final)
            self.write("cold-data-paths", {"model_directory": str(cold.relative_to(self.root)),
                                          "result_directory": str(exp.private_directory("results50").relative_to(self.root)),
                                          "city_fixture": str(city_file.relative_to(self.root))})
        finally:
            for name in (exp.fixture, exp.cold_engine):
                if name in exp.cmd(["docker", "ps", "-a", "--format", "{{.Names}}"]).splitlines():
                    exp.remove_fixture(name)
            exp.cmd(exp.compose + ["start", "openmeteo"])
            exp.wait_status("fresh", "fresh", seconds=360)

    def run(self):
        baseline = json.loads((self.voice_root / "before-weather-load/summary.json").read_text())
        assert baseline["count"] == baseline["success"] == 10 and baseline["failure"] == 0
        assert json.loads(self.faults.read_text()) == {}
        assert all(value["running"] and not value["oom_killed"] for value in self.initial.values())
        self.write("manifest", {"user_target": "50 concurrent environment requests; previous 350/1000 are history",
                                "adopted_baseline": str((self.voice_root / "before-weather-load").relative_to(self.root)),
                                "warm_load": "50 synchronized requests every 5 seconds for 1800 seconds, 50 places",
                                "cold_load": "same 50-way schedule, empty result and model caches, 50 places",
                                "voice": "10 serial real questions per phase, first failure stops phase, no automatic retry",
                                "comparison": "before/during-warm/after/during-cold; report actual cold-overlap count",
                                "proposed_voice_p95_degradation_limit": 0.10,
                                "failures_allowed": True, "capacity_attempt_limit": 1,
                                "initial_containers": self.initial, "listen_counters": listen_counters(),
                                "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                "api_voice_services_will_not_be_restarted": True})
        self.authority("matrix-start")
        for cities in (1, 10, 50):
            for number in range(1, 4):
                label = "burst50-cities%d-round%d" % (cities, number)
                self.load_summary(label, self.load(label, cities=cities))
        load = self.load("warm50-waves-30min", "waves")
        time.sleep(60)
        self.voice("during-warm-50")
        self.load_summary("warm50-waves-30min", load)
        self.check_business()
        self.voice("after-weather-load-50")
        self.cold()
        self.check_business()
        self.write("finished", {"phases": self.phases, "errors": self.errors, "containers": inspect(),
                                "listen_counters": listen_counters(), "weather_capacity_passed": True,
                                "voice_verification_completed": not self.errors})
        (self.root / "evidence/latest-final-capacity.json").write_text(json.dumps(
            {"evidence_directory": self.args.label, "devices": 50, "historical_scales_retained": [350, 1000]}, indent=2))
        (self.root / "evidence/final-capacity-complete").write_text(utc() + "\n")
        (self.root / "evidence/coexistence-finished").write_text(utc() + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--runtime", required=True)
    p.add_argument("--engine-image", required=True)
    p.add_argument("--audio", required=True)
    p.add_argument("--label", default="acceptance50-r1")
    p.add_argument("--voice-child")
    args = p.parse_args()
    assert re.fullmatch(r"[a-z0-9-]{1,30}", args.label)
    assert os.name == "posix" and os.geteuid() == 0 and "Ubuntu" in Path("/etc/os-release").read_text()
    if args.voice_child:
        # Reuse the parent's immutable baseline without creating evidence directories.
        obj = object.__new__(Acceptance)
        obj.args, obj.root = args, args.root.resolve()
        obj.out = obj.root / "evidence" / args.label
        obj.voice_root = obj.root / "evidence/voice/account2"
        obj.initial = json.loads((obj.out / "manifest.json").read_text())["initial_containers"]
        obj.business = {k: obj.initial[k] for k in ("xiaozhi-pr26-api", "xiaozhi-pr26-voice")}
        obj.errors, obj.phases, obj.children = [], [], []
        result = obj.voice(args.voice_child)
        obj.write("cold-voice-result", {"voice_summary": result, "errors": obj.errors})
        return
    obj = Acceptance(args)
    try:
        obj.run()
    except Exception as error:
        obj.write("failed", {"error": type(error).__name__, "reason": str(error)[:160],
                             "frames": [{"file": Path(f.filename).name, "line": f.lineno, "function": f.name}
                                        for f in traceback.extract_tb(error.__traceback__)[-6:]],
                             "phases": obj.phases, "errors": obj.errors, "automatic_retry": False})
        raise SystemExit(1)
    finally:
        # Every child was started in its own process group by this controller.
        # Do not signal existing business or observer processes.
        for process in obj.children:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
        if "weather43-voice-probe" in subprocess.check_output(
                ["docker", "ps", "--format", "{{.Names}}"], text=True).splitlines():
            subprocess.run(["docker", "stop", "-t", "10", "weather43-voice-probe"],
                           capture_output=True, timeout=30)


if __name__ == "__main__":
    main()
