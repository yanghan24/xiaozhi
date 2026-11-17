#!/usr/bin/env python3
"""Bounded development acceptance for the AI Market adapters.

Requires an explicitly authorized simulated device fixture under runtime. Uses
the existing Xiaozhi 104 database only. Real model calls require --live-voice.
No firmware, 114, AI Market settings, or third-party device tools are changed.
"""
import argparse
import asyncio
import base64
import io
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import threading
import time
import uuid
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch

import numpy as np
import pymysql
import redis
import requests
import yaml

parser = argparse.ArgumentParser()
parser.add_argument("--fixture-file", required=True)
parser.add_argument("--live-voice", action="store_true")
args = parser.parse_args()
assert os.environ.get("SPRING_DATASOURCE_DRUID_URL", "").startswith("jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?")
fixture_path = Path(args.fixture_file).resolve()
assert fixture_path.is_relative_to("/opt/xiaozhi-dev/run")
fixture = json.loads(fixture_path.read_text())
assert fixture["device"].startswith("model-integration-")
base = "http://127.0.0.1:8002/xiaozhi"
db = pymysql.connect(host="172.16.18.104", port=13306, user="xiaozhi_dev",
    password=os.environ["SPRING_DATASOURCE_DRUID_PASSWORD"], database="xiaozhi_dev", autocommit=True,
    cursorclass=pymysql.cursors.DictCursor)
cache = redis.Redis(host="172.16.18.104", port=16380, password=os.environ["SPRING_DATA_REDIS_PASSWORD"],
                    db=int(os.environ.get("SPRING_DATA_REDIS_DATABASE", 0)))
settings = dict(line.split("=", 1) for line in Path("/opt/xiaozhi-dev/speaker/speaker.properties").read_text().splitlines()
                if line and not line.startswith("#"))
private_key = yaml.safe_load(Path("/workspace/main/xiaozhi-server/data/.config.yaml").read_text())["manager-api"]["secret"]
device_headers = {"Device-Id": fixture["device"], "Authorization": "Bearer " + fixture["credential"]}
checks, counts, agent_ids, extra_models = [], {}, [], []
saved = {}
tts_server = None
prefix = "market-check-" + uuid.uuid4().hex[:10]

def sql(query, values=()):
    with db.cursor() as cursor:
        cursor.execute(query, values)
        return cursor.fetchall()

def admin(method, path, body=None, expected=200):
    response = requests.request(method, base + path, json=body,
        headers={"Authorization": "Bearer " + fixture["token"]}, timeout=30)
    data = response.json()
    assert response.status_code == expected, "Management status failed at " + path
    if expected == 200:
        assert data.get("code", 0) == 0, "Management request failed at " + path
        return data.get("data", data)
    return data

def private(path, body, expected=200):
    response = requests.post(base + "/config/" + path, json=body,
        headers={"Authorization": "Bearer " + private_key}, timeout=30)
    data = response.json()
    assert response.status_code == expected, "Private status failed at " + path
    if expected == 200:
        assert data.get("code") == 0, "Private request failed at " + path
        return data["data"]
    return data

original = sql("SELECT configured_agent_id,default_agent_id FROM speaker_runtime WHERE id=1")[0]
for kind in ("LLM", "VLLM", "ASR", "Intent", "Memory"):
    saved[kind] = sql("SELECT * FROM ai_model_config WHERE id=%s", ("MARKET_" + kind,))
backup = Path("/opt/xiaozhi-dev/run/market-integration-backup.json")
backup.write_text(json.dumps({"original": original, "models": saved}, default=str)); backup.chmod(0o600)

class SynthesisFixture(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        sound = io.BytesIO()
        with wave.open(sound, "wb") as wav:
            wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(24000)
            wav.writeframes((np.sin(np.arange(7200) * 2 * np.pi * 440 / 24000) * 3000).astype("<i2").tobytes())
        data = sound.getvalue()
        self.send_response(200); self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)

