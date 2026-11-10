#!/usr/bin/env python3
"""Recount raw HTTP outcomes and summarize sampled Ubuntu resource evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import statistics


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


def resources(samples, interface, disk_total):
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
            "container_sampled_peaks": peaks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--interface", required=True)
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
        timings = [row["end_to_end_s"] for row in rows]
        profiles.append({"name": str(path.parent.relative_to(args.evidence)),
                         "started_at": data["started_at"], "finished_at": data["finished_at"],
                         "scenario": data["settings"]["scenario"], "devices": data["settings"]["devices"],
                         "cities": data["settings"]["cities"], "elapsed_s": data["elapsed_s"],
                         "offered": len(rows), "counts": counts, "raw_counts_match_summary": valid,
                         "raw_sha256": hashlib.sha256(raw.read_bytes()).hexdigest(),
                         "latency_s": data["latency_s"], "latency_mean_s": statistics.mean(timings),
                         "resources": resources(data.get("resources", []), args.interface, total)})
    report = {"scope": "raw_evidence_recount", "profiles": profiles,
              "all_raw_counts_match_summaries": all(p["raw_counts_match_summary"] for p in profiles),
              "limitations": ["Docker peaks are sampled and can miss shorter spikes",
                              "Host CPU, disk and physical-NIC traffic include generator and other workloads",
                              "S3 relay payload counters, rather than host NIC totals, estimate weather source traffic",
                              "Loading acknowledgements are not fresh-data successes"]}
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"profiles": len(profiles), "raw_count_integrity_passed": report["all_raw_counts_match_summaries"]}))
    return 0 if report["all_raw_counts_match_summaries"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
