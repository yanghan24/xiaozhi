#!/usr/bin/env python3
"""Check the running voice service through its HTTP/WebSocket boundary."""
import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import urllib.request
import uuid

from voice_api import api, http_json, private_json


def require(condition, message):
    if not condition:
        raise ValueError(message)


def record(report, name, request, response):
    report["checks"].append({"name": name, "status": "passed", "request": request, "response": response})
    print("PASS: " + name)


def check_config(settings, report):
    path = "/config/server-base"
    status, response = http_json(settings["manager_url"], path, "POST", {}, settings["server_secret"])
    require(status == 200 and isinstance(response, dict) and response.get("code") == 0,
            "Real service credentials did not retrieve the server configuration")
    config = response["data"]
    selected = config["selected_module"]
    require(selected.get("ASR") == settings["asr_model"], "Unexpected default ASR model")
    require(config.get("server", {}).get("auth", {}).get("enabled") is True, "Device authentication is disabled")
    for module in ("VAD", "ASR"):
        require(bool(config.get(module, {}).get(selected.get(module))), f"Missing selected {module} configuration")
    record(report, "real service credentials retrieve configuration",
           {"method": "POST", "path": path, "authorization": "Bearer [redacted]"},
           {"http": status, "code": response["code"], "selected_module": selected, "device_authentication": True})
    status, response = http_json(settings["manager_url"], path, "POST", {}, "protocol-check-invalid-secret")
    require(status == 200 and isinstance(response, dict) and response.get("code") == 401,
            "Wrong service credentials were not rejected with API code 401")
    record(report, "wrong service credentials are rejected",
           {"method": "POST", "path": path, "authorization": "Bearer [invalid]"},
           {"http": status, "code": response["code"]})
    return config


