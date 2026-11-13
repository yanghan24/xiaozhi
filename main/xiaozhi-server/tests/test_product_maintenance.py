import asyncio
import json
import os
from pathlib import Path
import socket
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest

from core.product_maintenance import MaintenanceControl, MaintenanceDenied
from core.product_voice import GuardedSocket


class Client:
    def __init__(self):
        self.next_id = 3
        self.call_results = {}
        self.tools = {'ordinary': {'name': 'ordinary'}}

    async def is_ready(self):
        return True

    async def get_next_id(self):
        result = self.next_id
        self.next_id += 1
        return result

    async def register_call_result_future(self, call_id, future):
        self.call_results[call_id] = future

    async def cleanup_call_result(self, call_id):
        self.call_results.pop(call_id, None)


class Gate:
    version = 17
    denied = False

    def __init__(self):
        self.ended = threading.Event()

    async def check_async(self):
        if self.denied:
            raise MaintenanceDenied()


class MaintenanceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.frames = []
        self.client = Client()
        self.gate = Gate()
        self.tool_exists = True
        self.lose_result = False
        self.wait_for_upgrade_result = False
        self.upgrade_sent = asyncio.Event()
        self.page = False

        async def send(raw):
            frame = json.loads(raw)['payload']
            self.frames.append(frame)
            if frame['method'] == 'tools/list':
                if self.page and 'cursor' not in frame['params']:
                    result = {'tools': [], 'nextCursor': 'next-page'}
                else:
                    result = {'tools': [{'name': 'self.upgrade_firmware'}] if self.tool_exists else []}
            else:
                if self.lose_result:
                    raise ConnectionError('private_url_must_not_be_returned')
                if self.wait_for_upgrade_result:
                    self.upgrade_task = asyncio.current_task()
                    self.upgrade_sent.set()
                    return
                result = {'content': [{'type': 'text', 'text': 'true'}]}
            self.client.call_results[frame['id']].set_result(result)

        self.handler = SimpleNamespace(device_id='device-B', session_id='current-session',
            product_voice_gate=self.gate, mcp_client=self.client,
            websocket=GuardedSocket(SimpleNamespace(send=send), self.gate))
        self.control = MaintenanceControl({'one': self.handler}, self.temp.name, 'device-B')

    def request(self, action='probe', **changes):
        result = {'action': action, 'device_id': 'device-B', 'session_id': 'current-session', 'permission_version': 17}
        if action == 'upgrade':
            result.update(url='https://bridge.hncig.cn:18443/xiaozhi/maintenance-ota/p4-a30f6a8/product.bin?md5=' + 'a' * 22 + '&expires=' + str(int(time.time()) + 600), operation_id='a' * 32)
        result.update(changes)
        return result

    async def test_hidden_tool_probe_uses_fresh_ids_without_exposing_tools_to_model(self):
        self.page = True
        result = await self.control.execute(self.request())
        self.assertEqual({'status': 'upgrade_tool_available'}, result)
        self.assertEqual([3, 4], [f['id'] for f in self.frames])
        self.assertTrue(all(f['params']['withUserTools'] is True for f in self.frames))
        self.assertEqual({'ordinary': {'name': 'ordinary'}}, self.client.tools)
        self.assertFalse(self.client.call_results)

    async def test_only_bound_device_current_session_and_permission_can_probe(self):
        for changed in ({'device_id': 'device-A'}, {'session_id': 'old-session'}, {'permission_version': 16}, {'permission_version': True}):
            with self.subTest(changed=changed), self.assertRaises(MaintenanceDenied):
                await self.control.execute(self.request(**changed))
        self.assertFalse(self.frames)

    async def test_ambiguous_sessions_are_rejected(self):
        self.control.sessions['two'] = self.handler
        with self.assertRaises(MaintenanceDenied):
            await self.control.execute(self.request())
        self.assertFalse(self.frames)

    async def test_revoked_permission_prevents_rpc(self):
        self.gate.denied = True
        with self.assertRaises(MaintenanceDenied):
            await self.control.execute(self.request())
        self.assertFalse(self.frames)

    async def test_permission_revoked_after_probe_blocks_upgrade_at_send_boundary(self):
        original_send = self.handler.websocket.send

        async def send(raw, **kwargs):
            await original_send(raw, **kwargs)
            if json.loads(raw)['payload']['method'] == 'tools/list':
                self.gate.denied = True

        self.handler.websocket.send = send
        with self.assertRaises(MaintenanceDenied):
            await self.control.execute(self.request('upgrade'))
        self.assertFalse(any(f['method'] == 'tools/call' for f in self.frames))

    async def test_new_session_during_final_permission_check_prevents_upgrade(self):
        original_send = self.handler.websocket.send
        original_check = self.gate.check_async

        async def new_session_during_check():
            self.control.sessions['two'] = SimpleNamespace(device_id='device-B',
                session_id='new-session', product_voice_gate=Gate(), mcp_client=Client())
            await asyncio.sleep(0)
            await original_check()

        async def send(raw, **kwargs):
            if json.loads(raw)['payload']['method'] == 'tools/call':
                self.gate.check_async = new_session_during_check
            return await original_send(raw, **kwargs)

        self.handler.websocket.send = send
        with self.assertRaises(MaintenanceDenied):
            await self.control.execute(self.request('upgrade'))
        self.assertFalse(any(f['method'] == 'tools/call' for f in self.frames))
        self.assertFalse(self.client.call_results)

    async def test_missing_user_tool_never_calls_upgrade(self):
        self.tool_exists = False
        self.assertEqual({'status': 'upgrade_tool_unavailable'}, await self.control.execute(self.request('upgrade')))
        self.assertTrue(all(f['method'] == 'tools/list' for f in self.frames))

    async def test_upgrade_is_explicit_and_journal_prevents_replay(self):
        result = await self.control.execute(self.request('upgrade'))
        self.assertEqual({'status': 'queued_not_ota_confirmed'}, result)
        self.assertEqual('self.upgrade_firmware', self.frames[-1]['params']['name'])
        with self.assertRaises(FileExistsError):
            await self.control.execute(self.request('upgrade'))
        self.assertEqual(1, sum(f['method'] == 'tools/call' for f in self.frames))
        self.assertEqual(0o600, (Path(self.temp.name) / ('a' * 32 + '.json')).stat().st_mode & 0o777)

    async def test_current_release_can_be_queued_without_reusing_historical_artifact_path(self):
        url = self.request('upgrade')['url'].replace('p4-a30f6a8', 'p4-12f2ff1')
        result = await self.control.execute(self.request('upgrade', url=url))
        self.assertEqual({'status': 'queued_not_ota_confirmed'}, result)
        self.assertEqual(url, self.frames[-1]['params']['arguments']['url'])

    async def test_unknown_result_durably_prevents_automatic_replay(self):
        self.lose_result = True
        with self.assertRaises(ConnectionError):
            await self.control.execute(self.request('upgrade'))
        self.assertFalse(self.client.call_results)
        self.lose_result = False
        with self.assertRaises(FileExistsError):
            await self.control.execute(self.request('upgrade'))
        self.assertEqual(1, sum(f['method'] == 'tools/call' for f in self.frames))
        record = json.loads((Path(self.temp.name) / ('a' * 32 + '.json')).read_text())
        self.assertEqual('reserved_result_unknown', record['status'])

    async def test_other_origin_expired_url_and_ambiguous_query_never_upgrade(self):
        good = self.request('upgrade')['url']
        for url in (good.replace('bridge.hncig.cn', 'example.com'),
                    good.replace('https:', 'http:'), good + '&md5=other',
                    good.split('&expires=')[0] + '&expires=1'):
            with self.subTest(url_type='invalid'), self.assertRaises(MaintenanceDenied):
                await self.control.execute(self.request('upgrade', url=url))
        self.assertFalse(any(f['method'] == 'tools/call' for f in self.frames))

    @unittest.skipUnless(hasattr(os, 'geteuid') and os.geteuid() == 0, 'Requires Linux root Unix credentials')
    async def test_real_unix_socket_is_private_and_status_contains_no_url(self):
        await self.control.start()
        try:
            path = Path(self.temp.name) / 'control.sock'
            self.assertEqual(0o600, path.stat().st_mode & 0o777)
            reader, writer = await asyncio.open_unix_connection(str(path))
            writer.write(json.dumps({'action': 'status', 'device_id': 'device-B'}).encode() + b'\n')
            await writer.drain()
            result = json.loads(await reader.readline())
            self.assertEqual('current', result['status'])
            self.assertNotIn('url', result)
            writer.close()
            await writer.wait_closed()
        finally:
            await self.control.close()

    @unittest.skipUnless(hasattr(os, 'geteuid') and os.geteuid() == 0, 'Requires Linux root Unix credentials')
    async def test_real_socket_reports_unknown_instead_of_denied_after_reserved_send_failure(self):
        self.lose_result = True
        await self.control.start()
        try:
            reader, writer = await asyncio.open_unix_connection(str(Path(self.temp.name) / 'control.sock'))
            writer.write(json.dumps(self.request('upgrade')).encode() + b'\n')
            await writer.drain()
            result = json.loads(await reader.readline())
            self.assertEqual({'status': 'result_unknown_do_not_retry'}, result)
            writer.close()
            await writer.wait_closed()
        finally:
            await self.control.close()

    @unittest.skipUnless(hasattr(os, 'geteuid') and os.geteuid() == 0, 'Requires Linux root Unix credentials')
    async def test_cancelled_upgrade_returns_unknown_and_cannot_be_replayed(self):
        self.wait_for_upgrade_result = True
        await self.control.start()
        reader, writer = await asyncio.open_unix_connection(str(Path(self.temp.name) / 'control.sock'))
        try:
            writer.write(json.dumps(self.request('upgrade')).encode() + b'\n')
            await writer.drain()
            await asyncio.wait_for(self.upgrade_sent.wait(), 3)
            self.upgrade_task.cancel()
            result = json.loads(await asyncio.wait_for(reader.readline(), 3))
            await asyncio.gather(self.upgrade_task, return_exceptions=True)
            self.assertTrue(self.upgrade_task.cancelled())
            self.assertEqual({'status': 'result_unknown_do_not_retry'}, result)
            self.wait_for_upgrade_result = False
            with self.assertRaises(FileExistsError):
                await self.control.execute(self.request('upgrade'))
            self.assertEqual(1, sum(f['method'] == 'tools/call' for f in self.frames))
        finally:
            writer.close()
            await writer.wait_closed()
            await self.control.close()

    @unittest.skipUnless(hasattr(os, 'geteuid') and os.geteuid() == 0, 'Requires Linux root Unix credentials')
    async def test_regular_file_cannot_be_replaced_as_stale_socket(self):
        path = Path(self.temp.name) / 'control.sock'
        path.write_text('keep')
        with self.assertRaises(MaintenanceDenied):
            await self.control.start()
        self.assertEqual('keep', path.read_text())

    @unittest.skipUnless(hasattr(os, 'geteuid') and os.geteuid() == 0, 'Requires Linux root Unix credentials')
    async def test_stale_socket_can_recover_after_process_restart(self):
        path = Path(self.temp.name) / 'control.sock'
        sock = socket.socket(socket.AF_UNIX)
        sock.bind(str(path))
        sock.close()
        await self.control.start()
        await self.control.close()


if __name__ == '__main__':
    unittest.main()