async def live_voice(ticket):
    import opuslib_next
    import websockets
    sys.path.insert(0, "/workspace/main/xiaozhi-server")
    os.chdir("/workspace/main/xiaozhi-server")
    from config.settings import load_config
    from core.websocket_server import WebSocketServer
    from core.utils.prompt_manager import PromptManager
    config = await load_config()
    config["server"]["auth_key"] = "development-only-protocol-fixture"
    server = WebSocketServer(config)
    audio = subprocess.run(["ffmpeg", "-v", "error", "-i", "config/assets/bind_code.wav",
        "-f", "s16le", "-ac", "1", "-ar", "16000", "pipe:1"], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=True).stdout
    with patch.object(PromptManager, "update_context_info", lambda *a: None), patch.object(
            PromptManager, "build_enhanced_prompt", lambda self, prompt, *a, **k: prompt):
        async with websockets.serve(server._handle_connection, "127.0.0.1", 0, process_request=server._http_response) as listener:
            url = "ws://127.0.0.1:" + str(listener.sockets[0].getsockname()[1]) + "/xiaozhi/v1/"
            async with websockets.connect(url, additional_headers={"Device-Id": fixture["device"],
                    "Client-Id": fixture["device"], "Authorization": "Bearer " + ticket}) as ws:
                await ws.send(json.dumps({"type": "hello", "version": 1, "transport": "websocket",
                    "features": {"emoji": False}, "audio_params": {"format": "opus", "sample_rate": 16000,
                    "channels": 1, "frame_duration": 60}}))
                assert json.loads(await asyncio.wait_for(ws.recv(), 15))["type"] == "hello"
                await ws.send(json.dumps({"type": "listen", "state": "start", "mode": "manual"}))
                encoder = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_AUDIO)
                for offset in range(0, len(audio), 1920):
                    frame = audio[offset:offset + 1920].ljust(1920, b"\0")
                    await ws.send(encoder.encode(frame, 960)); await asyncio.sleep(.06)
                stopped = time.monotonic()
                await ws.send(json.dumps({"type": "listen", "state": "stop"}))
                heard, spoken, binary = [], [], 0
                while True:
                    packet = await asyncio.wait_for(ws.recv(), 40)
                    if isinstance(packet, bytes):
                        binary += 1
                    else:
                        event = json.loads(packet)
                        if event.get("type") == "stt": heard.append(event.get("text", ""))
                        if event.get("type") == "tts" and event.get("text"): spoken.append(event["text"])
                        if event.get("type") == "tts" and event.get("state") == "stop": break
                assert heard and binary > 0, "Real ASR/LLM did not reach original audio playback"
                checks.append("real ASR + real user application Key + original LLM + original TTS/Opus pipeline")
                counts["live_voice"] = {"recognized": heard, "spoken": spoken, "audio_packets": binary,
                    "after_stop_seconds": round(time.monotonic() - stopped, 3), "tts": "local protocol fixture, no real synthesis service"}
            await asyncio.sleep(3)

