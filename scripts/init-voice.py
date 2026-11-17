#!/usr/bin/env python3
"""Prepare private voice configuration against an existing local test manager."""
import argparse
import json
from pathlib import Path
import sys
from urllib.parse import urlparse

from voice_api import api, private_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:18002/xiaozhi")
    parser.add_argument("--admin-token", default=".runtime/voice/admin-token.json")
    parser.add_argument("--asr-model", default="ASR_OpenaiASR")
    parser.add_argument("--ws-url", default="ws://voice:8000/xiaozhi/v1/")
    parser.add_argument("--ota-url", default="http://api:8002/xiaozhi/ota/")
    parser.add_argument("--container-manager-url", default="http://api:8002/xiaozhi")
    parser.add_argument("--configure", action="store_true", help="Set the local test manager's remote ASR and enable device authentication")
    parser.add_argument("--publish-urls", action="store_true", help="Publish device URLs after the voice service is healthy (the manager validates connectivity)")
    args = parser.parse_args()
    if urlparse(args.url).hostname not in ("127.0.0.1", "localhost", "::1"):
        parser.error("This initializer is for a local test manager; --url must use a loopback address")
    root = Path(__file__).resolve().parent.parent
    output = root / ".runtime/voice"
    try:
        token = json.loads(Path(args.admin_token).read_text())["token"]
        params = api(args.url, "/admin/params/page?page=1&limit=1000", token=token)["list"]
        by_code = {item["paramCode"]: item for item in params}
        secret = by_code["server.secret"]["paramValue"]
        if not secret or "你" in secret:
            raise ValueError("The real manager server.secret is missing")
        config = api(args.url, "/config/server-base", "POST", {}, secret)
        backup = output / "manager-settings-before.json"
        if (args.configure or args.publish_urls) and not backup.exists():
            private_json(backup, {
                "manager_url": args.url,
                "asr_model": config["selected_module"]["ASR"],
                "parameters": [by_code[key] for key in ("server.websocket", "server.ota", "server.auth.enabled")],
            }, exclusive=True)
        if args.configure:
            model = api(args.url, "/models/" + args.asr_model, token=token)
            model_config = model["configJson"]
            if model_config.get("type") not in ("openai", "fun_server"):
                raise ValueError("This image supports remote ASR types openai and fun_server")
            api(args.url, "/models/default/" + args.asr_model, "PUT", token=token)
            api(args.url, "/admin/params", "PUT", {**by_code["server.auth.enabled"], "paramValue": "true"}, token)
            config = api(args.url, "/config/server-base", "POST", {}, secret)
        selected = config["selected_module"]
        asr_config = config["ASR"][selected["ASR"]]
        if asr_config.get("type") not in ("openai", "fun_server"):
            raise ValueError("Default ASR requires a different runtime; configure a remote model first with --configure")
        if config.get("server", {}).get("auth", {}).get("enabled") is not True:
            raise ValueError("Enable server.auth.enabled before running authentication checks")
        if args.publish_urls:
            # The upstream manager rejects loopback URLs and tests connectivity.
            # Compose DNS names are reachable by the manager and protocol client.
            for key, value in {"server.websocket": args.ws_url, "server.ota": args.ota_url}.items():
                api(args.url, "/admin/params", "PUT", {**by_code[key], "paramValue": value}, token)
        private_json(output / "data/.config.yaml", {
            "server": {"ip": "0.0.0.0", "port": 8000, "http_port": 8003, "auth_key": secret,
                       "vision_explain": "http://voice:8003/mcp/vision/explain"},
            "manager-api": {"url": args.container_manager_url, "secret": secret, "max_retries": 0},
            "prompt_template": "agent-base-prompt.txt",
        })
        private_json(output / "data/check.json", {
            "manager_url": args.container_manager_url, "admin_token": token, "server_secret": secret,
            "public_ws_url": args.ws_url, "asr_model": selected["ASR"],
        })
        (output / "logs").mkdir(parents=True, exist_ok=True, mode=0o700)
        (output / "startup-check").mkdir(parents=True, exist_ok=True, mode=0o700)
        print("PASS: private configuration prepared from the real management API")
        print(f"Selected ASR: {selected['ASR']}; VAD: {selected['VAD']}")
        print("Provider initialization and protocol checks do not verify remote inference credentials or audio generation.")
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
