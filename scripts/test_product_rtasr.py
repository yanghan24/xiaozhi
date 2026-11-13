"""Product ASR contract tests over real HTTP/WebSocket transports."""
import asyncio
import json
import sys
import tempfile
import unittest

sys.path.insert(0, '/app')
from aiohttp import web, WSMsgType
from core.product_voice import VoiceAuthority, VoiceDenied
from core.product_asr import AudioPublication, MarketASR, TranscriptionUnavailable


def result(text, final=False, segment=0, kind='0'):
    return {'msg_type': 'result', 'res_type': 'asr', 'data': {
        'seg_id': segment, 'ls': final,
        'cn': {'st': {'type': kind, 'rt': [{'ws': [{'cw': [{'w': text}]}]}]}}}}


class ASRContract(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.allowed = True
        self.received = bytearray()
        self.requests = 0
        self.connected = asyncio.Event()
        self.closed = asyncio.Event()
        self.mode = 'success'
        self.authority_latency = 0
        self.end_received = asyncio.Event()
        self.tmp = tempfile.TemporaryDirectory()
        app = web.Application()
        app.router.add_post('/internal/device-voice/v1/{action}', self.authority_request)
        app.router.add_get('/v1/asr/xfyun/rtasr-llm', self.market)
        self.runner = web.AppRunner(app)
        await self.runner.setup()
        site = web.TCPSite(self.runner, '127.0.0.1', 0)
        await site.start()
        self.base = 'http://127.0.0.1:' + str(site._server.sockets[0].getsockname()[1])
        self.authority = VoiceAuthority(self.base, 'synthetic-service-key-1234567890123456', self.tmp.name)
        self.gate = await self.authority.open({'Device-Id': 'test-device', 'Authorization': 'Bearer ' + 't'*43}, '/xiaozhi/v1/')
        self.provider = MarketASR(self.gate, AudioPublication(self.base))

    async def asyncTearDown(self):
        self.provider.cancel()
        await self.runner.cleanup()
        # Outbox workers are daemon threads; keep their private directory until process exit.
        self.tmp._finalizer.detach()

    async def authority_request(self, request):
        await asyncio.sleep(self.authority_latency)
        action = request.match_info['action']
        if action == 'key-rejected':
            self.allowed = False
            return web.json_response({'recorded': True})
        return web.json_response({
            'session': 's'*43, 'permission_version': 1, 'allowed': self.allowed,
            'authorization_scope': 'a'*43, 'authorization_subject': 'b'*64,
            'permission_authorization_order': 1, 'authorization_order': 1,
            'model': 'qwen3.8-flash', 'api_key': 'synthetic-market-key',
            'base_url': self.base + '/v1/', 'rejection_receipt': 'synthetic-receipt'})

    async def market(self, request):
        self.requests += 1
        self.assertEqual(request.headers.get('Authorization'), 'Bearer synthetic-market-key')
        self.assertEqual(dict(request.query), {'model': 'rtasr-llm', 'lang': 'autodialect',
                                             'audio_encode': 'pcm_s16le', 'samplerate': '16000'})
        if self.mode in ('401', '403', '500'):
            return web.Response(status=int(self.mode))
        if self.mode == 'redirect':
            return web.Response(status=302, headers={'Location': self.base + '/forbidden'})
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self.connected.set()
        try:
            await ws.send_json({'msg_type': 'action', 'data': {'sessionId': 'upstream-session'}})
            async for message in ws:
                if message.type == WSMsgType.BINARY:
                    self.received.extend(message.data)
                elif message.type == WSMsgType.TEXT:
                    self.assertEqual(json.loads(message.data), {'end': True, 'sessionId': 'upstream-session'})
                    self.end_received.set()
                    if self.mode == 'pending':
                        continue
                    if self.mode == 'disconnect':
                        await ws.send_json(result('未完成', kind='1'))
                        await ws.close()
                        break
                    if self.mode == 'error':
                        await ws.send_json({'msg_type': 'result', 'res_type': 'frc', 'data': {'normal': False}})
                        continue
                    if self.mode == 'empty':
                        await ws.send_json(result('', final=True))
                        continue
                    if self.mode == 'partial-final':
                        await ws.send_json(result('不是最终文本', final=True, kind='1'))
                        continue
                    if self.mode == 'native-sequence':
                        # Live rtasr-llm increments seg_id for each interim message too.
                        for event in (result('你', segment=0, kind='1'), result('你好，', segment=1),
                                      result('世', segment=2, kind='1'), result('世界', segment=3),
                                      result('。', segment=4, final=True)):
                            await ws.send_json(event)
                        continue
                    await ws.send_json(result('错误中间文本', kind='1'))
                    await ws.send_json(result('你好，'))
                    last = result('世界。', final=True, segment=1)
                    if self.mode == 'string-data':
                        last = {'action': 'result', 'code': '0', 'data': json.dumps(last['data'])}
                    await ws.send_json(last)
        finally:
            self.closed.set()
        return ws

    async def test_pcm_transcribes_final_segments_without_duplicate_partials(self):
        pcm = b'\x01\x00' * 1280
        self.assertEqual(await self.provider.speech_to_text([pcm], 'test'), ('你好，世界。', None))
        self.assertEqual(bytes(self.received), pcm)
        self.assertEqual(self.requests, 1)
        self.assertFalse(self.provider.publication.files)
        await asyncio.wait_for(self.closed.wait(), 1)

    async def test_string_result_envelope(self):
        self.mode = 'string-data'
        self.assertEqual(await self.provider.speech_to_text([b'\0'*1280], 'test'), ('你好，世界。', None))

    async def test_native_sequence_numbers_include_interim_messages(self):
        self.mode = 'native-sequence'
        self.assertEqual(await self.provider.speech_to_text([b'\0'*1280], 'test'), ('你好，世界。', None))

    async def test_permission_latency_does_not_accumulate_on_top_of_audio_duration(self):
        self.authority_latency = .04
        # Allow scheduler/SQLite overhead in the real gate as well as 2 s audio;
        # the old extra 40 ms sleep per frame alone pushes this above 4 s.
        self.provider.TIMEOUT = 3.6
        self.assertEqual(await self.provider.speech_to_text([b'\0'*64000], 'test'), ('你好，世界。', None))

    async def test_business_failures_do_not_revoke_or_retry(self):
        for mode in ('403', '500', 'redirect', 'error', 'empty', 'partial-final', 'disconnect'):
            with self.subTest(mode=mode):
                self.mode = mode
                before = self.requests
                with self.assertRaises(TranscriptionUnavailable):
                    await self.provider.speech_to_text([b'\0'*1280], 'test')
                self.assertEqual(self.requests, before + 1)
                await self.gate.check_async()

    async def test_401_revokes_other_connection(self):
        other = await self.authority.open({'Device-Id': 'test-device', 'Authorization': 'Bearer ' + 't'*43}, '/xiaozhi/v1/')
        self.mode = '401'
        with self.assertRaises(VoiceDenied):
            await self.provider.speech_to_text([b'\0'*1280], 'test')
        with self.assertRaises(VoiceDenied):
            await other.check_async()
        self.assertEqual(self.requests, 1)

    async def test_cancel_closes_upstream_and_allows_next_utterance(self):
        self.mode = 'pending'
        task = asyncio.create_task(self.provider.speech_to_text([b'\0'*1280], 'test'))
        await asyncio.wait_for(self.end_received.wait(), 2)
        self.provider.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        await asyncio.wait_for(self.closed.wait(), 1)
        self.mode = 'success'
        self.assertEqual(await self.provider.speech_to_text([b'\0'*1280], 'next'), ('你好，世界。', None))

    async def test_silent_upstream_stops_when_permission_lost(self):
        self.mode = 'pending'
        task = asyncio.create_task(self.provider.speech_to_text([b'\0'*1280], 'test'))
        await asyncio.wait_for(self.end_received.wait(), 2)
        self.allowed = False
        with self.assertRaises(VoiceDenied):
            await asyncio.wait_for(task, 1)
        await asyncio.wait_for(self.closed.wait(), 1)

    async def test_timeout_closes_without_retry(self):
        self.mode = 'pending'
        self.provider.TIMEOUT = .3
        with self.assertRaises(TranscriptionUnavailable):
            await self.provider.speech_to_text([b'\0'*1280], 'test')
        await asyncio.wait_for(self.closed.wait(), 1)
        self.assertEqual(self.requests, 1)

    async def test_empty_odd_and_oversized_audio_never_connect(self):
        for pcm in ([], [b''], [b'1'], [b'\0'*(60*32000+2)]):
            with self.assertRaises(TranscriptionUnavailable):
                await self.provider.speech_to_text(pcm, 'test')
        self.assertEqual(self.requests, 0)


if __name__ == '__main__':
    unittest.main()
