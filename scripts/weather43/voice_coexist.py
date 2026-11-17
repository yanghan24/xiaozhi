#!/usr/bin/env python3
"""Exercise deployed product ASR -> LLM -> TTS using a private test device.

Uses an existing synthetic recording, not a microphone or customer recording.
No device credential, voice token or model key is written to the report.
"""
import argparse
import asyncio
import audioop
from datetime import datetime, timezone
import hashlib
import http.client
import json
import math
import re
from pathlib import Path
import socket
import ssl
import time
import traceback
import wave
from urllib.parse import urlsplit

import opuslib_next
import websockets


def utc():
    return datetime.now(timezone.utc).isoformat()


def get_json(origin, path, headers, connect_host, connect_port):
    url = urlsplit(origin)
    connection = http.client.HTTPSConnection(url.hostname, url.port or 443, timeout=15,
                                           context=ssl.create_default_context())
    if connect_host:
        connection._create_connection = lambda address, timeout, source_address=None: socket.create_connection(
            (connect_host, connect_port), timeout, source_address)
    try:
        connection.request("GET", path, headers=headers)
        response = connection.getresponse()
        body = response.read()
        if response.status != 200:
            raise ValueError("http_status_" + str(response.status))
        return json.loads(body)
    finally:
        connection.close()


async def attempt(args, device, pcm, number):
    start = time.monotonic()
    row = {"at": utc(), "phase": args.phase, "number": number, "success": False,
           "audio_frames": 0, "decoded_samples": 0, "timing_s": {}}
    stage = "state"
    try:
        state = await asyncio.to_thread(get_json, device["origin"], "/xiaozhi/device-control/v1/state",
                                       {"Device-Id": device["device_id"],
                                        "Authorization": "Bearer " + device["credential"]},
                                       args.connect_host, args.connect_port)
        permission = state["data"]
        if permission["use_permission"] != "allowed":
            raise ValueError("permission_" + permission["reason"])
        voice = permission["voice"]
        row["timing_s"]["state"] = time.monotonic() - start
        headers = {"Device-Id": device["device_id"], "Client-Id": device["device_id"],
                   "Authorization": "Bearer " + voice["token"]}
        options = {"additional_headers": headers, "open_timeout": 15, "max_size": 1048576}
        if args.connect_host:
            options.update(host=args.connect_host, port=args.connect_port,
                           server_hostname=urlsplit(voice["url"]).hostname, ssl=ssl.create_default_context())
        stage = "websocket"
        async with websockets.connect(voice["url"], **options) as ws:
            await ws.send(json.dumps({"type": "hello", "version": 1, "transport": "websocket",
                                      "audio_params": {"format": "opus", "sample_rate": 24000,
                                                       "channels": 1, "frame_duration": 60}}))
            hello = json.loads(await asyncio.wait_for(ws.recv(), 15))
            ready_origin = voice["url"].replace("wss:", "https:").replace("ws:", "http:")
            parsed = urlsplit(ready_origin)
            stage = "ready"
            for _ in range(150):
                ready = await asyncio.to_thread(get_json, ready_origin, parsed.path + "ready/" + hello["session_id"],
                                               headers, args.connect_host, args.connect_port)
                if ready.get("status") != "pending":
                    break
                await asyncio.sleep(.1)
            if ready.get("status") != "ready":
                raise ValueError("not_ready_" + str(ready.get("status")))
            row["timing_s"]["ready"] = time.monotonic() - start
            stage = "audio_send"
            encoder = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_AUDIO)
            decoder = opuslib_next.Decoder(24000, 1)
            digest = hashlib.sha256()
            await ws.send(json.dumps({"type": "listen", "state": "start", "mode": "manual"}))
            audio_start = time.monotonic()
            chunks = [pcm[i:i + 1920].ljust(1920, b"\0") for i in range(0, len(pcm), 1920)]
            for i, chunk in enumerate(chunks):
                await ws.send(encoder.encode(chunk, 960))
                await asyncio.sleep(max(0, audio_start + (i + 1) * .06 - time.monotonic()))
            await ws.send(json.dumps({"type": "listen", "state": "stop"}))
            sent = time.monotonic()
            row["timing_s"]["audio_sent"] = sent - start
            stage = "response"
            # The deployed image uses Python 3.10, before asyncio.timeout.
            deadline = time.monotonic() + args.response_timeout
            while time.monotonic() < deadline:
                message = await asyncio.wait_for(ws.recv(), deadline - time.monotonic())
                elapsed = time.monotonic() - sent
                if isinstance(message, bytes):
                    decoded = decoder.decode(message, 1440)
                    row["audio_frames"] += 1
                    row["decoded_samples"] += len(decoded) // 2
                    digest.update(decoded)
                    row["timing_s"].setdefault("first_audio_after_input", elapsed)
                else:
                    event = json.loads(message)
                    if not isinstance(event, dict):
                        row.setdefault("non_object_events", []).append(type(event).__name__)
                        continue
                    kind = event.get("type")
                    if kind == "stt":
                        row["recognized_synthetic_text"] = event.get("text", "")[:300]
                        row["recognized_expected_phrase"] = "你好语音测试成功" in re.sub(
                            r"[^\u3400-\u9fff]", "", row["recognized_synthetic_text"])
                        row["timing_s"].setdefault("stt_after_input", elapsed)
                    elif kind == "llm":
                        row["timing_s"].setdefault("llm_after_input", elapsed)
                    elif kind == "tts" and event.get("state") == "sentence_start" and event.get("text"):
                        row.setdefault("spoken_synthetic_response", []).append(event["text"][:500])
                    elif kind == "tts" and event.get("state") == "stop":
                        row["timing_s"]["complete_after_input"] = elapsed
                        row["decoded_pcm_sha256"] = digest.hexdigest()
                        row["success"] = (row.get("recognized_expected_phrase", False) and row["audio_frames"] > 0 and
                                          bool(row.get("spoken_synthetic_response")) and "llm_after_input" in row["timing_s"])
                        if not row["success"]:
                            row["failure"] = "missing_expected_recognition_or_model_audio"
                        break
                    elif kind in ("error", "goodbye"):
                        raise ValueError("service_event_" + kind)
            if "complete_after_input" not in row["timing_s"]:
                raise ValueError("connection_closed_before_tts_stop")
    except Exception as error:
        # Never stringify transport exceptions that might contain auth headers.
        row["failure"] = type(error).__name__
        row["failure_stage"] = stage
        row["failure_frames"] = [{"file": Path(f.filename).name, "line": f.lineno, "function": f.name}
                                 for f in traceback.extract_tb(error.__traceback__)[-5:]]
        if isinstance(error, websockets.exceptions.ConnectionClosed):
            row["websocket_close_code"] = error.rcvd.code if error.rcvd else None
    row["elapsed_s"] = time.monotonic() - start
    return row


