import json
from types import SimpleNamespace
from unittest.mock import Mock

from core.device_session import diagnostic_deadline
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
