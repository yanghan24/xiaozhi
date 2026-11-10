#!/usr/bin/env python3
"""Temporary single-account test bridge; not the product device authorization flow.

Expose API routes only on the private Docker network. Publish only AUDIO_PATH
through TLS. Credentials are short leases refreshed by the authorized operator.
No recording, capability URL, transcript, prompt or credential is logged here.
"""
import argparse
import asyncio
import hmac
import io
import json
import re
import secrets
import time
import wave
import contextvars
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlsplit

import aiohttp
import async_timeout
from aiohttp import web

ASR = 'qwen-audio-3.0-asr-flash-filetrans'
LLM = 'qwen3.8-flash'
AUDIO_PATH = '/xiaozhi/test-asr-audio/'
MAX_AUDIO = 2 * 1024 * 1024
TRACE = contextvars.ContextVar('market_trace', default=None)


def stage(name):
    trace = TRACE.get()
    if trace is not None:
        trace['stage'] = name
        trace['stage_offsets_ms'].setdefault(name, round((time.monotonic() - trace['_start']) * 1000, 2))
        if not name.endswith('_complete'):
            trace['upstream_http_status'] = None
            trace['upstream_error_code'] = None


def upstream_code(body):
    if not isinstance(body, dict):
        return None
    code = body.get('code')
    if code is None and isinstance(body.get('error'), dict):
        code = body['error'].get('code')
    # Never log messages, full bodies, URLs, headers or request/credential IDs.
    if isinstance(code, (str, int)) and re.fullmatch(r'[A-Za-z0-9_.-]{1,64}', str(code)):
        return str(code)
    return None


