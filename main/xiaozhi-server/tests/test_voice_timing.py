import json
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest

from core.device_session import diagnostic_deadline
from core.handle.sendAudioHandle import _do_send_audio, sendAudioMessage, send_stt_message, send_tts_message
from core.providers.tts.dto.dto import SentenceType
from core.voice_timing import VoiceTiming


def test_java_deadline_and_malformed_values():
    assert diagnostic_deadline('1760000000000') == 1760000000000
    assert diagnostic_deadline(1760000000000) == 1760000000000
    assert all(diagnostic_deadline(value) == 0 for value in (None, True, '-1', 'NaN', 1.2))


def test_lease_and_sentence_isolation(monkeypatch):
    monkeypatch.setattr('core.voice_timing.time.time', lambda: 5)
    logger = Mock()
    first = SimpleNamespace(identity_session=SimpleNamespace(diagnostics_until_ms=0), logger=logger)
    second = SimpleNamespace(identity_session=SimpleNamespace(diagnostics_until_ms=0), logger=Mock())
    assert VoiceTiming.input(first) is None
    first.identity_session.diagnostics_until_ms = 6500
    trace = VoiceTiming.input(first)
    assert VoiceTiming.input(second) is None
    assert VoiceTiming.input(first) is trace
    trace.sentence_id = 'first'
    first.voice_output_timing = trace
    assert VoiceTiming.for_sentence(first, 'old') is None
    assert VoiceTiming.for_sentence(first, 'first') is trace
    trace.mark('llm_first_text')
    trace.finish('completed')
    trace.finish('interrupted')
    logger.bind.return_value.info.assert_called_once()
    result = json.loads(logger.bind.return_value.info.call_args.args[1])
    assert result['trace_id'] == trace.trace_id
    assert set(result['phase_offset_ms']) == {'first_audio_frame', 'llm_first_text'}


def test_close_and_expiry_discard_unfinished_trace(monkeypatch):
    monkeypatch.setattr('core.voice_timing.time.time', lambda: 5)
    conn = SimpleNamespace(identity_session=SimpleNamespace(diagnostics_until_ms=6500), logger=Mock())
    trace = VoiceTiming.input(conn)
    conn.identity_session.diagnostics_until_ms = 0
    trace.finish('completed')
    conn.logger.bind.assert_not_called()
    conn.identity_session.diagnostics_until_ms = 6500
    monkeypatch.setattr('core.voice_timing.time.time', lambda: 7)
    assert VoiceTiming.input(conn) is None
    trace.finish('completed')
    conn.logger.bind.assert_not_called()


@pytest.mark.asyncio
async def test_farewell_uses_same_trace_for_start_frames_stop_and_close(monkeypatch):
    tick = [0]
    monkeypatch.setattr('core.voice_timing.time.time', lambda: 5)
    monkeypatch.setattr('core.voice_timing.time.monotonic_ns', lambda: tick[0] * 1_000_000)
    ws = SimpleNamespace(send=AsyncMock(), protocol=SimpleNamespace(
        close_sent=SimpleNamespace(code=1000), close_rcvd=None))
    conn = SimpleNamespace(
        identity_session=SimpleNamespace(diagnostics_until_ms=6500), logger=Mock(),
        websocket=ws, sentence_id='farewell', session_id='session', conn_from_mqtt_gateway=False,
        config={'end_prompt': {'prompt': 'farewell'}}, calling=False, close_after_chat=True,
        tts=SimpleNamespace(tts_audio_first_sentence=False), close=AsyncMock(), clearSpeakStatus=Mock())
    trace = VoiceTiming.input(conn)
    trace.sentence_id = conn.sentence_id
    conn.voice_output_timing = trace
    flow = {'sentence_id': conn.sentence_id}

    await send_stt_message(conn, 'farewell', timing=trace)
    tick[0] = 100
    await _do_send_audio(conn, b'first', flow)
    tick[0] = 190
    await _do_send_audio(conn, b'last', flow)
    await sendAudioMessage(conn, SentenceType.LAST, [], None, conn.sentence_id)

    start, first, last, stop = [call.args[0] for call in ws.send.await_args_list]
    assert first == b'first' and last == b'last'
    assert json.loads(start) == dict(type='tts', state='start', session_id='session', diagnostic_trace_id=trace.trace_id)
    assert json.loads(stop) == dict(type='tts', state='stop', session_id='session', diagnostic_trace_id=trace.trace_id)
    conn.close.assert_awaited_once()
    reports = {call.args[0]: json.loads(call.args[1]) for call in conn.logger.bind.return_value.info.call_args_list
               if call.args[0] in ('voice_timing {}', 'voice_tts_close {}')}
    timing = reports['voice_timing {}']
    assert timing['tts_frames_sent'] == 2 and timing['tts_max_send_gap_ms'] == 90
    assert timing['phase_offset_ms']['last_audio_sent'] == timing['phase_offset_ms']['tts_stop_sent'] == 190
    assert reports['voice_tts_close {}']['trace_id'] == trace.trace_id
    assert reports['voice_tts_close {}']['close_sent_code'] == 1000
    assert reports['voice_tts_close {}']['close_received_code'] is None

    await send_tts_message(conn, 'start', timing=trace)
    assert 'diagnostic_trace_id' not in json.loads(ws.send.await_args.args[0])


@pytest.mark.asyncio
async def test_stop_skipped_for_a_new_sentence_is_not_counted_as_sent(monkeypatch):
    from core.handle import sendAudioHandle

    monkeypatch.setattr('core.voice_timing.time.time', lambda: 5)
    conn = SimpleNamespace(identity_session=SimpleNamespace(diagnostics_until_ms=6500),
                           sentence_id='one', session_id='session', config={}, websocket=SimpleNamespace(send=AsyncMock()))
    trace = VoiceTiming.input(conn)

    async def switch_sentence(connection):
        connection.sentence_id = 'two'

    monkeypatch.setattr(sendAudioHandle, '_wait_for_audio_completion', switch_sentence)
    await send_tts_message(conn, 'stop', timing=trace)
    assert 'tts_stop_superseded' in trace.phases and 'tts_stop_sent' not in trace.phases
    conn.websocket.send.assert_not_awaited()
