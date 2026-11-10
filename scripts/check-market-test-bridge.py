#!/usr/bin/env python3
"""HTTP integration checks, run inside the Ubuntu voice image without paid calls."""
import asyncio
import contextlib
import importlib.util
import io
import json
import tempfile
import time
import unittest
import wave
from pathlib import Path

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

spec = importlib.util.spec_from_file_location('bridge', Path(__file__).with_name('market-test-bridge.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Tests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.lease = Path(self.directory.name) / 'lease.json'
        self.lease.write_text(json.dumps({'api_key': 'private-account-key', 'expires_at': time.time() + 300}))
        self.audio_url = None
        self.submissions = 0
        self.mode = 'success'
        self.chat_chunks = None
        self.result_authorization = None
        app = web.Application()
        app.router.add_post('/v1/services/audio/asr/transcription', self.submit)
        app.router.add_get('/v1/tasks/task1', self.task)
        app.router.add_get('/result', self.result)
        app.router.add_post('/v1/chat/completions', self.chat)
        self.market = TestServer(app)
        await self.market.start_server()
        config = {'bridge_token': 'test-bridge-token', 'expires_at': time.time() + 300, 'public_origin': ''}
        self.bridge = module.Bridge(config, self.lease, str(self.market.make_url('/v1/')))
        self.client = TestClient(TestServer(self.bridge.application()))
        await self.client.start_server()
        config['public_origin'] = str(self.client.make_url('')).rstrip('/')
        self.headers = {'Authorization': 'Bearer test-bridge-token'}

    async def asyncTearDown(self):
        await self.client.close()
        await self.market.close()
        self.directory.cleanup()

    async def submit(self, request):
        self.assertEqual(request.headers['Authorization'], 'Bearer private-account-key')
        body = await request.json()
        self.assertEqual(body['model'], module.ASR)
        self.submissions += 1
        if self.mode == 'http_error':
            return web.json_response({'code': 'InvalidParameter', 'message': 'secret-message-private-account-key'}, status=400)
        self.audio_url = body['input']['file_urls'][0]
        async with aiohttp.ClientSession() as session:
            async with session.get(self.audio_url) as audio:
                self.assertEqual(audio.status, 200)
                self.assertEqual(audio.headers['Cache-Control'], 'no-store')
                with wave.open(io.BytesIO(await audio.read())) as wav:
                    self.assertEqual(wav.getnframes(), 1600)
        if self.mode == 'redirect':
            raise web.HTTPFound('/forbidden')
        if self.mode == 'expire':
            self.lease.write_text(json.dumps({'api_key': 'private-account-key', 'expires_at': 0}))
        return web.json_response({'output': {'task_id': 'task1'}})

    async def task(self, request):
        status = 'FAILED' if self.mode == 'subtask_failure' else 'SUCCEEDED'
        url = 'https://untrusted.invalid/result' if self.mode == 'bad_origin' else str(self.market.make_url('/result'))
        return web.json_response({'output': {'task_status': 'SUCCEEDED', 'results': [
            {'subtask_status': status, 'code': 'FILE_DOWNLOAD_FAILED' if status == 'FAILED' else None, 'transcription_url': url}]}})

    async def result(self, request):
        self.result_authorization = request.headers.get('Authorization')
        return web.json_response({'transcripts': [{'channel_id': 0, 'text': 'test words'}]})

    async def chat(self, request):
        self.assertEqual(request.headers['Authorization'], 'Bearer private-account-key')
        body = await request.json()
        self.assertFalse(body['enable_thinking'])
        self.assertEqual(body['max_tokens'], 256)
        if self.mode in ('llm_http_error', 'llm_large_http_error'):
            message = 'secret-message-private-account-key'
            if self.mode == 'llm_large_http_error':
                message *= 1024
            return web.json_response({'error': {'code': 'InvalidParameter', 'message': message}}, status=400)
        if self.chat_chunks is not None:
            response = web.StreamResponse(headers={'Content-Type': 'text/event-stream'})
            await response.prepare(request)
            try:
                for chunk in self.chat_chunks:
                    await response.write(chunk)
                    await asyncio.sleep(0)
                await response.write_eof()
            except ConnectionResetError:
                pass  # The bridge aborts an invalid upstream stream.
            return response
        return web.Response(text='data: {"choices":[{"delta":{"content":"hello"}}]}\n\ndata: [DONE]\n\n',
                            content_type='text/event-stream')

    async def chat_with_audit(self):
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            response = await self.client.post('/v1/chat/completions', headers=self.headers,
                json={'model': module.LLM, 'stream': True})
            try:
                body = await response.read()
            except aiohttp.ClientPayloadError:
                body = None
        health = await self.client.get('/health', headers=self.headers)
        return response.status, body, json.loads(captured.getvalue()), captured.getvalue(), await health.json()

    def form(self, audio=None):
        if audio is None:
            output = io.BytesIO()
            with wave.open(output, 'wb') as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(16000)
                wav.writeframes(b'\0' * 3200)
            audio = output.getvalue()
        form = aiohttp.FormData()
        form.add_field('model', module.ASR)
        form.add_field('file', audio, filename='recording.wav', content_type='audio/wav')
        return form

    async def test_transcription_and_capability_cleanup(self):
        response = await self.client.post('/v1/audio/transcriptions', headers=self.headers, data=self.form())
        self.assertEqual(response.status, 200)
        self.assertEqual((await response.json())['text'], 'test words')
        self.assertIsNone(self.result_authorization)
        self.assertEqual(self.submissions, 1)
        self.assertFalse(self.bridge.audio)
        async with aiohttp.ClientSession() as session:
            async with session.get(self.audio_url) as response:
                self.assertEqual(response.status, 404)

    async def test_failure_no_retry_or_retained_audio(self):
        for mode in ['subtask_failure', 'bad_origin', 'redirect']:
            self.mode = mode
            before = self.submissions
            response = await self.client.post('/v1/audio/transcriptions', headers=self.headers, data=self.form())
            self.assertEqual(response.status, 502, mode)
            self.assertEqual(self.submissions, before + 1)
            self.assertFalse(self.bridge.audio)

    async def test_lease_expiry_stops_pending_work(self):
        self.mode = 'expire'
        response = await self.client.post('/v1/audio/transcriptions', headers=self.headers, data=self.form())
        self.assertEqual(response.status, 503)
        self.assertFalse(self.bridge.audio)
        response = await self.client.post('/v1/chat/completions', headers=self.headers,
            json={'model': module.LLM, 'stream': True})
        self.assertEqual(response.status, 503)
        self.assertEqual(self.bridge.stats['llm_submitted'], 0)

    async def test_auth_and_invalid_audio_do_not_call_market(self):
        response = await self.client.post('/v1/audio/transcriptions', data=self.form())
        self.assertEqual(response.status, 401)
        response = await self.client.post('/v1/audio/transcriptions', headers=self.headers, data=self.form(b'bad'))
        self.assertEqual(response.status, 502)
        self.assertEqual(self.submissions, 0)

    async def test_streaming_model(self):
        response = await self.client.post('/v1/chat/completions', headers=self.headers,
            json={'model': module.LLM, 'stream': True, 'messages': [{'role': 'user', 'content': 'test'}]})
        self.assertEqual(response.status, 200)
        self.assertIn('[DONE]', await response.text())
        self.assertEqual(self.bridge.stats['llm_succeeded'], 1)

    async def test_stream_errors_are_failures(self):
        error = b'data: {"error":{"code":"ModelError","message":"secret-message-private-account-key"}}\n\n'
        valid = b'data: {"choices":[{"delta":{"content":"hello"}}]}\n\n'
        cases = [
            ([error, b'data: [DONE]\n\n'], 'ModelError'),
            ([b'event: error\ndata: {"code":"QuotaExceeded","message":"secret-message"}\n\n'], 'QuotaExceeded'),
            ([b'event: error\ndata: secret-message\n\n'], None),
            ([b'data: {"code":"InvalidParameter","message":"secret-message"}\n\n'], 'InvalidParameter'),
            ([valid, error], 'ModelError'),
            ([b'\xef\xbb\xbf' + error], 'ModelError'),
            ([b'data: not-json\n\n'], None),
        ]
        for index, (chunks, code) in enumerate(cases, 1):
            with self.subTest(case=index):
                self.chat_chunks = chunks
                status, _, record, raw, health = await self.chat_with_audit()
                self.assertEqual(status, 200)  # Streaming headers were already sent.
                self.assertEqual(health['llm_succeeded'], 0)
                self.assertEqual(health['failures'], index)
                self.assertEqual(health['active'], 0)
                self.assertEqual(record['stage'], 'llm_stream')
                self.assertEqual(record['upstream_http_status'], 200)
                self.assertEqual(record['upstream_error_code'], code)
                self.assertEqual(record['error_type'], 'ModelStreamError')
                self.assertNotIn('secret-message', raw)
                self.assertNotIn('private-account-key', raw)

    async def test_stream_ending_without_done_is_failure(self):
        cases = [
            b'data: {"choices":[{"delta":{"content":"hello"}}]}\n\n',
            b'data: {"choices":[{"finish_reason":"stop","delta":{}}]}\n\n',
            b'data: {"choices":',
            b'data: [DONE]',
            b': keepalive\n\n',
            b'',
        ]
        for index, payload in enumerate(cases, 1):
            with self.subTest(case=index):
                self.chat_chunks = [payload] if payload else []
                _, _, record, _, health = await self.chat_with_audit()
                self.assertEqual(health['llm_succeeded'], 0)
                self.assertEqual(health['failures'], index)
                self.assertEqual(health['active'], 0)
                self.assertEqual(record['stage'], 'llm_stream')
                self.assertEqual(record['error_type'], 'IncompleteModelStream')

    async def test_fragmented_stream_is_forwarded_intact(self):
        for index, newline in enumerate((b'\n', b'\r\n', b'\r'), 1):
            with self.subTest(newline=newline):
                payload = newline.join([
                    b': keepalive', b'',
                    b'event: message', b'id: 1',
                    b'data: {"choices":[',
                    'data: {"delta":{"content":"你好"}}]}'.encode(), b'',
                    b'data: [DONE]', b'', b'',
                ])
                self.chat_chunks = [bytes([byte]) for byte in payload]
                status, body, record, raw, health = await self.chat_with_audit()
                self.assertEqual(status, 200)
                self.assertEqual(body, payload)
                self.assertEqual(health['llm_succeeded'], index)
                self.assertEqual(health['failures'], 0)
                self.assertEqual(record['stage'], 'llm_complete')
                self.assertIsNone(record['error_type'])
                self.assertEqual(record['upstream_http_status'], 200)
                self.assertNotIn('你好', raw)

    async def test_http_model_errors_are_audited(self):
        for index, mode in enumerate(('llm_http_error', 'llm_large_http_error'), 1):
            with self.subTest(mode=mode):
                self.mode = mode
                status, _, record, raw, health = await self.chat_with_audit()
                self.assertEqual(status, 502)
                self.assertEqual(health['llm_succeeded'], 0)
                self.assertEqual(health['failures'], index)
                self.assertEqual(record['upstream_http_status'], 400)
                self.assertEqual(record['upstream_error_code'], 'InvalidParameter' if index == 1 else None)
                self.assertNotIn('secret-message', raw)
                self.assertNotIn('private-account-key', raw)
                self.assertLess(len(raw), 2048)

    async def test_failure_audit_is_correlated_bounded_and_redacted(self):
        records = []
        for mode, expected_stage, status, code in [
                ('http_error', 'asr_submit', 400, 'InvalidParameter'),
                ('subtask_failure', 'asr_poll', 200, 'FILE_DOWNLOAD_FAILED')]:
            self.mode = mode
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured):
                response = await self.client.post('/v1/audio/transcriptions', headers=self.headers, data=self.form())
                await response.read()
            self.assertEqual(response.status, 502)
            record = json.loads(captured.getvalue())
            records.append(record)
            self.assertEqual(record['stage'], expected_stage)
            self.assertEqual(record['upstream_http_status'], status)
            self.assertEqual(record['upstream_error_code'], code)
            self.assertEqual(record['bridge_http_status'], 502)
            self.assertGreater(record['elapsed_ms'], 0)
            self.assertIn('started_at_utc', record)
            self.assertIn('finished_at_utc', record)
            for secret in ['private-account-key', 'test-bridge-token', 'secret-message', 'file_urls', 'transcription_url', 'recording.wav']:
                self.assertNotIn(secret, captured.getvalue())
        self.assertNotEqual(records[0]['request_id'], records[1]['request_id'])


if __name__ == '__main__':
    unittest.main()
