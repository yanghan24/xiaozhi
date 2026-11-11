#!/usr/bin/env python3
"""Verify task-owned Ubuntu egress survives its own service restart."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def command(args):
    return subprocess.run(args, capture_output=True, text=True, timeout=180, check=True).stdout.strip()


def state():
    props = command(["systemctl", "show", "xiaozhi-weather43-egress", "--property=MainPID,ActiveState,SubState,UnitFileState,MemoryCurrent,MemoryPeak,CPUUsageNSec,NRestarts"])
    values = dict(line.split("=", 1) for line in props.splitlines() if "=" in line)
    sockets = command(["ss", "-lntp"])
    selected = [line for line in sockets.splitlines() if ":7890 " in line or ":17890 " in line]
    return {"at": datetime.now(timezone.utc).isoformat(), "systemd": values,
            "selected_listening_sockets": selected,
            "temporary_reverse_ssh_port_17890_listening": any(":17890 " in line for line in selected)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "posix" or os.geteuid() != 0 or "Ubuntu" not in Path("/etc/os-release").read_text():
        parser.error("Run on the authorized Ubuntu host")
    if not (args.root / "evidence/recovery-complete").exists():
        parser.error("Do not interrupt baseline weather loads")
    output = args.root / "evidence/independent-network"
    output.mkdir(exist_ok=False)
    before = state()
    assert not before["temporary_reverse_ssh_port_17890_listening"]
    started = time.monotonic()
    command(["systemctl", "restart", "xiaozhi-weather43-egress"])
    time.sleep(2)
    after = state()
    command(["python3", str(args.root / "scripts/weather43/probe_sources.py"), "--proxy", "http://127.0.0.1:7890",
             "--output", str(output / "sources-after-restart.json")])
    proof = {"before": before, "after": after, "restart_and_probe_s": time.monotonic() - started,
             "mihomo_binary_sha256": hashlib.sha256(Path("/home/jt/.local/libexec/weather43/mihomo").read_bytes()).hexdigest(),
             "service_unit_sha256": hashlib.sha256(Path("/etc/systemd/system/xiaozhi-weather43-egress.service").read_bytes()).hexdigest(),
             "proxy_route": "Ubuntu systemd-owned local listener; no SSH data forwarding",
             "scope": "service restart, not an Ubuntu host reboot"}
    assert before["systemd"]["MainPID"] != after["systemd"]["MainPID"]
    assert after["systemd"]["ActiveState"] == "active"
    assert not after["temporary_reverse_ssh_port_17890_listening"]
    (output / "restart-proof.json").write_text(json.dumps(proof, indent=2) + "\n")
    (args.root / "evidence/network-restart-complete").write_text(after["at"] + "\n")
    print(json.dumps({"independent_proxy_restart_and_real_sources": "passed", "elapsed_s": proof["restart_and_probe_s"]}))


if __name__ == "__main__":
    main()
