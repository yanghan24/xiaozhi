"""Isolated HTTP/WebSocket device peer for #97 acceptance; never a production entrypoint.

Run only on an isolated test Docker network. Ports 8097/8098 must not be published.
"""
import asyncio
import json
import os
import tempfile
import threading
from types import SimpleNamespace
from aiohttp import web
import websockets
from core.product_ota_bridge import ProductOtaBridge
from core.product_voice import GuardedSocket


KEY = os.environ['PRODUCT_OTA_BRIDGE_KEY']
ORIGIN = os.environ.get('PRODUCT_OTA_BRIDGE_DOWNLOAD_ORIGIN', 'https://firmware.example')
PARTITIONS = [
    dict(label='nvs', type=1, subtype=2, address=0x9000, size=0x4000),
    dict(label='otadata', type=1, subtype=0, address=0xd000, size=0x2000),
    dict(label='phy_init', type=1, subtype=1, address=0xf000, size=0x1000),
    dict(label='ota_0', type=0, subtype=16, address=0x20000, size=0x3f0000),
    dict(label='ota_1', type=0, subtype=17, address=0x410000, size=0x3f0000),
    dict(label='assets', type=1, subtype=130, address=0x800000, size=0x800000)]


class DevicePeer:
    def __init__(self, device_id, sessions):
        self.device_id = device_id
        self.sessions = sessions
        self.session_id = 'session-97'
        self.mode = 'accepted'
        self.version = '2.2.5'
        self.elf_sha256 = 'a' * 64
        self.permission_version = 1
        self.revision = 100
        self.pending = {}
        self.next_id = 3
        self.upgrades = 0
        self.calls = []
        self.connected = asyncio.Event()
        self.tasks = []

    async def check(self):
        if self.mode in ('revoked', 'offline'):
            raise PermissionError('fixture permission denied')

    async def is_ready(self): return self.mode != 'not_ready'

    async def get_next_id(self):
        value = self.next_id
        self.next_id += 1
        return value

    async def register_call_result_future(self, call_id, future): self.pending[call_id] = future

    async def cleanup_call_result(self, call_id): self.pending.pop(call_id, None)

    async def voice_connection(self, socket):
        gate = SimpleNamespace(version=self.permission_version, check_async=self.check, ended=threading.Event())
        self.handler = SimpleNamespace(device_id=self.device_id, session_id=self.session_id,
            product_voice_gate=gate, mcp_client=self, websocket=GuardedSocket(socket, gate))
        self.sessions[self.device_id] = self.handler
        self.connected.set()
        async for raw in socket:
            reply = json.loads(raw)['payload']
            future = self.pending.get(reply['id'])
            if future is not None and not future.done(): future.set_result(reply['result'])

    async def device_connection(self, uri):
        async with websockets.connect(uri) as socket:
            async for raw in socket:
                frame = json.loads(raw)['payload']
                method = frame['method']
                name = frame['params'].get('name', '')
                self.calls.append(method + ':' + name)
                if method == 'tools/list':
                    result = {'tools': [] if self.mode == 'unsupported' else
                              [{'name': 'self.upgrade_firmware'}, {'name': 'self.get_system_info'}]}
                elif name == 'self.get_system_info':
                    info = {'flash_size': 33554432, 'chip_model_name': 'esp32p4',
                        'chip_info': {'model': 18, 'cores': 2, 'revision': self.revision, 'features': 0},
                        'application': {'version': self.version, 'elf_sha256': self.elf_sha256},
                        'board': {'type': 'waveshare-esp32-p4-wifi6-touch-lcd-4.3', 'name': 'fixture', 'ssid': 'must-not-escape'},
                        'partition_table': PARTITIONS, 'ota': {'label': 'ota_0'}}
                    result = {'content': [{'type': 'text', 'text': json.dumps(info)}]}
                elif name == 'self.upgrade_firmware':
                    self.upgrades += 1
                    # Neither retain nor print the signed download credential.
                    if self.mode == 'lost': continue
                    result = {'content': [{'type': 'text', 'text': 'false' if self.mode == 'rejected' else 'true'}]}
                else:
                    result = {'isError': True}
                await socket.send(json.dumps({'type': 'mcp', 'payload': {'jsonrpc': '2.0', 'id': frame['id'], 'result': result}}))

    async def start(self):
        self.server = await websockets.serve(self.voice_connection, '127.0.0.1', 0)
        uri = 'ws://127.0.0.1:%s' % self.server.sockets[0].getsockname()[1]
        self.tasks.append(asyncio.create_task(self.device_connection(uri)))
        await self.connected.wait()

    def configure(self, body):
        self.mode = body.get('mode', self.mode)
        if self.mode not in ('accepted', 'lost', 'rejected', 'offline', 'duplicate', 'not_ready', 'revoked', 'unsupported'):
            raise ValueError()
        self.session_id = body.get('session_id', self.session_id)
        self.version = body.get('version', self.version)
        self.elf_sha256 = body.get('elf_sha256', self.elf_sha256)
        self.permission_version = body.get('permission_version', self.permission_version)
        self.revision = body.get('revision', self.revision)
        self.handler.session_id = self.session_id
        self.handler.product_voice_gate.version = self.permission_version
        self.sessions.pop(self.device_id + '-duplicate', None)
        if self.mode == 'offline': self.sessions.pop(self.device_id, None)
        else: self.sessions[self.device_id] = self.handler
        if self.mode == 'duplicate': self.sessions[self.device_id + '-duplicate'] = self.handler

    async def close(self):
        for task in self.tasks: task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        self.server.close()
        await self.server.wait_closed()


class Fixture:
    async def reset(self):
        if getattr(self, 'bridge', None):
            await self.bridge.close()
            for peer in self.peers.values(): await peer.close()
            self.directory.cleanup()
        self.directory = tempfile.TemporaryDirectory(prefix='ota97-fixture-')
        self.sessions = {}
        self.peers = {name: DevicePeer(name, self.sessions) for name in ('ota97-device', 'ota97-unselected')}
        for peer in self.peers.values(): await peer.start()
        self.bridge = ProductOtaBridge(self.sessions, self.directory.name, KEY, ORIGIN)
        await self.bridge.start('0.0.0.0', 8097)

    async def control(self, request):
        if request.headers.get('X-Fixture-Key') != KEY: return web.json_response({'error': 'denied'}, status=401)
        try:
            body = await request.json()
            action = body['action']
            if action == 'reset': await self.reset()
            elif action == 'restart':
                await self.bridge.close()
                self.bridge = ProductOtaBridge(self.sessions, self.directory.name, KEY, ORIGIN)
                await self.bridge.start('0.0.0.0', 8097)
            elif action == 'device': self.peers[body['device_id']].configure(body)
            elif action != 'state': raise ValueError()
            return web.json_response({name: {'upgrades': p.upgrades, 'calls': p.calls, 'mode': p.mode} for name, p in self.peers.items()})
        except (KeyError, ValueError): return web.json_response({'error': 'invalid fixture command'}, status=400)


async def main():
    fixture = Fixture()
    await fixture.reset()
    app = web.Application(client_max_size=8192)
    app.router.add_post('/fixture', fixture.control)
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    await web.TCPSite(runner, '0.0.0.0', 8098).start()
    print('Isolated OTA protocol fixture ready', flush=True)
    await asyncio.Future()


if __name__ == '__main__':
    asyncio.run(main())