def check_device(settings, report):
    base = settings["manager_url"]
    token = settings["admin_token"]
    secret = settings["server_secret"]
    trace = uuid.uuid4().hex
    mac = "02:" + ":".join(trace[index:index + 2] for index in range(0, 10, 2))
    client = "protocol-" + trace
    headers = {"Device-Id": mac, "Client-Id": client}
    report["device_id"] = mac
    report["client_id"] = client
    model_request = {"macAddress": mac, "clientId": client, "selectedModule": {}}
    model_evidence = {"method": "POST", "path": "/config/agent-models", "authorization": "Bearer [redacted]", "body": model_request}
    status, response = http_json(base, "/config/agent-models", "POST", model_request, secret)
    require(status == 200 and isinstance(response, dict) and response.get("code") == 10041,
            "Fresh device was not reported as unregistered")
    record(report, "fresh device has no configuration", model_evidence, {"http": status, "code": response["code"]})
    agent_id = api(base, "/agent", "POST", {"agentName": client}, token)
    require(isinstance(agent_id, str) and bool(agent_id), "No disposable agent ID returned")
    report["agent_id"] = agent_id
    try:
        api(base, "/agent/" + agent_id, "PUT", {
            "asrModelId": settings["asr_model"], "vadModelId": "VAD_SileroVAD",
            "llmModelId": "LLM_ChatGLMLLM", "ttsModelId": "TTS_EdgeTTS",
            "intentModelId": "Intent_nointent", "memModelId": "Memory_nomem", "chatHistoryConf": 0,
        }, token)
        body = {"version": 1, "mac_address": mac, "uuid": trace,
                "application": {"name": "protocol-check", "version": "1.0.0"},
                "board": {"type": "protocol-check", "mac": mac}}
        status, response = http_json(base, "/ota/activate", "POST", {}, headers=headers)
        require(status == 202, "Unbound device was reported as activated")
        record(report, "unbound device activation is pending", {"method": "POST", "path": "/ota/activate", **headers}, {"http": status})
        status, response = http_json(base, "/ota/", "POST", body, headers=headers)
        code = response.get("activation", {}).get("code") if isinstance(response, dict) else None
        require(status == 200 and isinstance(code, str) and len(code) == 6 and code.isdigit(),
                "OTA did not issue a six-digit activation code")
        record(report, "OTA issues activation information", {"method": "POST", "path": "/ota/", "headers": headers, "body": body},
               {"http": status, "activation": {"code": "[redacted]", "length": len(code)}})
        status, response = http_json(base, "/config/agent-models", "POST", model_request, secret)
        require(status == 200 and isinstance(response, dict) and response.get("code") == 10042,
                "Pending device was not reported as requiring binding")
        record(report, "pending device cannot retrieve agent configuration", model_evidence, {"http": status, "code": response["code"]})
        status, response = http_json(base, f"/device/bind/{agent_id}/{code}", "POST", token=token)
        require(status == 200 and isinstance(response, dict) and response.get("code") == 0, "Activation-code binding failed")
        devices = api(base, "/device/bind/" + agent_id, token=token)
        matching = [device for device in devices if device.get("macAddress", "").lower() == mac]
        # This endpoint returns only the authenticated account's devices.
        # Its normal-user response does not populate bindUserName.
        require(len(matching) == 1, "Device identity was absent from the authenticated account's bound devices")
        record(report, "management API binds the device to the logged-in test account",
               {"method": "POST", "path": f"/device/bind/{agent_id}/[activation-code]"},
               {"http": status, "code": response["code"], "readback_path": "/device/bind/" + agent_id,
                "device_id": matching[0]["id"], "macAddress": matching[0]["macAddress"], "visible_to_logged_in_account": True})
        status, response = http_json(base, "/ota/activate", "POST", {}, headers=headers)
        require(status == 200 and response == "success", "Bound device activation did not become successful")
        record(report, "bound device activation succeeds", {"method": "POST", "path": "/ota/activate", **headers}, {"http": status, "body": response})
        status, response = http_json(base, "/ota/", "POST", body, headers=headers)
        require(status == 200 and isinstance(response, dict) and not response.get("activation"), "Bound OTA still requires activation")
        websocket = response.get("websocket", {})
        require(websocket.get("url") == settings["public_ws_url"] and bool(websocket.get("token")),
                "Bound OTA has no authenticated WebSocket configuration at the expected URL; check server.websocket and server.auth.enabled")
        record(report, "bound OTA returns authenticated WebSocket configuration", {"method": "POST", "path": "/ota/", "headers": headers},
               {"http": status, "websocket": {"url": websocket["url"], "token": "[redacted]"}, "activation": "absent"})
        config = api(base, "/config/agent-models", "POST", model_request, secret)
        selected = config["selected_module"]
        require(selected.get("ASR") == settings["asr_model"] and config.get("chat_history_conf") == 0,
                "Bound agent configuration does not match the disposable test agent")
        providers = {}
        for module, name in selected.items():
            provider = config.get(module, {}).get(name, {})
            key = provider.get("api_key") or ""
            key_state = "missing" if not key.strip() else ("placeholder" if "你" in key or "your_" in key else "present_unverified")
            providers[module] = {"id": name, "type": provider.get("type"), "api_key_state": key_state}
        record(report, "bound device retrieves its actual agent configuration", model_evidence,
               {"http": 200, "code": 0, "providers": providers, "chat_history_conf": config["chat_history_conf"]})
        asyncio.run(check_websocket(websocket, headers, report))
    finally:
        api(base, "/agent/" + agent_id, "DELETE", token=token)
        require(all(agent["id"] != agent_id for agent in api(base, "/agent/list", token=token)),
                "Disposable agent cleanup did not persist")
        status, _ = http_json(base, "/ota/activate", "POST", {}, headers=headers)
        require(status == 202, "Disposable device still exists after agent cleanup")
        record(report, "disposable agent and device are removed", {"method": "DELETE", "path": "/agent/" + agent_id},
               {"code": 0, "device_activation_http_after_delete": status})