async def run(args):
    if args.output.exists():
        raise ValueError("Refusing to overwrite evidence")
    device = json.loads(args.device.read_text())
    with wave.open(str(args.audio), "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 2:
            raise ValueError("Expected synthetic mono 16-bit WAV")
        pcm = source.readframes(source.getnframes())
        rate = source.getframerate()
    if rate != 16000:
        pcm, _ = audioop.ratecv(pcm, 2, 1, rate, 16000, None)
    args.output.mkdir(parents=True)
    context = {"scope": "server_product_protocol_with_real_ASR_LLM_TTS",
               "primary_latency_measurement": "first decodable audio after input stop; completion also recorded",
               "load_generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               "physical_microphone_or_speaker": False, "phase": args.phase,
               "source_audio_sha256": hashlib.sha256(args.audio.read_bytes()).hexdigest(),
               "device_id_sha256": hashlib.sha256(device["device_id"].encode()).hexdigest(),
               "tls_verified": True, "connect_override": bool(args.connect_host),
               "connect_reason": "LAN route preserves public Host and TLS SNI; public-port hairpin times out",
               "count": args.count, "started_at": utc()}
    (args.output / "context.json").write_text(json.dumps(context, indent=2) + "\n")
    rows = []
    with (args.output / "requests.jsonl").open("x") as output:
        for number in range(args.count):
            row = await attempt(args, device, pcm, number)
            rows.append(row)
            output.write(json.dumps(row, ensure_ascii=False) + "\n")
            output.flush()
            print(json.dumps({"phase": args.phase, "number": number, "success": row["success"],
                              "elapsed_s": row["elapsed_s"], "failure_stage": row.get("failure_stage")}), flush=True)
            if args.stop_on_failure and not row["success"]:
                break
            await asyncio.sleep(args.interval)
    summary = {"count": len(rows), "success": sum(r["success"] for r in rows),
               "failure": sum(not r["success"] for r in rows), "finished_at": utc(),
               "planned_count": args.count, "stopped_on_failure": args.stop_on_failure and any(not r["success"] for r in rows),
               "percentile_method": "nearest_rank_on_successful_attempts", "timing_s": {}}
    for name in ("stt_after_input", "llm_after_input", "first_audio_after_input", "complete_after_input"):
        values = sorted(r["timing_s"][name] for r in rows if r["success"] and name in r["timing_s"])
        summary["timing_s"][name] = {"samples": len(values), "p50": values[max(0, math.ceil(len(values) * .5) - 1)] if values else None,
                                           "p95": values[max(0, math.ceil(len(values) * .95) - 1)] if values else None,
                                           "max": max(values) if values else None}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase", required=True)
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--interval", type=float, default=5)
    parser.add_argument("--response-timeout", type=float, default=90)
    parser.add_argument("--connect-host")
    parser.add_argument("--stop-on-failure", action="store_true", help="Stop this phase at the first real voice failure")
    parser.add_argument("--connect-port", type=int, default=443)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()
