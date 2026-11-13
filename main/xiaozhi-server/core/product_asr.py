"""Bounded, memory-only audio and AI-market rtasr-llm transcription."""
import asyncio
import io
import json
import re
import secrets
import time
import aiohttp
import wave
from urllib.parse import urlparse

from core.product_voice import VoiceDenied
from core.providers.asr.base import ASRProviderBase
from core.providers.asr.dto.dto import InterfaceType


class TranscriptionUnavailable(Exception):
    def __init__(self):
        super().__init__('Audio transcription unavailable')


class KeyRejected(Exception):
    pass


class AudioPublication:
    """Owned by one event loop; URLs expire even when the requester disappears."""
    PATH = '/xiaozhi/asr-audio/'
    MAX_PCM = 16000 * 2 * 60
    TTL = 90
    MAX_FILES = 32

    def __init__(self, public_origin):
        parsed = urlparse(public_origin)
        if (not parsed.hostname or parsed.username or parsed.password or parsed.query
                or parsed.fragment or parsed.path not in ('', '/')
                or (parsed.scheme != 'https' and not
                    (parsed.scheme == 'http' and parsed.hostname in ('127.0.0.1', 'localhost')))):
            raise ValueError('Product ASR requires a public HTTPS audio origin')
        self.origin = public_origin.rstrip('/')
        self.files = {}

    def publish(self, pcm, gate):
        if not pcm or len(pcm) > self.MAX_PCM or len(pcm) % 2 or len(self.files) >= self.MAX_FILES:
            raise TranscriptionUnavailable()
        output = io.BytesIO()
        with wave.open(output, 'wb') as audio:
            audio.setnchannels(1)
            audio.setsampwidth(2)
            audio.setframerate(16000)
            audio.writeframes(pcm)
        token = secrets.token_urlsafe(32)
        timer = asyncio.get_running_loop().call_later(self.TTL, self.remove, token)
        self.files[token] = (output.getvalue(), gate, time.monotonic() + self.TTL, timer)
        return token, self.origin + self.PATH + token + '.wav'

    def remove(self, token):
        entry = self.files.pop(token, None)
        if entry:
            entry[3].cancel()

    def revoke(self, gate):
        for token, entry in list(self.files.items()):
            if entry[1] is gate:
                self.remove(token)

    async def read(self, path):
        match = re.fullmatch(re.escape(self.PATH) + r'([A-Za-z0-9_-]{43})\.wav', path)
        entry = self.files.get(match[1]) if match else None
        if not entry:
            return None
        try:
            await entry[1].check_async()
        except VoiceDenied:
            self.remove(match[1])
            return None
        if time.monotonic() >= entry[2] or self.files.get(match[1]) is not entry:
            self.remove(match[1])
            return None
        return entry[0]


