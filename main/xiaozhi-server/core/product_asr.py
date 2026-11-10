"""Bounded, memory-only audio publication and AI-market asynchronous transcription."""
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
    """One connection's non-streaming ASR; never persists recordings or retries submit."""
    MODEL = 'qwen-audio-3.0-asr-flash-filetrans'
    interface_type = InterfaceType.NON_STREAM
    TIMEOUT = 60

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
        if self.busy or sum(map(len, pcm_data)) > self.publication.MAX_PCM:
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
    async def _json_request(url, key=None, payload=None):
        headers = {}
        if key:
            headers['Authorization'] = 'Bearer ' + key
        if payload is not None:
            headers.update({'Content-Type': 'application/json', 'X-DashScope-Async': 'enable'})
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=5), trust_env=False) as session:
            async with session.request('GET' if payload is None else 'POST', url,
                    headers=headers, json=payload, allow_redirects=False) as response:
                if response.status != 200:
                    raise TranscriptionUnavailable()
                raw = bytearray()
                async for chunk in response.content.iter_chunked(16384):
                    raw.extend(chunk)
                    if len(raw) > 1024 * 1024:
                        raise TranscriptionUnavailable()
                return json.loads(raw)

    async def _transcribe(self, pcm):
        access = await asyncio.to_thread(self.gate.model_access)
        base = access['base_url'].rstrip('/') + '/'
        token, audio_url = self.publication.publish(pcm, self.gate)
        try:
            await self.gate.check_async()
            submitted = await self._json_request(
                base + 'services/audio/asr/transcription', access['api_key'],
                {'model': self.MODEL, 'input': {'file_urls': [audio_url]}, 'parameters': {}})
            task = submitted.get('output', {}).get('task_id')
            if not isinstance(task, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', task):
                raise TranscriptionUnavailable()
            while True:
                await self.gate.check_async()
                result = await self._json_request(base + 'tasks/' + task, access['api_key'])
                await self.gate.check_async()
                output = result.get('output', {})
                status = output.get('task_status')
                if status == 'SUCCEEDED':
                    break
                if status not in ('PENDING', 'RUNNING'):
                    raise TranscriptionUnavailable()
                await asyncio.sleep(1)
            results = output.get('results', [])
            if len(results) != 1 or results[0].get('subtask_status') != 'SUCCEEDED':
                raise TranscriptionUnavailable()
            url = results[0].get('transcription_url', '')
            parsed, origin = urlparse(url), urlparse(base)
            same_origin = (parsed.scheme, parsed.netloc) == (origin.scheme, origin.netloc)
            official_result = parsed.scheme == 'https' and parsed.hostname in (
                'dashscope-result-bj.oss-cn-beijing.aliyuncs.com',
                'dashscope-result-sg.oss-ap-southeast-1.aliyuncs.com')
            if parsed.username or parsed.password or parsed.fragment or not (same_origin or official_result):
                raise TranscriptionUnavailable()
            # The result URL is a capability; never forward the business Key to it.
            await self.gate.check_async()
            transcript = await self._json_request(url)
            await self.gate.check_async()
            texts = [entry['text'] for entry in transcript.get('transcripts', [])
                     if entry.get('channel_id', 0) == 0 and isinstance(entry.get('text'), str)]
            text = ''.join(texts).strip()
            if not text or len(text) > 16000:
                raise TranscriptionUnavailable()
            return text, None
        finally:
            self.publication.remove(token)
