#!/usr/bin/env python3
"""Recount raw HTTP outcomes and summarize sampled Ubuntu resource evidence."""
import argparse
import hashlib
import http.client
import json
import math
from pathlib import Path
import re
import shutil
import socket
import statistics


class DockerSocket(http.client.HTTPConnection):
    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect("/var/run/docker.sock")


def docker_memory(names):
    """Unadjusted cgroup memory, since CLI memory subtracts inactive file cache."""
    rows = {}
    for name in names:
        connection = DockerSocket("localhost", timeout=5)
        try:
            connection.request("GET", "/containers/" + name + "/stats?stream=false&one-shot=true")
            response = connection.getresponse()
            if response.status != 200:
                raise ValueError("docker_stats_status")
            data = json.loads(response.read(1048576))["memory_stats"]
            rows[name] = {key: data.get(key) for key in ("usage", "max_usage", "limit", "failcnt")}
            rows[name]["stats"] = {key: data.get("stats", {}).get(key) for key in
                                   ("cache", "total_cache", "total_inactive_file", "inactive_file")}
        except Exception as error:
            rows[name] = {"error": type(error).__name__}
        finally:
            connection.close()
    return rows


def size(value):
    match = re.fullmatch(r"\s*([0-9.]+)\s*([A-Za-z]+)\s*", value)
    if not match:
        return None
    units = {"b": 1, "kb": 1000, "mb": 1000 ** 2, "gb": 1000 ** 3,
             "kib": 1024, "mib": 1024 ** 2, "gib": 1024 ** 3}
    factor = units.get(match[2].lower())
    return float(match[1]) * factor if factor else None


def traffic(sample, interface):
    for line in sample["network_counters"]:
        name, values = line.split(":", 1)
        if name.strip() == interface:
            fields = values.split()
            return int(fields[0]), int(fields[8])
    return None


def disk_values(sample, device):
    for line in sample["disk_counters"]:
        fields = line.split()
        if fields[2] == device and len(fields) >= 14:
            return {"reads": int(fields[3]), "read_bytes": int(fields[5]) * 512,
                    "writes": int(fields[7]), "written_bytes": int(fields[9]) * 512,
                    "in_progress": int(fields[11])}
    return None


def disk_resources(samples, device):
    # Kernel diskstats counts sectors as 512 bytes, independent of filesystem blocks:
    # https://docs.kernel.org/block/stat.html
    if not device:
        return {"scope": "not_requested"}
    values = [disk_values(sample, device) for sample in samples]
    if len(values) < 2 or any(value is None for value in values):
        return {"device": device, "error": "missing_disk_samples"}
    intervals = []
    for index in range(1, len(samples)):
        elapsed = samples[index]["monotonic_s"] - samples[index - 1]["monotonic_s"]
        delta = {key: values[index][key] - values[index - 1][key]
                 for key in ("reads", "writes", "read_bytes", "written_bytes")}
        if elapsed <= 0 or any(value < 0 for value in delta.values()):
            return {"device": device, "error": "counter_reset_or_invalid_sample_interval"}
        intervals.append({"iops": (delta["reads"] + delta["writes"]) / elapsed,
                          "read_bytes_per_s": delta["read_bytes"] / elapsed,
                          "written_bytes_per_s": delta["written_bytes"] / elapsed})
    elapsed = samples[-1]["monotonic_s"] - samples[0]["monotonic_s"]
    delta = {key: values[-1][key] - values[0][key]
             for key in ("reads", "writes", "read_bytes", "written_bytes")}
    return {"device": device, "sample_window_s": elapsed, **delta,
            "mean_iops": (delta["reads"] + delta["writes"]) / elapsed,
            "sampled_interval_iops_max": max(value["iops"] for value in intervals),
            "sampled_interval_read_bytes_per_s_max": max(value["read_bytes_per_s"] for value in intervals),
            "sampled_interval_written_bytes_per_s_max": max(value["written_bytes_per_s"] for value in intervals),
            "sampled_in_progress_max": max(value["in_progress"] for value in values),
            "scope": "Shared host block-device activity; not an isolated storage benchmark"}


