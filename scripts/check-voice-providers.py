#!/usr/bin/env python3
"""Verify a real provider configuration failure and recovery with a private fixture."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import uuid

from voice_api import api, private_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", default="/app/data/check.json")
    parser.add_argument("--output", default="/app/data/provider-result.json")
    args = parser.parse_args()
    settings = json.loads(Path(args.settings).read_text())
    base, token = settings["manager_url"], settings["admin_token"]
    directory = Path(args.output).parent
    fixture_settings = directory / ("provider-settings-" + uuid.uuid4().hex + ".json")
    report = {"scope": "provider_configuration", "status": "failed", "started_at": datetime.now(timezone.utc).isoformat()}
    model_id = None
    try:
        template = api(base, "/models/TTS_EdgeTTS", token=token)
        body = {"modelCode": "EdgeTTS", "modelName": "protocol-provider-" + uuid.uuid4().hex,
                "isEnabled": 1, "isDefault": 0, "sort": 999,
                "configJson": {**template["configJson"], "type": "unsupported-protocol-provider"}}
        model = api(base, "/models/TTS/edge", "POST", body, token)
        model_id = model["id"]
        report["model_id"] = model_id
        private_json(fixture_settings, {**settings, "tts_model": model_id})
        for name, expected_exit, expected_state in (("rejected", 1, "failed"), ("recovered", 0, "ready")):
            if name == "recovered":
                api(base, "/models/TTS/edge/" + model_id, "PUT", {**body, "configJson": template["configJson"]}, token)
            result_path = directory / ("provider-" + name + ".json")
            result = subprocess.run([sys.executable, str(Path(__file__).with_name("check-voice.py")),
                                     "--settings", str(fixture_settings), "--output", str(result_path)], timeout=60)
            observation = json.loads(result_path.read_text())
            report[name] = observation
            if result.returncode != expected_exit or observation.get("provider_initialization", {}).get("status") != expected_state:
                raise ValueError(f"Provider {name} check did not return exit {expected_exit} and initialization {expected_state}")
            print("PASS: real provider configuration " + name)
        report["status"] = "passed"
    except Exception as error:
        report["error"] = str(error)
        print("FAIL: " + str(error), file=sys.stderr)
    finally:
        try:
            if model_id:
                api(base, "/models/" + model_id, "DELETE", token=token)
                if api(base, "/models/" + model_id, token=token) is not None:
                    raise ValueError("Disposable provider model was not removed")
                report["model_removed"] = True
        except Exception as error:
            report["status"] = "failed"
            report["cleanup_error"] = str(error)
            print("FAIL: provider cleanup: " + str(error), file=sys.stderr)
        fixture_settings.unlink(missing_ok=True)
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        private_json(args.output, report)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
