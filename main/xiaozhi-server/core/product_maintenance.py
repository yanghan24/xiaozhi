"""Optional root-only Unix control for an explicit upgrade on a current product session."""
import asyncio
import json
import os
from pathlib import Path
import re
import socket
import stat
import struct
import time
from urllib.parse import urlsplit, parse_qs


class MaintenanceDenied(Exception):
    pass


class MaintenanceControl:
    def __init__(self, sessions, directory, allowed_device):
        self.sessions = sessions
        self.directory = Path(directory)
        if not isinstance(allowed_device, str) or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', allowed_device):
            raise MaintenanceDenied()
        self.allowed_device = allowed_device
        self.server = None
        self.lock = asyncio.Lock()

    async def start(self):
        self.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        info = self.directory.stat()
        if self.directory.is_symlink() or info.st_uid != 0 or info.st_mode & 0o077:
            raise MaintenanceDenied()
        path = self.directory / 'control.sock'
        if path.exists():
            info = path.lstat()
            if not stat.S_ISSOCK(info.st_mode) or info.st_uid != 0:
                raise MaintenanceDenied()
            probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            probe.settimeout(0.1)
            try:
                probe.connect(str(path))
            except ConnectionRefusedError:
                path.unlink()  # A verified stale root-owned socket from a stopped process.
            else:
                raise MaintenanceDenied()
            finally:
                probe.close()
        self.server = await asyncio.start_unix_server(self.connection, path=str(path), limit=8192)
        os.chmod(path, 0o600)

    async def close(self):
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
            (self.directory / 'control.sock').unlink(missing_ok=True)
            self.server = None

    async def connection(self, reader, writer):
        result = {'status': 'denied'}
        request = None
        try:
            peer = writer.get_extra_info('socket')
            _, uid, _ = struct.unpack('3i', peer.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if uid != 0:
                raise MaintenanceDenied()
            raw = await asyncio.wait_for(reader.readline(), 3)
            if len(raw) > 8192 or not raw.endswith(b'\n'):
                raise MaintenanceDenied()
            request = json.loads(raw)
            async with self.lock:
                result = await self.execute(request)
        except (Exception, asyncio.CancelledError) as error:
            # URLs, credentials, device responses, and exception text never enter logs/output.
            if isinstance(request, dict) and request.get('action') == 'upgrade':
                operation = request.get('operation_id')
                if (isinstance(operation, str) and re.fullmatch(r'[a-f0-9]{32}', operation)
                        and (self.directory / (operation + '.json')).exists()):
                    result = {'status': 'result_unknown_do_not_retry'}
            if isinstance(error, asyncio.CancelledError):
                raise  # Preserve shutdown cancellation after classifying the socket response.
        finally:
            writer.write(json.dumps(result).encode() + b'\n')
            try:
                await writer.drain()
            finally:
                writer.close()
                await writer.wait_closed()

    def current(self, request):
        matches = [h for h in self.sessions.values()
                   if getattr(h, 'device_id', None) == request.get('device_id')
                   and getattr(h, 'product_voice_gate', None) is not None]
        if len(matches) != 1:
            raise MaintenanceDenied()
        handler = matches[0]
        if (request.get('session_id') != handler.session_id
                or type(request.get('permission_version')) is not int
                or request['permission_version'] != handler.product_voice_gate.version
                or not getattr(handler, 'mcp_client', None)):
            raise MaintenanceDenied()
        return handler

    async def rpc(self, handler, request, method, params):
        client = handler.mcp_client
        if not await client.is_ready():
            raise MaintenanceDenied()
        await handler.product_voice_gate.check_async()
        call_id = await client.get_next_id()
        future = asyncio.get_running_loop().create_future()
        await client.register_call_result_future(call_id, future)
        def validate_session():
            if self.current(request) is not handler:
                raise MaintenanceDenied()
        try:
            # Deliberately bypass the generic helper, which logs tool arguments.
            # GuardedSocket rechecks current authority at the actual send boundary.
            await handler.websocket.send(json.dumps({'type': 'mcp', 'payload': {
                'jsonrpc': '2.0', 'id': call_id, 'method': method, 'params': params}}),
                before_send=validate_session)
            return await asyncio.wait_for(future, 10)
        finally:
            await client.cleanup_call_result(call_id)

    async def execute(self, request):
        if not isinstance(request, dict) or request.get('device_id') != self.allowed_device:
            raise MaintenanceDenied()
        if request.get('action') == 'status' and set(request) == {'action', 'device_id'}:
            matches = [h for h in self.sessions.values()
                       if getattr(h, 'device_id', None) == request['device_id']
                       and getattr(h, 'product_voice_gate', None) is not None]
            if len(matches) != 1:
                return {'status': 'no_unique_session'}
            h = matches[0]
            await h.product_voice_gate.check_async()
            return {'status': 'current', 'session_id': h.session_id,
                    'permission_version': h.product_voice_gate.version,
                    'mcp_ready': bool(getattr(h, 'mcp_client', None) and await h.mcp_client.is_ready())}
        base = {'action', 'device_id', 'session_id', 'permission_version'}
        action = request.get('action')
        if action not in ('probe', 'upgrade') or set(request) != (base if action == 'probe' else base | {'url', 'operation_id'}):
            raise MaintenanceDenied()
        handler = self.current(request)
        result = await self.rpc(handler, request, 'tools/list', {'withUserTools': True})
        names = set()
        for _ in range(32):
            if not isinstance(result, dict) or result.get('isError'):
                raise MaintenanceDenied()
            names.update(t.get('name') for t in result.get('tools', []) if isinstance(t, dict))
            cursor = result.get('nextCursor')
            if not cursor:
                break
            result = await self.rpc(handler, request, 'tools/list', {'withUserTools': True, 'cursor': cursor})
        else:
            raise MaintenanceDenied()
        # The user-only response is resolved as an RPC future, never added to model tools.
        if 'self.upgrade_firmware' not in names:
            return {'status': 'upgrade_tool_unavailable'}
        self.current(request)
        if action == 'probe':
            return {'status': 'upgrade_tool_available'}
        url = request['url']
        if not isinstance(url, str):
            raise MaintenanceDenied()
        parsed = urlsplit(url)
        query = parse_qs(parsed.query, strict_parsing=True)
        if (parsed.scheme != 'https' or parsed.netloc != 'bridge.hncig.cn:18443'
                or parsed.path != '/xiaozhi/maintenance-ota/p4-a30f6a8/product.bin'
                or parsed.fragment or set(query) != {'md5', 'expires'}
                or any(len(v) != 1 for v in query.values())
                or not re.fullmatch(r'[A-Za-z0-9_-]{22}', query['md5'][0])
                or not query['expires'][0].isdigit()
                or not 30 < int(query['expires'][0]) - time.time() <= 900):
            raise MaintenanceDenied()
        operation = request['operation_id']
        if not isinstance(operation, str) or not re.fullmatch(r'[a-f0-9]{32}', operation):
            raise MaintenanceDenied()
        # Reserve durably before sending: a lost response must never invite an automatic retry.
        with (self.directory / (operation + '.json')).open('x') as f:
            os.chmod(f.name, 0o600)
            json.dump({'status': 'reserved_result_unknown'}, f)
            f.flush()
            os.fsync(f.fileno())
        directory_fd = os.open(self.directory, os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
        result = await self.rpc(handler, request, 'tools/call', {
            'name': 'self.upgrade_firmware', 'arguments': {'url': url}})
        accepted = (isinstance(result, dict) and not result.get('isError')
                    and any(isinstance(c, dict) and c.get('text', '').strip() == 'true'
                            for c in result.get('content', [])))
        status = 'queued_not_ota_confirmed' if accepted else 'result_requires_hardware_check'
        (self.directory / (operation + '.json')).write_text(json.dumps({'status': status}))
        return {'status': status}