class MarketASR(ASRProviderBase):
    """One recorded utterance over paced WebSocket PCM; no persistence or retries."""
    MODEL = 'rtasr-llm'
    interface_type = InterfaceType.NON_STREAM
    TIMEOUT = 80

    def __init__(self, gate, publication):
        self.gate = gate
        self.publication = publication
        self.busy = False
        self.task = None
        self.utterance = None

    def cancel(self):
        self.publication.revoke(self.gate)
        if self.task is not None:
            self.task.cancel()
        # startToChat can interrupt older playback from inside this utterance.
        if self.utterance is not None and self.utterance is not asyncio.current_task():
            self.utterance.cancel()

    async def handle_voice_stop(self, conn, pcm_frames):
        # Keep the input consumer responsive while file transcription is pending.
        # One utterance per connection; continuous silence must not build a queue.
        if self.utterance is not None and not self.utterance.done():
            return
        self.utterance = asyncio.create_task(super().handle_voice_stop(conn, pcm_frames))

    async def receive_audio(self, conn, pcm_frame, audio_have_voice):
        if self.utterance is not None and not self.utterance.done():
            return
        # Bound the recording before joining frames or waiting for listen.stop.
        if sum(map(len, conn.asr_audio)) + len(pcm_frame) > self.publication.MAX_PCM:
            conn.reset_audio_states()
            await conn.websocket.close(code=1009, reason='Recording too long')
            return
        await super().receive_audio(conn, pcm_frame, audio_have_voice)

    async def speech_to_text_wrapper(self, pcm_data, session_id):
        # Bypass upstream file creation and disk-space checks: recordings stay in memory.
        return await self.speech_to_text(pcm_data, session_id)

    async def speech_to_text(self, pcm_data, session_id, artifacts=None):
        if self.busy or not pcm_data or not 0 < sum(map(len, pcm_data)) <= self.publication.MAX_PCM or sum(map(len, pcm_data)) % 2:
            raise TranscriptionUnavailable()
        self.busy = True
        try:
            self.task = asyncio.create_task(self._transcribe(b''.join(pcm_data)))
            return await asyncio.wait_for(self.task, self.TIMEOUT)
        except VoiceDenied:
            raise
        except Exception:
            raise TranscriptionUnavailable() from None
        finally:
            self.busy = False
            self.task = None

    @staticmethod
    def _message(raw):
        message = json.loads(raw)
        if not isinstance(message, dict):
            raise TranscriptionUnavailable()
        if (message.get('action') == 'error' or
                str(message.get('code', 0)) != '0'):
            raise TranscriptionUnavailable()
        data = message.get('data', {})
        if isinstance(data, str):
            data = json.loads(data) if data else {}
        if not isinstance(data, dict) or data.get('normal') is False:
            raise TranscriptionUnavailable()
        return message, data

    async def _transcribe(self, pcm):
        access = await asyncio.to_thread(self.gate.model_access)
        parsed = urlparse(access['base_url'])
        if (parsed.username or parsed.password or parsed.query or parsed.fragment or
                not parsed.hostname or (parsed.scheme != 'https' and not
                (parsed.scheme == 'http' and parsed.hostname in ('127.0.0.1', 'localhost')))):
            raise TranscriptionUnavailable()
        scheme = 'wss' if parsed.scheme == 'https' else 'ws'
        url = parsed._replace(scheme=scheme, path=parsed.path.rstrip('/') +
            '/asr/xfyun/rtasr-llm', query='model=rtasr-llm&lang=autodialect&audio_encode=pcm_s16le&samplerate=16000').geturl()
        # aiohttp WebSocket handshakes otherwise follow HTTP redirects.
        async def reject_redirect(*args):
            raise TranscriptionUnavailable()
        trace = aiohttp.TraceConfig()
        trace.on_request_redirect.append(reject_redirect)
        await self.gate.check_async()
        try:
            async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=5),
                    trust_env=False, trace_configs=[trace]) as session:
                async with session.ws_connect(url, headers={'Authorization': 'Bearer ' + access['api_key']},
                        max_msg_size=1024 * 1024, autoclose=True) as socket:
                    ready = await asyncio.wait_for(socket.receive(), 5)
                    if ready.type != aiohttp.WSMsgType.TEXT:
                        raise TranscriptionUnavailable()
                    message, data = self._message(ready.data)
                    session_id = data.get('sessionId') or message.get('sid')
                    if (not isinstance(session_id, str) or not 0 < len(session_id) <= 256 or
                            not (message.get('msg_type') == 'action' or message.get('action') == 'started')):
                        raise TranscriptionUnavailable()
                    ended = asyncio.Event()
                    sender = asyncio.create_task(self._send_pcm(socket, pcm, session_id, ended))
                    receiver = asyncio.create_task(self._receive_text(socket, ended))
                    monitor = asyncio.create_task(self._watch_permission())
                    tasks = (sender, receiver, monitor)
                    try:
                        # Sender errors and permission loss must also interrupt a silent receiver.
                        pending = set(tasks)
                        while True:
                            done, pending = await asyncio.wait(pending, return_when=asyncio.FIRST_COMPLETED)
                            for task in done:
                                task.result()
                            if receiver in done:
                                await sender
                                await self.gate.check_async()
                                return receiver.result(), None
                    finally:
                        for task in tasks:
                            task.cancel()
                        await asyncio.gather(*tasks, return_exceptions=True)
        except aiohttp.WSServerHandshakeError as error:
            if error.status == 401:
                await asyncio.to_thread(self.gate.key_rejected, access)
            raise TranscriptionUnavailable() from None

    async def _watch_permission(self):
        while True:
            await self.gate.check_async()
            await asyncio.sleep(.2)

    async def _send_pcm(self, socket, pcm, session_id, ended):
        for offset in range(0, len(pcm), 1280):
            await self.gate.check_async()
            chunk = pcm[offset:offset + 1280]
            await socket.send_bytes(chunk)
            await asyncio.sleep(len(chunk) / 32000)
        await self.gate.check_async()
        await socket.send_json({'end': True, 'sessionId': session_id})
        ended.set()

    async def _receive_text(self, socket, ended):
        segments = {}
        async for frame in socket:
            await self.gate.check_async()
            if frame.type != aiohttp.WSMsgType.TEXT:
                raise TranscriptionUnavailable()
            message, data = self._message(frame.data)
            if message.get('msg_type', message.get('action')) != 'result':
                raise TranscriptionUnavailable()
            if message.get('res_type', 'asr') != 'asr':
                continue
            sentence = data.get('cn', {}).get('st')
            if sentence is not None:
                segment = data.get('seg_id')
                if type(segment) is not int or not 0 <= segment < 4096:
                    raise TranscriptionUnavailable()
                if str(sentence.get('type')) not in ('0', '1'):
                    raise TranscriptionUnavailable()
                text = ''.join(word['cw'][0]['w'] for entry in sentence['rt'] for word in entry['ws'] if word.get('cw'))
                # seg_id numbers messages, including interim hypotheses. Only
                # committed results contribute text; a final punctuation frame is additive.
                if len(text) > 16000:
                    raise TranscriptionUnavailable()
                if str(sentence['type']) == '0':
                    segments[segment] = text
                elif data.get('ls') is True:
                    raise TranscriptionUnavailable()
                if sum(map(len, segments.values())) > 16000:
                    raise TranscriptionUnavailable()
            if data.get('ls') is True:
                if not ended.is_set():
                    raise TranscriptionUnavailable()
                text = ''.join(segments[key] for key in sorted(segments)).strip()
                if not text:
                    raise TranscriptionUnavailable()
                return text
        raise TranscriptionUnavailable()
