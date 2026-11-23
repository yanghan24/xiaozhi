"""Only a local websocket fixture; no models, authorization or billing."""
import asyncio
import base64
import json
from types import SimpleNamespace
from unittest.mock import AsyncMock

import pytest
import websockets

from core.providers.asr.aimarket_xfyun_asr_v2 import ASRProvider
from core.providers.asr.base import ASRProviderBase


def result(text, sn, pgs="apd", rg=None):
    payload = {"sn": sn, "ws": [{"cw": [{"w": text}]}]}
    encoded = {"text": base64.b64encode(json.dumps(payload).encode()).decode(), "pgs": pgs}
    if rg is not None:
        encoded["rg"] = rg
    return encoded


def test_replaces_partial_results_without_duplicating_segments():
    parts = {}
    ASRProvider.merge_result(parts, result("旧", 1))
    ASRProvider.merge_result(parts, result("句子", 2))
    ASRProvider.merge_result(parts, result("新句子", 3, "rpl", [1, 2]))
    ASRProvider.merge_result(parts, result("新句子", 3))
    assert parts == {3: "新句子"}


@pytest.mark.asyncio
@pytest.mark.parametrize("manual", [True, False])
async def test_original_stream_interface_preserves_audio_and_final_handoff(monkeypatch, manual):
    frames = []
    received = bytearray()
    headers = []
    async def handler(ws):
        headers.append(ws.request.headers.get("Authorization"))
        async for raw in ws:
            message = json.loads(raw)
            frames.append(message)
            received.extend(base64.b64decode(message["payload"]["audio"]["audio"]))
            if message["header"]["status"] == 2:
                await ws.send(json.dumps({"header": {"code": 0, "status": 1}, "payload": {"result": result("旧文字", 1)}}))
                await ws.send(json.dumps({"header": {"code": 0, "status": 2}, "payload": {"result": result("最终文字", 2, "rpl", [1, 1])}}))
    handoffs = []
    async def handoff(self, conn, audio):
        handoffs.append((await self.speech_to_text(audio, "fixture"), b"".join(audio)))
    monkeypatch.setattr(ASRProviderBase, "handle_voice_stop", handoff)
    async with websockets.serve(handler, "127.0.0.1", 0) as server:
        root = "http://127.0.0.1:" + str(server.sockets[0].getsockname()[1])
        provider = ASRProvider({"model_name": "spark_zh_iat_new", "base_url": root, "api_key": "fixture-user-key"})
        conn = SimpleNamespace(client_listen_mode="manual" if manual else "auto", asr_audio=[],
                               client_have_voice=True, client_voice_stop=False, asr=provider,
                               reset_audio_states=lambda: None)
        audio = [b"\x01\x02" * 100, b"\x03\x04" * 900, b"\x05\x06" * 300]
        # Simulate bounded VAD pre-roll, then new speech frames.
        conn.asr_audio.append(audio[0])
        await provider.receive_audio(conn, audio[1], True)
        await provider.receive_audio(conn, audio[2], True)
        if manual:
            await provider._send_stop_request()
        else:
            conn.client_voice_stop = True
            await provider.receive_audio(conn, b"", False)
        task = provider._task
        await asyncio.wait_for(task, 3)
        assert bytes(received) == b"".join(audio)
        assert headers == ["Bearer fixture-user-key"]
        assert [m["payload"]["audio"]["seq"] for m in frames] == list(range(1, len(frames) + 1))
        assert frames[0]["parameter"]["iat"]["domain"] == "spark_asr_v2"
        assert all(m["header"]["status"] == m["payload"]["audio"]["status"] for m in frames)
        assert sum(m["header"]["status"] == 0 for m in frames) == 1
        assert sum(m["header"]["status"] == 2 for m in frames) == 1
        assert handoffs == [(("最终文字", None), b"".join(audio))]
        await provider.close()


def test_rejects_other_models_and_audio_longer_than_sixty_seconds():
    with pytest.raises(ValueError):
        ASRProvider({"model_name": "rtasr-llm"})
    provider = ASRProvider({"model_name": "spark_zh_iat_new"})
    provider._queue = asyncio.Queue()
    provider._bytes = provider.MAX_AUDIO_BYTES
    with pytest.raises(ValueError):
        provider._enqueue(b"\x00\x00")


@pytest.mark.asyncio
async def test_close_cancels_pending_receiver_without_handling_text(monkeypatch):
    handoff = AsyncMock()
    monkeypatch.setattr(ASRProviderBase, "handle_voice_stop", handoff)
    connected = asyncio.Event()
    async def handler(ws):
        connected.set()
        await ws.wait_closed()
    async with websockets.serve(handler, "127.0.0.1", 0) as server:
        root = "http://127.0.0.1:" + str(server.sockets[0].getsockname()[1])
        provider = ASRProvider({"model_name": "spark_zh_iat_new", "api_key": "fixture", "base_url": root})
        conn = SimpleNamespace(client_listen_mode="manual", asr_audio=[], client_have_voice=True,
            client_voice_stop=False, asr=provider, reset_audio_states=lambda: None)
        await provider.receive_audio(conn, b"\x00\x00"*640, True)
        await asyncio.wait_for(connected.wait(), 2)
        await provider.close()
        assert provider._task is None
        handoff.assert_not_awaited()


@pytest.mark.asyncio
@pytest.mark.parametrize("reject", [True, False])
async def test_rejection_or_missing_final_result_never_starts_chat(monkeypatch, reject):
    handoff = AsyncMock()
    monkeypatch.setattr(ASRProviderBase, "handle_voice_stop", handoff)
    async def handler(ws):
        async for raw in ws:
            if reject:
                await ws.send(json.dumps({"header": {"code": 100019, "status": 2}}))
                break
            if json.loads(raw)["header"]["status"] == 2:
                await ws.wait_closed()
                break
    async with websockets.serve(handler, "127.0.0.1", 0) as server:
        root = "http://127.0.0.1:" + str(server.sockets[0].getsockname()[1])
        provider = ASRProvider({"model_name": "spark_zh_iat_new", "api_key": "fixture", "base_url": root, "final_timeout": 1})
        conn = SimpleNamespace(client_listen_mode="manual", asr_audio=[], client_have_voice=True,
            client_voice_stop=False, asr=provider, reset_audio_states=lambda: None)
        await provider.receive_audio(conn, b"\x00\x00"*640, True)
        await provider._send_stop_request()
        await asyncio.wait_for(provider._task, 3)
        assert provider.last_error == "ASR_UNAVAILABLE"
        handoff.assert_not_awaited()
        await provider.close()
