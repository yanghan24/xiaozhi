"""Exercise the original runtime with controlled providers, without external calls."""
import queue
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest

from core.connection import ConnectionHandler
from core.providers.asr.xunfei_stream import ASRProvider
from core.providers.tts.dto.dto import SentenceType
from core.utils.dialogue import Dialogue


@pytest.mark.parametrize('interrupt', [False, True])
def test_original_chat_queues_text_before_model_stream_finishes(interrupt):
    speech = queue.Queue()
    conn = SimpleNamespace(
        logger=Mock(), dialogue=Dialogue(), memory=None, intent_type='nointent', session_id='fixture',
        current_speaker=None, system_introduced_speakers=set(), client_abort=False,
        features={'emoji': False}, config={}, tts=SimpleNamespace(
            tts_text_queue=speech, store_tts_text=Mock()),
    )
    conn.dialogue.update_system_message('Fixture assistant')

    def model(*args):
        yield 'First sentence.'
        # This executes before the second model fragment is available.
        assert any(item.sentence_type == SentenceType.MIDDLE and
                   item.content_detail == 'First sentence.' for item in speech.queue)
        if interrupt:
            conn.client_abort = True
        yield 'Second sentence.'

    conn.llm = SimpleNamespace(response=model)
    ConnectionHandler.chat(conn, 'question')
    spoken = [item.content_detail for item in speech.queue
              if item.sentence_type == SentenceType.MIDDLE]
    assert spoken == (['First sentence.'] if interrupt else ['First sentence.', 'Second sentence.'])
    assert speech.queue[-1].sentence_type == SentenceType.LAST


@pytest.mark.asyncio
async def test_original_streaming_asr_sends_frames_before_listen_stop():
    provider = ASRProvider({'app_id': 'fixture', 'api_key': 'fixture', 'api_secret': 'fixture'}, True)
    conn = SimpleNamespace(client_listen_mode='auto', asr_audio=[],
                           client_have_voice=True, client_voice_stop=False, asr=provider)

    async def start(_):
        provider.asr_ws = object()
        provider.is_processing = provider.server_ready = True

    provider._start_recognition = AsyncMock(side_effect=start)
    provider._send_audio_frame = AsyncMock()
    await provider.receive_audio(conn, b'first-pcm', True)
    provider._send_audio_frame.assert_awaited_once_with(b'first-pcm', 1)
    assert conn.client_voice_stop is False
    await provider.receive_audio(conn, b'second-pcm', True)
    assert provider._send_audio_frame.await_count == 2
    provider._start_recognition.assert_awaited_once()