async def check_websocket(websocket, headers, report):
    from websockets.asyncio.client import connect

    url = websocket["url"]
    async with connect(url, additional_headers={**headers, "Authorization": "Bearer protocol-check-invalid-token"},
                       open_timeout=10, close_timeout=5, ping_interval=None) as connection:
        rejection = await asyncio.wait_for(connection.recv(), 5)
        require(rejection == "认证失败", "Invalid WebSocket credentials were not rejected")
        await asyncio.wait_for(connection.wait_closed(), 5)
        record(report, "wrong WebSocket credentials are rejected and closed",
               {"url": url, "headers": {**headers, "Authorization": "Bearer [invalid]"}},
               {"message": rejection, "close_code": connection.close_code})

    hello = {"type": "hello", "version": 1, "transport": "websocket",
             "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1, "frame_duration": 60}}
    async with connect(url, additional_headers={**headers, "Authorization": "Bearer " + websocket["token"]},
                       open_timeout=10, close_timeout=5, ping_interval=None) as connection:
        await connection.send(json.dumps(hello))
        welcome = json.loads(await asyncio.wait_for(connection.recv(), 10))
        require(welcome.get("type") == "hello" and welcome.get("version") == 1 and welcome.get("transport") == "websocket",
                "Voice service did not return the upstream hello handshake")
        require(welcome.get("audio_params") == hello["audio_params"], "Hello audio negotiation did not match")
        uuid.UUID(welcome["session_id"])
        record(report, "OTA credentials complete a real WebSocket hello handshake",
               {"url": url, "headers": {**headers, "Authorization": "Bearer [redacted]"}, "message": hello},
               {key: welcome[key] for key in ("type", "version", "transport", "session_id", "audio_params")})
        # Allow the server to fetch and initialize private configuration while
        # keeping the socket alive, without sending audio or invoking a model.
        for _ in range(3):
            waiter = await connection.ping(b"xiaozhi-protocol-check")
            await asyncio.wait_for(waiter, 5)
            await asyncio.sleep(1)
        record(report, "WebSocket control Ping/Pong messages round trip",
               {"opcode": 9, "payload": "xiaozhi-protocol-check", "count": 3},
               {"opcode": 10, "matching_payload": True, "count": 3})


def check_health(ws_url, report):
    url = ws_url.replace("ws://", "http://", 1).replace("wss://", "https://", 1)
    client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with client.open(url, timeout=10) as response:
        if response.status != 200 or response.read().decode().strip() != "Server is running":
            raise ValueError("voice service did not return its HTTP health response")
    record(report, "voice service HTTP boundary", {"method": "GET", "url": url}, {"http": 200, "body": "Server is running"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ws-url", help="Health URL (default: host loopback, or the advertised URL with --settings)")
    parser.add_argument("--settings", help="Private JSON containing the real manager credentials")
    parser.add_argument("--output", help="Sanitized JSON result, overwritten on both success and failure")
    args = parser.parse_args()
    report = {"started_at": datetime.now(timezone.utc).isoformat(), "status": "failed", "checks": [],
              "scope": "device_protocol" if args.settings else "http_health",
              "audio_inference": "not_run", "hardware_playback": "not_run"}
    settings = {}
    try:
        if args.settings:
            settings = json.loads(Path(args.settings).read_text())
            check_health(args.ws_url or settings["public_ws_url"], report)
            check_config(settings, report)
            check_device(settings, report)
        else:
            check_health(args.ws_url or "ws://127.0.0.1:18000/xiaozhi/v1/", report)
        report["status"] = "passed"
    except Exception as error:
        message = str(error)
        for key in ("admin_token", "server_secret"):
            if settings.get(key):
                message = message.replace(settings[key], "[redacted]")
        report["error"] = message
        print(f"FAIL: {message}", file=sys.stderr)
    report["finished_at"] = datetime.now(timezone.utc).isoformat()
    if args.output:
        private_json(args.output, report)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