try:
    for kind in saved:
        options = admin("GET", "/models/market/" + kind + "/options")["items"]
        assert all("api_key" not in item and "upstream_url" not in item for item in options)
        counts[kind] = len(options)
        chosen = "spark_zh_iat_new" if kind == "ASR" else "qwen3.8-flash"
        assert chosen in [item["publicId"] for item in options]
        if kind == "ASR": assert [item["publicId"] for item in options] == ["spark_zh_iat_new"]
        admin("PUT", "/models/market/" + kind, {"publicId": chosen, "enabled": True})
    admin("PUT", "/models/market/ASR", {"publicId": "rtasr-llm", "enabled": True}, expected=409)
    admin("PUT", "/models/market/LLM", {"publicId": "doubao-seedream-5.0-lite", "enabled": True}, expected=409)
    assert not any(item["id"].startswith("MARKET_") for item in admin("GET", "/models/list?modelType=LLM&page=1&limit=100")["list"])
    assert any(item["id"] == "MARKET_LLM" for item in admin("GET", "/models/names?modelType=SLM"))
    checks.append("live catalogue filtering, single adapter per purpose, server-side bypass rejection and SLM selection")

    tts_server = ThreadingHTTPServer(("127.0.0.1", 0), SynthesisFixture)
    threading.Thread(target=tts_server.serve_forever, daemon=True).start()
    tts_model = prefix + "-tts"; extra_models.append(tts_model)
    tts_config = {"type": "openai", "api_url": "http://127.0.0.1:" + str(tts_server.server_port) + "/tts",
        "api_key": "synthetic-tts-fixture", "voice": "alloy", "format": "wav",
        "output_dir": "/opt/xiaozhi-dev/run/market-voice-audio"}
    sql("INSERT INTO ai_model_config(id,model_type,model_code,model_name,is_default,is_enabled,config_json,sort) VALUES (%s,'TTS',%s,%s,0,1,%s,0)",
        (tts_model, tts_model, tts_model, json.dumps(tts_config)))
    agent = admin("POST", "/agent", {"agentName": prefix}); agent_ids.append(agent)
    admin("PUT", "/agent/" + agent, {"systemPrompt": "这是接口联调。无论用户说什么，只回答两个字：收到。不要调用任何工具。",
        "vadModelId": "VAD_SileroVAD", "asrModelId": "MARKET_ASR", "llmModelId": "MARKET_LLM",
        "vllmModelId": "MARKET_VLLM", "slmModelId": "MARKET_LLM", "ttsModelId": tts_model,
        "intentModelId": "Intent_nointent", "memModelId": "Memory_mem_report_only",
        "chatHistoryConf": 1, "contextProviders": [], "functions": []})
    status = admin("GET", "/admin/speaker/agent-default")
    admin("POST", "/admin/speaker/agent-default/" + agent, {"expected_revision": status["revision"]})

    state = requests.get(base + "/speaker/v1/state", headers=device_headers, timeout=10).json()
    assert state["use_status"] == "ready", "Real user has not finished device authorization"
    opened = private("device-session/open", {"device_id": fixture["device"], "token": state["voice"]["token"]})
    body = {"macAddress": fixture["device"], "clientId": fixture["device"], "selectedModule": {}, "voiceSession": opened["session"]}
    configured = private("agent-models", body)
    model = configured["LLM"]["MARKET_LLM"]
    assert model["api_key"] and model["base_url"] == "https://aimarket.hncig.cn/v1"
    assert configured["ASR"]["MARKET_ASR"]["api_key"] == model["api_key"]
    for kind in saved:
        stored = sql("SELECT config_json FROM ai_model_config WHERE id=%s", ("MARKET_" + kind,))[0]
        assert "api_key" not in json.loads(stored["config_json"])
    private("agent-models", dict(body, voiceSession="invalid"), expected=401)
    private("device-session/close", {"session": opened["session"]})
    checks.append("real device authorization, automatic application Key provisioning, backend-only injection and invalid-session rejection")

    # Test the original JSON auxiliary configuration without invoking another model.
    admin("PUT", "/agent/" + agent, {"systemPrompt": "仅用于辅助模型配置检查。",
        "intentModelId": "MARKET_Intent", "memModelId": "MARKET_Memory"})
    state = requests.get(base + "/speaker/v1/state", headers=device_headers, timeout=10).json()
    opened = private("device-session/open", {"device_id": fixture["device"], "token": state["voice"]["token"]})
    aux = private("agent-models", dict(body, voiceSession=opened["session"]))
    assert aux["LLM"]["market_intent_llm"]["response_format"]["type"] == "json_object"
    assert aux["LLM"]["market_memory_llm"]["api_key"] == model["api_key"]
    assert "api_key" not in aux["Memory"]["MARKET_Memory"]
    private("device-session/close", {"session": opened["session"]})
    checks.append("original Intent/Memory auxiliary models keep the same customer's resource identity")
    admin("PUT", "/agent/" + agent, {"systemPrompt": "这是接口联调。无论用户说什么，只回答两个字：收到。不要调用任何工具。",
        "intentModelId": "Intent_nointent", "memModelId": "Memory_mem_report_only"})
    fixture["agent"] = agent; fixture["name"] = prefix
    fixture_path.write_text(json.dumps(fixture, ensure_ascii=False)); fixture_path.chmod(0o600)
    subprocess.run(["node", "/workspace/main/manager-web/scripts/check-market-ui.cjs", str(fixture_path)], check=True)
    checks.append("browser source groups, real options/save, ASR scope, desktop layout and original role editor")
    if args.live_voice:
        sys.path.insert(0, "/workspace/main/xiaozhi-server")
        os.chdir("/workspace/main/xiaozhi-server")
        import core.websocket_server  # Initialize original logging outside an event loop.
        state = requests.get(base + "/speaker/v1/state", headers=device_headers, timeout=10).json()
        asyncio.run(live_voice(state["voice"]["token"]))
    report = {"passed": checks, "counts": counts, "physical_device": False,
              "real_authorized_account_retained": True, "test_key_used_as_product_key": False}
    Path("/opt/xiaozhi-dev/run/market-integration-result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
    print(json.dumps(report, ensure_ascii=False))
finally:
    current = sql("SELECT configured_agent_id FROM speaker_runtime WHERE id=1")[0]["configured_agent_id"]
    if current in agent_ids:
        sql("UPDATE speaker_runtime SET configured_agent_id=%s,default_agent_id=%s,revision=revision+1 WHERE id=1",
            (original["configured_agent_id"], original["default_agent_id"]))
    sessions = sql("SELECT id,memory_scope FROM speaker_conversation WHERE device_id=%s", (fixture["device"],))
    for session in sessions:
        sql("DELETE FROM ai_agent_chat_title WHERE session_id=%s", (session["id"],))
        sql("DELETE FROM speaker_memory WHERE scope=%s", (session["memory_scope"],))
    for agent in agent_ids:
        admin("DELETE", "/agent/" + agent)
    for kind, rows in saved.items():
        key = "MARKET_" + kind
        current_rows = sql("SELECT * FROM ai_model_config WHERE id=%s", (key,))
        if current_rows:
            current_row = current_rows[0]
            writer = current_row.get("updater") or current_row.get("creator")
            if writer is not None and str(writer) != str(fixture["user_id"]):
                continue  # Preserve a concurrent administrator's settings.
        if rows:
            row = rows[0]
            columns = list(row)
            values = [column for column in columns if column != "id"]
            sql("UPDATE ai_model_config SET " + ",".join(column + "=%s" for column in values) + " WHERE id=%s",
                tuple(row[c] for c in values) + (key,))
        else:
            sql("DELETE FROM ai_model_config WHERE id=%s", (key,))
        cache.delete("model:data:" + key, "model:name:" + key)
    for key in extra_models:
        sql("DELETE FROM ai_model_config WHERE id=%s", (key,)); cache.delete("model:data:" + key, "model:name:" + key)
    cache.delete("server:config")
    if tts_server:
        tts_server.shutdown(); tts_server.server_close()
    db.close()
