#!/usr/bin/env python3
"""Exercise real app startup failures and recovery in a disposable container."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

from voice_api import private_json

APP = Path("/app")
CONFIG = APP / "data/.config.yaml"
EVIDENCE = Path("/evidence")


def start_app(log):
    return subprocess.Popen([sys.executable, "app.py"], cwd=APP, stdout=log, stderr=subprocess.STDOUT)


def stop_app(process):
    if process.poll() is None:
        process.terminate()
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def failed_start(name, config, expected, report):
    if config is not None:
        private_json(CONFIG, config)
    log_path = EVIDENCE / (name + ".log")
    with os.fdopen(os.open(log_path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o600), "w") as log:
        process = start_app(log)
        try:
            exit_code = process.wait(timeout=30)
        finally:
            stop_app(process)
    if exit_code == 0 or expected not in log_path.read_text():
        raise ValueError(f"{name}: expected a nonzero exit and {expected}; inspect the private startup log")
    report["checks"].append({"name": name, "status": "passed", "command": "python app.py", "exit_code": exit_code, "diagnostic": expected})
    print("PASS: " + name)
    CONFIG.unlink(missing_ok=True)


def recovered_start(config, report):
    private_json(CONFIG, config)
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    log_path = EVIDENCE / "recovery.log"
    with os.fdopen(os.open(log_path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o600), "w") as log:
        process = start_app(log)
        try:
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise ValueError("Recovery exited before becoming healthy; inspect the private recovery log")
                try:
                    with client.open("http://127.0.0.1:8000/xiaozhi/v1/", timeout=1) as response:
                        if response.status == 200 and response.read().decode().strip() == "Server is running":
                            report["checks"].append({"name": "corrected configuration starts the real service", "status": "passed",
                                                     "http": 200, "body": "Server is running"})
                            print("PASS: corrected configuration starts the real service")
                            return
                except OSError:
                    pass
                time.sleep(0.2)
            raise ValueError("Recovery did not become healthy within 45 seconds")
        finally:
            stop_app(process)
            CONFIG.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="/baseline/.config.yaml", help="Read-only known-good private configuration")
    args = parser.parse_args()
    # Refuse the normal running service's mounted data directory.
    if CONFIG.exists() or not (APP / "app.py").exists():
        parser.error("Run the voice-config-check service with its empty, disposable /app/data tmpfs")
    EVIDENCE.mkdir(mode=0o700, parents=True, exist_ok=True)
    report = {"started_at": datetime.now(timezone.utc).isoformat(), "status": "failed", "scope": "startup_configuration", "checks": []}
    try:
        known_good = json.loads(Path(args.config).read_text())
        failed_start("missing configuration file is diagnosed", None, "找不到data/.config.yaml文件", report)
        missing_secret = json.loads(json.dumps(known_good))
        missing_secret["manager-api"].pop("secret", None)
        failed_start("missing manager secret is diagnosed", missing_secret, "manager-api的url或secret配置错误", report)
        wrong_secret = json.loads(json.dumps(known_good))
        wrong_secret["manager-api"]["secret"] = "protocol-check-invalid-secret"
        failed_start("wrong manager secret prevents startup", wrong_secret, "无效的服务器密钥", report)
        recovered_start(known_good, report)
        report["status"] = "passed"
    except Exception as error:
        report["error"] = str(error)
        print("FAIL: " + str(error), file=sys.stderr)
    finally:
        CONFIG.unlink(missing_ok=True)
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        private_json(EVIDENCE / "result.json", report)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