def resources(samples, interface, disk_total, disk_device=None):
    if not samples:
        return {"error": "no_samples"}
    containers, stats_errors = {}, 0
    for point in samples:
        stats = point.get("docker_stats")
        if not isinstance(stats, list):
            stats_errors += 1
            continue
        for item in stats:
            name = item.get("Name") or item.get("Container")
            data = containers.setdefault(name, {"cpu_percent": [], "memory_bytes": []})
            try:
                data["cpu_percent"].append(float(item["CPUPerc"].strip("%")))
                usage = size(item["MemUsage"].split("/")[0].strip())
                if usage is not None:
                    data["memory_bytes"].append(usage)
            except (KeyError, ValueError):
                stats_errors += 1
    peaks = {name: {"sampled_cpu_percent_max": max(value["cpu_percent"]) if value["cpu_percent"] else None,
                    "sampled_memory_bytes_max": max(value["memory_bytes"]) if value["memory_bytes"] else None}
             for name, value in containers.items()}
    cpu_a = list(map(int, samples[0]["cpu_ticks"].split()[1:9]))
    cpu_b = list(map(int, samples[-1]["cpu_ticks"].split()[1:9]))
    delta = [b - a for a, b in zip(cpu_a, cpu_b)]
    host_busy = 100 * (sum(delta) - delta[3] - delta[4]) / sum(delta) if sum(delta) else None
    a, b = traffic(samples[0], interface), traffic(samples[-1], interface)
    return {"sample_count": len(samples), "docker_stats_errors": stats_errors,
            "host_cpu_busy_percent_over_sample_window": host_busy,
            "host_available_memory_kib_min": min(p["memory_kib"]["MemAvailable"] for p in samples),
            "host_disk_free_bytes_min": min(p["disk_free_bytes"] for p in samples),
            "host_disk_free_fraction_min": min(p["disk_free_bytes"] for p in samples) / disk_total,
            "interface": interface,
            "shared_host_network_received_bytes": b[0] - a[0] if a and b else None,
            "shared_host_network_sent_bytes": b[1] - a[1] if a and b else None,
            "shared_host_disk": disk_resources(samples, disk_device),
            "cache_logical_file_bytes_max": max((p.get("cache_file_bytes", 0) for p in samples)),
            "cache_allocated_bytes_max": max((p.get("cache_allocated_bytes", 0) for p in samples)),
            "container_sampled_peaks": peaks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--interface", required=True)
    parser.add_argument("--disk-device", help="Whole block-device name, verified from the actual root filesystem")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new evidence output")
    profiles = []
    total = shutil.disk_usage(args.evidence).total
    for path in sorted(args.evidence.rglob("summary.json")):
        data = json.loads(path.read_text())
        if data.get("scope") != "unified_environment_feasibility_gateway":
            continue
        raw = path.parent / "requests.jsonl"
        rows = [json.loads(line) for line in raw.read_text().splitlines()]
        counts = {name: sum(row["outcome"] == name for row in rows) for name in data["counts"]}
        valid = counts == data["counts"] and len(rows) == data["offered"] and sum(counts.values()) == len(rows)
        timings = sorted(row["end_to_end_s"] for row in rows)
        latency = {key: timings[max(0, math.ceil(len(timings) * fraction) - 1)]
                   for key, fraction in (("p50", .5), ("p95", .95), ("p99", .99), ("max", 1))}
        latency_valid = all(math.isclose(value, data["latency_s"][key], rel_tol=1e-12, abs_tol=1e-12)
                            for key, value in latency.items())
        profiles.append({"name": str(path.parent.relative_to(args.evidence)),
                         "started_at": data["started_at"], "finished_at": data["finished_at"],
                         "scenario": data["settings"]["scenario"], "devices": data["settings"]["devices"],
                         "cities": data["settings"]["cities"], "elapsed_s": data["elapsed_s"],
                         "offered": len(rows), "counts": counts, "raw_counts_match_summary": valid,
                         "raw_sha256": hashlib.sha256(raw.read_bytes()).hexdigest(),
                         "raw_latency_matches_summary": latency_valid,
                         "latency_s": latency, "latency_mean_s": statistics.mean(timings),
                         "resources": resources(data.get("resources", []), args.interface, total, args.disk_device)})
    report = {"scope": "raw_evidence_recount", "profiles": profiles,
              "all_raw_counts_match_summaries": all(p["raw_counts_match_summary"] for p in profiles),
              "all_raw_latencies_match_summaries": all(p["raw_latency_matches_summary"] for p in profiles),
              "limitations": ["Docker peaks are sampled and can miss shorter spikes",
                              "Host CPU, disk and physical-NIC traffic include generator and other workloads",
                              "S3 relay payload counters, rather than host NIC totals, estimate weather source traffic",
                              "Loading acknowledgements are not fresh-data successes"]}
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    passed = report["all_raw_counts_match_summaries"] and report["all_raw_latencies_match_summaries"]
    print(json.dumps({"profiles": len(profiles), "raw_count_and_latency_integrity_passed": passed}))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
