#!/usr/bin/env python3
"""Read-only checks before starting this project's management environment."""
from pathlib import Path
import os
import shutil
import socket
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
try:
    subprocess.run(["docker", "info", "--format", "Docker {{.ServerVersion}} / {{.Architecture}}"], check=True, timeout=15)
    subprocess.run(["docker", "compose", "version"], check=True, timeout=10)
    subprocess.run(["docker", "compose", "config", "--quiet"], cwd=root, check=True, timeout=10)
    settings = dict(line.split("=", 1) for line in (root / ".env").read_text().splitlines() if line and not line.startswith("#"))
    port = int(os.environ.get("MANAGER_PORT", settings.get("MANAGER_PORT", "18002")))
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", port))
    disk = shutil.disk_usage(root).free / 1024**3
    print(f"Port {port} is available; free disk: {disk:.1f} GiB")
    if disk < 8:
        raise ValueError("At least 8 GiB of free disk is required for the initial builds and test data.")
    if Path("/proc/meminfo").exists():
        memory = dict(line.split(":", 1) for line in Path("/proc/meminfo").read_text().splitlines())
        available = int(memory["MemAvailable"].split()[0]) / 1024
        print(f"Available memory: {available:.0f} MiB; build web and API sequentially.")
        if available < 1800:
            raise ValueError("Less than 1800 MiB available; free resources before building or starting services.")
    print("PASS: prerequisites, configuration, local port and resources")
except (OSError, ValueError, subprocess.SubprocessError) as error:
    print(f"FAIL: {error}", file=sys.stderr)
    sys.exit(1)
