import tempfile
import unittest
import asyncio
import json
import threading
import time
from types import SimpleNamespace
from aiohttp import ClientSession
import websockets
from core.product_ota_bridge import ProductOtaBridge
from core.product_voice import GuardedSocket


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.bridge = ProductOtaBridge({}, self.temp.name, 'k' * 32, 'https://firmware.example')
        await self.bridge.start('127.0.0.1', 0)
        self.url = 'http://127.0.0.1:%s/internal/product-ota/v1/control' % self.bridge.port
        self.http = ClientSession()

    async def asyncTearDown(self):
        await self.http.close()
        await self.bridge.close()

    async def test_private_http_requires_separate_service_key(self):
        for headers in ({}, {'X-OTA-Service-Key': 'wrong'}):
            async with self.http.post(self.url, json={'action': 'status', 'device_id': 'B'}, headers=headers) as r:
                self.assertEqual(401, r.status)
        async with self.http.post(self.url, json={'action': 'status', 'device_id': 'B'}, headers={'X-OTA-Service-Key': 'k' * 32}) as r:
            self.assertEqual(200, r.status)
            self.assertEqual('offline', (await r.json())['status'])

    async def post(self, body):
        async with self.http.post(self.url, json=body, headers={'X-OTA-Service-Key': 'k' * 32}) as r:
            return r.status, await r.json()

    def device(self):
        self.frames = []
        self.pending = {}
        self.sequence = 0
        self.revoked = False
        self.lose_result = False
        async def check():
            if self.revoked:
                raise PermissionError()
        async def ready(): return True
        async def next_id():
            self.sequence += 1
            return self.sequence
        async def register(i, future): self.pending[i] = future
        async def cleanup(i): self.pending.pop(i, None)
        async def send(raw):
            frame = json.loads(raw)['payload']
            self.frames.append(frame)
            if frame['method'] == 'tools/list':
                result = {'tools': [{'name': 'self.upgrade_firmware'}, {'name': 'self.get_system_info'}]}
            elif frame['params']['name'] == 'self.get_system_info':
                result = {'content': [{'type': 'text', 'text': json.dumps({'flash_size': 33554432,
                    'chip_model_name': 'esp32p4', 'chip_info': {'model': 18, 'revision': 100},
                    'board': {'type': 'waveshare-esp32-p4-wifi6-touch-lcd-4.3', 'ssid': 'secret'},
                    'application': {'version': '2.2.6', 'elf_sha256': 'b' * 64},
                    'partition_table': [{'label': 'ota_0', 'type': 0, 'subtype': 16, 'address': 131072, 'size': 4128768}]})}]}
            else:
                if self.lose_result: raise ConnectionError('private url')
                result = {'content': [{'type': 'text', 'text': 'true'}]}
            self.pending[frame['id']].set_result(result)
        gate = SimpleNamespace(version=17, check_async=check, ended=threading.Event())
        client = SimpleNamespace(is_ready=ready, get_next_id=next_id, register_call_result_future=register, cleanup_call_result=cleanup)
        self.bridge.sessions['B'] = SimpleNamespace(device_id='B', session_id='session', product_voice_gate=gate,
            mcp_client=client, websocket=GuardedSocket(SimpleNamespace(send=send), gate))

    def dispatch(self, operation='a' * 32):
        return {'action': 'dispatch', 'device_id': 'B', 'session_id': 'session', 'permission_version': 17,
            'operation_id': operation, 'artifact_sha256': 'c' * 64,
            'target_elf_sha256': 'b' * 64, 'target_version': '2.2.6',
            'url': 'https://firmware.example/xiaozhi/ota-artifacts/' + operation + '/' + 'c' * 64
                + '.bin?expires=' + str(int(time.time()) + 600) + '&signature=' + 'd' * 64}

    async def test_inspect_is_guarded_and_only_exposes_compatibility_fields(self):
        self.device()
        code, value = await self.post({'action': 'inspect', 'device_id': 'B', 'session_id': 'session', 'permission_version': 17})
        self.assertEqual(200, code)
        self.assertEqual('ready', value['status'])
        self.assertEqual('b' * 64, value['system_info']['application']['elf_sha256'])
        self.assertNotIn('secret', json.dumps(value))

    async def test_dispatch_is_persisted_and_never_replayed_after_bridge_restart(self):
        self.device()
        request = self.dispatch()
        code, value = await self.post(request)
        self.assertEqual('accepted_unconfirmed', value['status'])
        sessions = self.bridge.sessions
        await self.bridge.close()
        self.bridge = ProductOtaBridge(sessions, self.temp.name, 'k' * 32, 'https://firmware.example')
        await self.bridge.start('127.0.0.1', 0)
        self.url = 'http://127.0.0.1:%s/internal/product-ota/v1/control' % self.bridge.port
        self.assertEqual(value, (await self.post(request))[1])
        self.assertEqual('not_sent', (await self.post(self.dispatch('e' * 32)))[1]['status'])
        self.assertEqual(1, sum(f.get('params', {}).get('name') == 'self.upgrade_firmware' for f in self.frames))

    async def test_unknown_transport_is_preserved_when_new_session_observes_target(self):
        self.device()
        self.lose_result = True
        self.assertEqual('result_unknown', (await self.post(self.dispatch()))[1]['status'])
        reconcile = {'action': 'reconcile', 'device_id': 'B', 'operation_id': 'a' * 32,
                     'session_id': 'session', 'permission_version': 17}
        self.assertNotIn('observation_status', (await self.post(reconcile))[1])
        self.bridge.sessions['B'].session_id = 'new-session'
        reconcile['session_id'] = 'new-session'
        result = (await self.post(reconcile))[1]
        self.assertEqual('result_unknown', result['status'])
        self.assertEqual('target_running', result['observation_status'])
        self.assertEqual(result, (await self.post({'action': 'operation', 'device_id': 'B', 'operation_id': 'a' * 32}))[1])
        self.lose_result = False
        next_request = self.dispatch('e' * 32)
        next_request['session_id'] = 'new-session'
        self.assertEqual('accepted_unconfirmed', (await self.post(next_request))[1]['status'])

    async def test_rejects_arbitrary_rpc_url_duplicate_json_and_large_body(self):
        self.device()
        invalid = self.dispatch()
        invalid['url'] = invalid['url'].replace('firmware.example', 'evil.example')
        self.assertEqual(400, (await self.post(invalid))[0])
        invalid = self.dispatch()
        invalid['method'] = 'tools/call'
        self.assertEqual(400, (await self.post(invalid))[0])
        for raw in ('{"action":"status","action":"dispatch","device_id":"B"}', ' ' * 9000):
            async with self.http.post(self.url, data=raw, headers={'X-OTA-Service-Key': 'k' * 32, 'Content-Type': 'application/json'}) as r:
                self.assertEqual(400, r.status)
        async with self.http.post(self.url, json={'action': 'status', 'device_id': 'B'},
                                  headers=[('X-OTA-Service-Key', 'k' * 32), ('X-OTA-Service-Key', 'k' * 32)]) as r:
            self.assertEqual(401, r.status)
        self.assertFalse(self.frames)

    async def test_stale_session_revoked_permission_and_other_device_never_receive_upgrade(self):
        self.device()
        for changes in ({'session_id': 'old'}, {'permission_version': 16}, {'device_id': 'A'}):
            request = self.dispatch()
            request.update(changes)
            self.assertEqual('not_sent', (await self.post(request))[1]['status'])
        self.revoked = True
        self.assertEqual('permission_unavailable', (await self.post({'action': 'status', 'device_id': 'B'}))[1]['status'])
        self.assertEqual('not_sent', (await self.post(self.dispatch()))[1]['status'])
        self.assertFalse(self.frames)

    async def test_session_replaced_during_final_guard_never_receives_upgrade(self):
        self.device()
        socket = self.bridge.sessions['B'].websocket
        original = socket.send
        async def send(raw, **kwargs):
            frame = json.loads(raw)['payload']
            if frame.get('params', {}).get('name') == 'self.upgrade_firmware':
                self.bridge.sessions['B'].session_id = 'replacement'
            return await original(raw, **kwargs)
        socket.send = send
        self.assertEqual('result_unknown', (await self.post(self.dispatch()))[1]['status'])
        self.assertFalse(any(f.get('params', {}).get('name') == 'self.upgrade_firmware' for f in self.frames))

    async def test_conflicting_identity_and_unknown_restart_cannot_resend(self):
        self.device()
        self.lose_result = True
        request = self.dispatch()
        self.assertEqual('result_unknown', (await self.post(request))[1]['status'])
        self.lose_result = False
        request['target_elf_sha256'] = 'f' * 64
        self.assertEqual('operation_conflict', (await self.post(request))[1]['reason'])
        request['target_elf_sha256'] = 'b' * 64
        self.assertEqual('result_unknown', (await self.post(request))[1]['status'])
        self.assertEqual(1, sum(f.get('params', {}).get('name') == 'self.upgrade_firmware' for f in self.frames))

    async def test_no_unique_or_initializing_session_is_not_pushable(self):
        self.device()
        self.bridge.sessions['duplicate'] = self.bridge.sessions['B']
        self.assertEqual('no_unique_session', (await self.post({'action': 'status', 'device_id': 'B'}))[1]['status'])
        self.assertEqual('not_sent', (await self.post(self.dispatch()))[1]['status'])
        del self.bridge.sessions['duplicate']
        async def not_ready(): return False
        self.bridge.sessions['B'].mcp_client.is_ready = not_ready
        self.assertEqual('mcp_initializing', (await self.post({'action': 'status', 'device_id': 'B'}))[1]['status'])
        self.assertEqual('not_sent', (await self.post(self.dispatch()))[1]['status'])
        self.assertFalse(self.frames)

    async def test_wrong_observed_artifact_does_not_release_device(self):
        self.device()
        request = self.dispatch()
        request['target_elf_sha256'] = 'f' * 64
        self.assertEqual('accepted_unconfirmed', (await self.post(request))[1]['status'])
        self.bridge.sessions['B'].session_id = 'new-session'
        result = (await self.post({'action': 'reconcile', 'device_id': 'B', 'operation_id': 'a' * 32,
                                  'session_id': 'new-session', 'permission_version': 17}))[1]
        self.assertNotIn('observation_status', result)
        next_request = self.dispatch('e' * 32)
        next_request['session_id'] = 'new-session'
        self.assertEqual('device_operation_pending', (await self.post(next_request))[1]['reason'])

    async def test_expired_download_and_unrecognized_tool_are_not_sent(self):
        self.device()
        request = self.dispatch()
        request['url'] = request['url'].replace(str(int(time.time()) + 600), '1000000000')
        self.assertEqual(400, (await self.post(request))[0])
        handler = self.bridge.sessions['B']
        async def send(raw):
            frame = json.loads(raw)['payload']
            self.frames.append(frame)
            self.pending[frame['id']].set_result({'tools': [{'name': 'ordinary'}]})
        handler.websocket = GuardedSocket(SimpleNamespace(send=send), handler.product_voice_gate)
        self.assertEqual('tool_unavailable', (await self.post(self.dispatch()))[1]['reason'])
        self.assertTrue(all(f['method'] == 'tools/list' for f in self.frames))

    async def test_unknown_survives_restart_and_repeat_reconciliation_does_not_resend(self):
        self.device()
        self.lose_result = True
        request = self.dispatch()
        self.assertEqual('result_unknown', (await self.post(request))[1]['status'])
        sessions = self.bridge.sessions
        await self.bridge.close()
        self.bridge = ProductOtaBridge(sessions, self.temp.name, 'k' * 32, 'https://firmware.example')
        await self.bridge.start('127.0.0.1', 0)
        self.url = 'http://127.0.0.1:%s/internal/product-ota/v1/control' % self.bridge.port
        self.assertEqual('result_unknown', (await self.post(request))[1]['status'])
        self.assertEqual(1, sum(f.get('params', {}).get('name') == 'self.upgrade_firmware' for f in self.frames))

    async def test_http_dispatch_crosses_real_websocket_to_selected_device_only(self):
        self.device()
        connected = asyncio.Event()
        selected_frames = []
        other_frames = []
        async def voice_connection(socket):
            self.bridge.sessions['B'].websocket = GuardedSocket(socket, self.bridge.sessions['B'].product_voice_gate)
            connected.set()
            async for raw in socket:
                reply = json.loads(raw)['payload']
                self.pending[reply['id']].set_result(reply['result'])
        async def selected_device(socket):
            async for raw in socket:
                frame = json.loads(raw)['payload']
                selected_frames.append(frame)
                if frame['method'] == 'tools/list':
                    result = {'tools': [{'name': 'self.upgrade_firmware'}, {'name': 'self.get_system_info'}]}
                elif frame['params']['name'] == 'self.get_system_info':
                    result = {'content': [{'type': 'text', 'text': '{"application":{"version":"2.2.5","elf_sha256":"' + 'a' * 64 + '"}}'}]}
                else:
                    result = {'content': [{'type': 'text', 'text': 'true'}]}
                await socket.send(json.dumps({'type': 'mcp', 'payload': {'jsonrpc': '2.0', 'id': frame['id'], 'result': result}}))
        async def other_send(raw): other_frames.append(raw)
        other = SimpleNamespace(**vars(self.bridge.sessions['B']))
        other.device_id = 'A'
        other.websocket = GuardedSocket(SimpleNamespace(send=other_send), other.product_voice_gate)
        self.bridge.sessions['A'] = other
        async with websockets.serve(voice_connection, '127.0.0.1', 0) as server:
            address = 'ws://127.0.0.1:%s' % server.sockets[0].getsockname()[1]
            async with websockets.connect(address) as socket:
                await connected.wait()
                device_task = asyncio.create_task(selected_device(socket))
                try:
                    self.assertEqual('accepted_unconfirmed', (await self.post(self.dispatch()))[1]['status'])
                    self.assertEqual(1, sum(f.get('params', {}).get('name') == 'self.upgrade_firmware' for f in selected_frames))
                    self.assertFalse(other_frames)
                finally:
                    device_task.cancel()
                    await asyncio.gather(device_task, return_exceptions=True)


if __name__ == '__main__':
    unittest.main()