class Bridge:
    def __init__(self, config, lease_path, market='https://aimarket.hncig.cn/v1/'):
        self.config = config
        self.lease_path = Path(lease_path)
        self.market = market.rstrip('/') + '/'
        self.audio = {}
        self.active = 0
        self.stats = {'asr_submitted': 0, 'asr_succeeded': 0, 'llm_submitted': 0,
                      'llm_succeeded': 0, 'failures': 0}

    @web.middleware
    async def audit(self, request, handler):
        if request.path not in ('/v1/audio/transcriptions', '/v1/chat/completions'):
            return await handler(request)
        trace = {'event': 'market_request', 'request_id': secrets.token_hex(16),
                 'operation': 'asr' if request.path.endswith('transcriptions') else 'llm',
                 'started_at_utc': datetime.now(timezone.utc).isoformat(),
                 '_start': time.monotonic(), 'stage_offsets_ms': {},
                 'bridge_http_status': None, 'upstream_http_status': None,
                 'upstream_error_code': None, 'error_type': None}
        handle = TRACE.set(trace)
        stage('authorization')
        try:
            response = await handler(request)
            trace['bridge_http_status'] = response.status
            return response
        except web.HTTPException as error:
            trace['bridge_http_status'] = error.status
            trace['error_type'] = trace['error_type'] or type(error).__name__
            raise
        except asyncio.CancelledError:
            trace['error_type'] = 'CancelledError'
            raise
        except Exception as error:
            trace['error_type'] = type(error).__name__
            raise
        finally:
            trace['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
            trace['elapsed_ms'] = round((time.monotonic() - trace.pop('_start')) * 1000, 2)
            print(json.dumps(trace, ensure_ascii=True), flush=True)
            TRACE.reset(handle)

    def key(self):
        try:
            lease = json.loads(self.lease_path.read_text())
            if (time.time() >= min(lease['expires_at'], self.config['expires_at'])
                    or not lease.get('api_key')):
                raise ValueError()
            return lease['api_key']
        except (OSError, ValueError, KeyError, TypeError):
            raise web.HTTPServiceUnavailable(text='Test authorization unavailable') from None

    def authorize(self, request):
        if not hmac.compare_digest(request.headers.get('Authorization', ''),
                                   'Bearer ' + self.config['bridge_token']):
            raise web.HTTPUnauthorized()
        self.key()

    async def json_request(self, session, url, payload=None, key=None):
        headers = {'Authorization': 'Bearer ' + key} if key else {}
        if payload is not None:
            headers['X-DashScope-Async'] = 'enable'
        async with session.request('GET' if payload is None else 'POST', url,
                json=payload, headers=headers, allow_redirects=False) as response:
            trace = TRACE.get()
            if trace is not None:
                trace['upstream_http_status'] = response.status
            if response.status != 200:
                raw = await response.content.read(8192)
                try:
                    if trace is not None:
                        trace['upstream_error_code'] = upstream_code(json.loads(raw))
                except (ValueError, UnicodeError):
                    pass
                raise ValueError('Upstream status')
            raw = bytearray()
            async for chunk in response.content.iter_chunked(16384):
                raw.extend(chunk)
                if len(raw) > 1024 * 1024:
                    raise ValueError('Upstream response size')
            return json.loads(raw)

    async def read_audio(self, request):
        entry = self.audio.get(request.match_info['token'])
        if not entry or entry[1] <= time.monotonic():
            raise web.HTTPNotFound()
        self.key()
        return web.Response(body=entry[0], content_type='audio/wav',
                            headers={'Cache-Control': 'no-store'})

    async def transcribe(self, request):
        self.authorize(request)
        if self.active >= 2:
            raise web.HTTPTooManyRequests()
        self.active += 1
        token = None
        timer = None
        try:
            async with async_timeout.timeout(75):
                stage('upload')
                # Read each multipart part with a bound, including unknown fields.
                reader = await request.multipart()
                fields = {}
                total = 0
                async for part in reader:
                    if part.name == 'file' and part.filename:
                        match = re.search(r'asr_[A-Za-z0-9]+_([0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12})_', part.filename)
                        if match and TRACE.get() is not None:
                            TRACE.get()['upload_session_id'] = match[1]
                    data = bytearray()
                    while True:
                        chunk = await part.read_chunk()
                        if not chunk:
                            break
                        total += len(chunk)
                        if total > MAX_AUDIO or part.name in fields:
                            raise web.HTTPBadRequest(text='Invalid audio upload')
                        data.extend(chunk)
                    fields[part.name] = bytes(data)
                if fields.get('model', b'').decode() != ASR or 'file' not in fields:
                    raise web.HTTPBadRequest(text='Unsupported transcription request')
                wav = fields['file']
                stage('wav_validation')
                with wave.open(io.BytesIO(wav), 'rb') as audio:
                    if (audio.getnchannels() != 1 or audio.getsampwidth() != 2
                            or audio.getframerate() != 16000 or audio.getcomptype() != 'NONE'
                            or not 0 < audio.getnframes() <= 16000 * 60
                            or len(audio.readframes(audio.getnframes())) != audio.getnframes() * 2):
                        raise web.HTTPBadRequest(text='Expected mono 16kHz PCM WAV, at most 60s')
                token = secrets.token_urlsafe(32)
                self.audio[token] = (wav, time.monotonic() + 75)
                timer = asyncio.get_running_loop().call_later(75, self.audio.pop, token, None)
                url = self.config['public_origin'].rstrip('/') + AUDIO_PATH + token + '.wav'
                timeout = aiohttp.ClientTimeout(total=10)
                async with aiohttp.ClientSession(timeout=timeout, trust_env=False) as session:
                    stage('asr_submit')
                    self.stats['asr_submitted'] += 1
                    submitted = await self.json_request(session, self.market + 'services/audio/asr/transcription',
                        {'model': ASR, 'input': {'file_urls': [url]}, 'parameters': {}}, self.key())
                    task = submitted.get('output', {}).get('task_id', '')
                    if not re.fullmatch(r'[A-Za-z0-9_-]{1,128}', task):
                        raise ValueError('Invalid task')
                    while True:
                        stage('asr_poll')
                        result = await self.json_request(session, self.market + 'tasks/' + task, key=self.key())
                        self.key()
                        output = result.get('output', {})
                        status = output.get('task_status')
                        if status == 'SUCCEEDED':
                            break
                        if status not in ('PENDING', 'RUNNING'):
                            if TRACE.get() is not None:
                                TRACE.get()['upstream_error_code'] = upstream_code(output) or upstream_code(result)
                            raise ValueError('Transcription failed')
                        await asyncio.sleep(1)
                    results = output.get('results', [])
                    if len(results) != 1 or results[0].get('subtask_status') != 'SUCCEEDED':
                        if TRACE.get() is not None and len(results) == 1:
                            TRACE.get()['upstream_error_code'] = upstream_code(results[0])
                        raise ValueError('Transcription subtask failed')
                    result_url = results[0].get('transcription_url', '')
                    parsed, origin = urlsplit(result_url), urlsplit(self.market)
                    same_origin = (parsed.scheme, parsed.netloc) == (origin.scheme, origin.netloc)
                    official = parsed.scheme == 'https' and parsed.hostname in (
                        'dashscope-result-bj.oss-cn-beijing.aliyuncs.com',
                        'dashscope-result-sg.oss-ap-southeast-1.aliyuncs.com')
                    if parsed.username or parsed.password or parsed.fragment or not (same_origin or official):
                        raise ValueError('Invalid result origin')
                    # Capability result URL must never receive the account key.
                    stage('asr_result_download')
                    result = await self.json_request(session, result_url)
                    self.key()
                    text = ''.join(t['text'] for t in result.get('transcripts', [])
                                   if t.get('channel_id', 0) == 0 and isinstance(t.get('text'), str)).strip()
                    if not text or len(text) > 16000:
                        raise ValueError('Invalid transcript')
                    self.stats['asr_succeeded'] += 1
                    stage('asr_complete')
                    return web.json_response({'text': text})
        except web.HTTPException:
            raise
        except Exception as error:
            if TRACE.get() is not None:
                TRACE.get()['error_type'] = type(error).__name__
            self.stats['failures'] += 1
            raise web.HTTPBadGateway(text='Transcription unavailable') from None
        finally:
            self.active -= 1
            if timer:
                timer.cancel()
            if token:
                self.audio.pop(token, None)

    async def chat(self, request):
        self.authorize(request)
        if self.active >= 2:
            raise web.HTTPTooManyRequests()
        self.active += 1
        output = None
        try:
            stage('llm_input')
            body = await request.json()
            if body.get('model') != LLM or body.get('stream') is not True:
                raise web.HTTPBadRequest(text='Unsupported model request')
            # Bounded test usage and audible answers, not reasoning-only output.
            body.update(max_tokens=256, enable_thinking=False)
            body.pop('thinking', None)
            async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=60), trust_env=False) as session:
                stage('llm_submit')
                self.stats['llm_submitted'] += 1
                async with session.post(self.market + 'chat/completions', json=body,
                        headers={'Authorization': 'Bearer ' + self.key()}, allow_redirects=False) as upstream:
                    if TRACE.get() is not None:
                        TRACE.get()['upstream_http_status'] = upstream.status
                    if upstream.status != 200:
                        raw = await upstream.content.read(8192)
                        try:
                            if TRACE.get() is not None:
                                TRACE.get()['upstream_error_code'] = upstream_code(json.loads(raw))
                        except (ValueError, UnicodeError):
                            pass
                        raise ValueError('Model request failed')
                    output = web.StreamResponse(headers={'Content-Type': 'text/event-stream', 'Cache-Control': 'no-store'})
                    await output.prepare(request)
                    size = 0
                    async for chunk in upstream.content.iter_chunked(16384):
                        if size == 0 and TRACE.get() is not None:
                            TRACE.get()['first_stream_chunk_ms'] = round((time.monotonic() - TRACE.get()['_start']) * 1000, 2)
                        self.key()
                        size += len(chunk)
                        if size > 1024 * 1024:
                            raise ValueError('Model response size')
                        await output.write(chunk)
                    await output.write_eof()
                    self.stats['llm_succeeded'] += 1
                    stage('llm_complete')
                    return output
        except web.HTTPException:
            raise
        except Exception as error:
            if TRACE.get() is not None:
                TRACE.get()['error_type'] = type(error).__name__
            self.stats['failures'] += 1
            if output is not None:
                if request.transport:
                    request.transport.close()
                return output
            raise web.HTTPBadGateway(text='Model unavailable') from None
        finally:
            self.active -= 1

    async def health(self, request):
        self.authorize(request)
        return web.json_response({**self.stats, 'active': self.active, 'published_audio': len(self.audio)})

    def application(self):
        app = web.Application(client_max_size=MAX_AUDIO, middlewares=[self.audit])
        app.router.add_post('/v1/audio/transcriptions', self.transcribe)
        app.router.add_post('/v1/chat/completions', self.chat)
        app.router.add_get(AUDIO_PATH + r'{token:[A-Za-z0-9_-]{43}}.wav', self.read_audio)
        app.router.add_get('/health', self.health)
        return app


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True)
    parser.add_argument('--lease', required=True)
    parser.add_argument('--port', type=int, default=8050)
    args = parser.parse_args()
    config = json.loads(Path(args.config).read_text())
    origin = urlsplit(config['public_origin'])
    assert origin.scheme == 'https' and origin.hostname and not origin.username
    assert not origin.password and not origin.query and not origin.fragment and origin.path in ('', '/')
    assert len(config['bridge_token']) >= 32
    web.run_app(Bridge(config, args.lease).application(), port=args.port, access_log=None,
                handler_cancellation=True)
