import asyncio
import base64
import json
import threading
from types import SimpleNamespace
from unittest.mock import Mock
from urllib.parse import parse_qs, urlsplit

import pytest
import websockets

from core.providers.tts.dto.dto import SentenceType
from core.providers.tts.xunfei_stream import TTSProvider


def market_provider(base_url="https://aimarket.hncig.cn", **config):
    provider = TTSProvider(dict(
        source="aimarket", api_key="fixture-key", model_name="super-smart-tts",
        base_url=base_url, private_voice="x6_lingfeiyi_pro", **config
    ), delete_audio_file=True)
    provider.conn = SimpleNamespace(
        sentence_id="turn", client_abort=False, stop_event=threading.Event(), sample_rate=24000
    )
    provider.opus_encoder = Mock()
    provider.opus_encoder.encode_pcm_to_opus_stream.side_effect = (
        lambda pcm, end_of_stream, callback: callback(pcm) if pcm else None
    )
    return provider


def response(status, pcm=b""):
    return json.dumps({
        "header": {"code": 0, "status": status},
        "payload": {"audio": {"status": status, "audio": base64.b64encode(pcm).decode()}},
    })


@pytest.mark.asyncio
async def test_original_provider_still_uses_signed_url_and_vendor_app_id(monkeypatch):
    connect = Mock()
    monkeypatch.setattr(websockets, "connect", connect)
    direct = TTSProvider(
        {"app_id": "app", "api_key": "system", "api_secret": "secret"}, delete_audio_file=True
    )
    direct.conn = SimpleNamespace(sample_rate=24000)
    direct._open_websocket()
    url = urlsplit(connect.call_args.args[0])
    assert url.hostname == "cbm01.cn-huabei-1.xf-yun.com"
    assert set(parse_qs(url.query)) == {"authorization", "date", "host"}
    assert "additional_headers" not in connect.call_args.kwargs
    assert direct._build_base_request(0)["header"]["app_id"] == "app"
    assert "oral" in direct._build_base_request(0)["parameter"]


@pytest.mark.asyncio
async def test_market_stream_uses_existing_provider_and_flushes_initial_and_final_pcm():
    requests = []

    async def synthesize(ws):
        assert ws.request.path == "/v1/private/mcd9m97e6?model=super-smart-tts"
        assert ws.request.headers["Authorization"] == "Bearer fixture-key"
        for index in range(3):
            requests.append(json.loads(await ws.recv()))
            await ws.send(response(index, bytes([index, 0])))

    async with websockets.serve(synthesize, "127.0.0.1", 0) as server:
        provider = market_provider(f"http://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        await provider.start_session("turn")
        assert provider.ws is None
        await provider.text_to_speak("你好，", None)
        await provider.text_to_speak("我是小智。", None)
        await asyncio.wait_for(provider.finish_session("turn"), 2)
        audio = list(provider.tts_audio_queue.queue)
        assert [item[0] for item in audio] == [SentenceType.FIRST] + [SentenceType.MIDDLE] * 3 + [SentenceType.LAST]
        assert audio[-2][1] == b"\x02\x00"
        assert all(item[3] == "turn" for item in audio)
        assert provider.opus_encoder.encode_pcm_to_opus_stream.call_args.args[:2] == (b"", True)
        assert [item["header"] for item in requests] == [{"status": 0}, {"status": 1}, {"status": 2}]
        assert [item["payload"]["text"]["seq"] for item in requests] == [0, 1, 2]
        assert requests[0]["parameter"]["tts"]["vcn"] == "x6_lingfeiyi_pro"
        assert "oral" not in requests[0]["parameter"]
        await provider.close()


@pytest.mark.asyncio
async def test_interrupted_market_stream_cannot_publish_late_pcm_or_close_the_next_turn():
    arrived = asyncio.Event()
    late = asyncio.Event()
    sessions = 0

    async def synthesize(ws):
        nonlocal sessions
        sessions += 1
        if sessions == 1:
            await ws.recv()
            arrived.set()
            await late.wait()
            await ws.send(response(1, b"\x00\x00"))
            await ws.wait_closed()
        else:
            await ws.recv()
            await ws.recv()
            await ws.send(response(2, b"\x12\x00"))

    async with websockets.serve(synthesize, "127.0.0.1", 0) as server:
        provider = market_provider(f"http://127.0.0.1:{server.sockets[0].getsockname()[1]}")
        await provider.start_session("turn")
        await provider.text_to_speak("中断此句", None)
        await arrived.wait()
        old_connection = provider.ws
        provider.conn.client_abort = True
        late.set()
        await asyncio.wait_for(old_connection.wait_closed(), 2)
        assert provider.tts_audio_queue.empty()

        provider.conn.sentence_id = "next"
        provider.conn.client_abort = False
        await provider.start_session("next")
        await provider.text_to_speak("继续正常播报", None)
        await asyncio.wait_for(provider.finish_session("next"), 2)
        assert [item[3] for item in provider.tts_audio_queue.queue] == ["next"] * 3
        await provider.close()


def test_market_provider_requires_user_key_and_a_valid_gateway():
    with pytest.raises(ValueError, match="Invalid AI Market TTS endpoint"):
        market_provider("https://unexpected.invalid")
    with pytest.raises(ValueError, match="MARKET_CREDENTIAL_REQUIRED"):
        TTSProvider({"source": "aimarket", "model_name": "super-smart-tts"}, True)
