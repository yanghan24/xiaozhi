"""Opt-in timing of the existing voice path. No audio or text is collected."""

import json
import re
import threading
import time
import uuid


class VoiceTiming:
    def __init__(self, conn, until_ms):
        self.conn = conn
        self.until_ms = until_ms
        self.trace_id = uuid.uuid4().hex
        self.started = time.monotonic_ns()
        self.phases = {}
        self.sentence_id = None
        self.tts_retries = 0
        self.finished = False
        self._finish_lock = threading.Lock()

    @staticmethod
    def enabled_until(conn):
        return getattr(getattr(conn, 'identity_session', None), 'diagnostics_until_ms', 0)

    @classmethod
    def input(cls, conn):
        until = cls.enabled_until(conn)
        if not until or until <= time.time() * 1000:
            return None
        trace = getattr(conn, 'voice_input_timing', None)
        if trace is None or trace.until_ms != until or time.monotonic_ns() - trace.started > 120_000_000_000:
            trace = conn.voice_input_timing = cls(conn, until)
            trace.mark('first_audio_frame')
        return trace

    @staticmethod
    def for_sentence(conn, sentence_id):
        trace = getattr(conn, 'voice_output_timing', None)
        return trace if trace is not None and sentence_id and trace.sentence_id == sentence_id else None

    def active(self):
        return (not self.finished and self.enabled_until(self.conn) == self.until_ms
                and self.until_ms > time.time() * 1000)

    def mark(self, phase):
        if phase not in self.phases:
            self.phases.setdefault(phase, (time.monotonic_ns() - self.started) // 1_000_000)

    def finish(self, outcome):
        with self._finish_lock:
            if not self.active():
                return
            self.finished = True
        config = getattr(self.conn, 'config', {})
        providers = {}
        for kind in ('ASR', 'VAD', 'Intent', 'Memory', 'LLM', 'TTS'):
            key = config.get('selected_module', {}).get(kind)
            details = config.get(kind, {}).get(key, {})
            model = details.get('model_name') or key
            if isinstance(model, str) and re.fullmatch(r'[A-Za-z0-9_.-]{1,100}', model):
                providers[kind] = model
        data = {'trace_id': self.trace_id, 'outcome': outcome,
                'phase_offset_ms': dict(self.phases), 'tts_retries': self.tts_retries,
                'models': providers}
        self.conn.logger.bind(tag=__name__).info('voice_timing {}', json.dumps(data, separators=(',', ':')))
