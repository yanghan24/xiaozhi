"""Private operator OTA capability; never registered on the public voice listener."""
import asyncio
import hmac
import json
import os
from pathlib import Path
import re
import sqlite3
import time
from urllib.parse import urlsplit, parse_qs
from aiohttp import web
from core.product_maintenance import MaintenanceControl, MaintenanceDenied


def strict_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError('Duplicate field')
        value[key] = item
    return value


class ProductOtaBridge:
    def __init__(self, sessions, directory, key, download_origin):
        if not isinstance(key, str) or len(key.encode()) < 32:
            raise ValueError('OTA bridge key required')
        origin = urlsplit(download_origin)
        if (origin.scheme != 'https' or not origin.hostname or origin.username or origin.password
                or origin.path not in ('', '/') or origin.query or origin.fragment):
            raise ValueError('OTA HTTPS origin required')
        self.origin = 'https://' + origin.netloc
        self.key = key
        self.sessions = sessions
        self.control = MaintenanceControl(sessions, directory, 'unused-private-adapter')
        path = Path(directory)
        path.mkdir(mode=0o700, parents=True, exist_ok=True)
        if path.is_symlink() or path.stat().st_uid != os.getuid() or path.stat().st_mode & 0o077:
            raise ValueError('Private OTA journal directory required')
        database = path / 'operations.sqlite3'
        if database.is_symlink():
            raise ValueError('Invalid OTA journal')
        self.db = sqlite3.connect(database)
        os.chmod(database, 0o600)
        self.db.execute('PRAGMA synchronous=FULL')
        self.db.execute('CREATE TABLE IF NOT EXISTS operations (id TEXT PRIMARY KEY, device TEXT NOT NULL, identity TEXT NOT NULL, result TEXT NOT NULL, active INTEGER NOT NULL)')
        self.db.execute('CREATE UNIQUE INDEX IF NOT EXISTS active_device ON operations(device) WHERE active=1')
        self.db.commit()
        self.locks = {}
        self.capacity = asyncio.Semaphore(8)
        self.runner = None

    async def start(self, host, port):
        app = web.Application(client_max_size=8192)
        app.router.add_post('/internal/product-ota/v1/control', self.handle)
        self.runner = web.AppRunner(app, access_log=None)
        await self.runner.setup()
        site = web.TCPSite(self.runner, host, port)
        await site.start()
        self.port = site._server.sockets[0].getsockname()[1]

    async def close(self):
        if self.runner is not None:
            await self.runner.cleanup()
        self.db.close()

    async def handle(self, request):
        keys = request.headers.getall('X-OTA-Service-Key', [])
        if len(keys) != 1 or not hmac.compare_digest(keys[0].encode(), self.key.encode()):
            return web.json_response({'status': 'denied'}, status=401)
        body = None
        try:
            if request.content_type != 'application/json':
                raise ValueError()
            raw = await asyncio.wait_for(request.read(), 3)
            body = json.loads(raw, object_pairs_hook=strict_object)
            self.validate(body)
            async with self.capacity:
                lock = self.locks.setdefault(body['device_id'], asyncio.Lock())
                async with lock:
                    result = await asyncio.wait_for(self.execute(body), 25)
            return web.json_response(result, headers={'Cache-Control': 'no-store'})
        except (ValueError, web.HTTPRequestEntityTooLarge):
            return web.json_response({'status': 'denied'}, status=400)
        except (Exception, asyncio.CancelledError) as error:
            # No raw exception, device reply, signed URL or service credential is logged.
            result = {'status': 'not_sent', 'reason': 'session_or_permission_unavailable'}
            if isinstance(body, dict) and body.get('action') == 'dispatch':
                row = self.record(body['operation_id'], body['device_id'])
                if row:
                    result = json.loads(row[1])
            if isinstance(error, asyncio.CancelledError):
                raise
            return web.json_response(result, headers={'Cache-Control': 'no-store'})

    def validate(self, body):
        if not isinstance(body, dict) or not isinstance(body.get('device_id'), str) or not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', body['device_id']):
            raise ValueError()
        basic = {'action', 'device_id'}
        session = {'session_id', 'permission_version'}
        fields = {'status': basic, 'inspect': basic | session, 'operation': basic | {'operation_id'},
                  'reconcile': basic | session | {'operation_id'},
                  'dispatch': basic | session | {'operation_id', 'artifact_sha256', 'target_elf_sha256', 'target_version', 'url'}}
        if body.get('action') not in fields or set(body) != fields[body['action']]:
            raise ValueError()
        if 'session_id' in body and (not isinstance(body['session_id'], str) or not 1 <= len(body['session_id']) <= 128
                or type(body['permission_version']) is not int or body['permission_version'] < 0):
            raise ValueError()
        if 'operation_id' in body and (not isinstance(body['operation_id'], str) or not re.fullmatch('[a-f0-9]{32}', body['operation_id'])):
            raise ValueError()
        if body['action'] == 'dispatch':
            for field in ('artifact_sha256', 'target_elf_sha256'):
                if not isinstance(body[field], str) or not re.fullmatch('[a-f0-9]{64}', body[field]): raise ValueError()
            if not isinstance(body['target_version'], str) or not 1 <= len(body['target_version']) <= 32: raise ValueError()
            if not isinstance(body['url'], str) or len(body['url']) > 2048: raise ValueError()
            parsed = urlsplit(body['url'])
            query = parse_qs(parsed.query, strict_parsing=True)
            if (parsed.scheme + '://' + parsed.netloc != self.origin or parsed.fragment
                    or parsed.path != '/xiaozhi/ota-artifacts/' + body['operation_id'] + '/' + body['artifact_sha256'] + '.bin'
                    or set(query) != {'expires', 'signature'} or any(len(v) != 1 for v in query.values())
                    or not re.fullmatch('[0-9]{10}', query['expires'][0])
                    or not re.fullmatch('[a-f0-9]{64}', query['signature'][0])):
                raise ValueError()

    def record(self, operation, device):
        return self.db.execute('SELECT identity,result FROM operations WHERE id=? AND device=?', (operation, device)).fetchone()

    def save(self, operation, result, active=1):
        self.db.execute('UPDATE operations SET result=?,active=? WHERE id=?', (json.dumps(result), active, operation))
        self.db.commit()

    async def inspect(self, request):
        handler = self.control.current(request)
        names = set()
        params = {'withUserTools': True}
        for _ in range(32):
            result = await self.control.rpc(handler, request, 'tools/list', params)
            if not isinstance(result, dict) or result.get('isError') or not isinstance(result.get('tools'), list):
                raise MaintenanceDenied()
            names.update(t.get('name') for t in result['tools'] if isinstance(t, dict) and isinstance(t.get('name'), str))
            if not result.get('nextCursor'): break
            params = {'withUserTools': True, 'cursor': result['nextCursor']}
        else:
            raise MaintenanceDenied()
        if not {'self.get_system_info', 'self.upgrade_firmware'} <= names:
            return {'status': 'tool_unavailable'}
        result = await self.control.rpc(handler, request, 'tools/call', {'name': 'self.get_system_info', 'arguments': {}})
        if not isinstance(result, dict) or result.get('isError'): raise MaintenanceDenied()
        texts = [c.get('text') for c in result.get('content', []) if isinstance(c, dict) and c.get('type') == 'text']
        if len(texts) != 1 or not isinstance(texts[0], str) or len(texts[0]) > 32768: raise MaintenanceDenied()
        info = json.loads(texts[0], object_pairs_hook=strict_object)
        if not isinstance(info, dict): raise MaintenanceDenied()
        # A projection, not an arbitrary RPC proxy. Compatibility checks remain in the manager.
        clean = {k: info[k] for k in ('flash_size', 'chip_model_name') if k in info and isinstance(info[k], (str, int))}
        for section, fields in {'chip_info': ('model', 'cores', 'revision', 'features'), 'application': ('version', 'elf_sha256'),
                                'board': ('type', 'name'), 'ota': ('label',)}.items():
            value = info.get(section, {})
            if isinstance(value, dict): clean[section] = {k: value[k] for k in fields if k in value and type(value[k]) in (str, int)}
        partitions = info.get('partition_table')
        if isinstance(partitions, list) and len(partitions) <= 96:
            clean['partition_table'] = [{k: p[k] for k in ('label', 'type', 'subtype', 'address', 'size')
                                        if k in p and type(p[k]) in (str, int)} for p in partitions if isinstance(p, dict)]
        self.control.current(request)
        return {'status': 'ready', 'system_info': clean}

    async def execute(self, request):
        action = request['action']
        if action == 'status':
            matches = [h for h in self.sessions.values() if getattr(h, 'device_id', None) == request['device_id'] and getattr(h, 'product_voice_gate', None) is not None]
            if not matches: return {'status': 'offline'}
            if len(matches) != 1: return {'status': 'no_unique_session'}
            handler = matches[0]
            try: await handler.product_voice_gate.check_async()
            except Exception: return {'status': 'permission_unavailable'}
            if not getattr(handler, 'mcp_client', None) or not await handler.mcp_client.is_ready(): return {'status': 'mcp_initializing'}
            return {'status': 'current', 'session_id': handler.session_id, 'permission_version': handler.product_voice_gate.version}
        if action == 'inspect': return await self.inspect(request)
        row = self.record(request['operation_id'], request['device_id'])
        if action == 'operation': return json.loads(row[1]) if row else {'status': 'not_found'}
        if action == 'reconcile':
            if not row: return {'status': 'not_found'}
            identity, result = json.loads(row[0]), json.loads(row[1])
            if identity['session_id'] == request['session_id']: return result
            observation = await self.inspect(request)
            app = observation.get('system_info', {}).get('application', {})
            if observation['status'] == 'ready' and app.get('version') == identity['target_version'] and app.get('elf_sha256') == identity['target_elf_sha256']:
                result.update(observation_status='target_running', observed_at_ms=int(time.time() * 1000))
                self.save(request['operation_id'], result, active=0)
            return result
        identity = {k: v for k, v in request.items() if k not in ('action', 'url')}
        serialized = json.dumps(identity, sort_keys=True)
        if row:
            if row[0] != serialized: return {'status': 'not_sent', 'reason': 'operation_conflict'}
            return json.loads(row[1])
        if self.db.execute('SELECT id FROM operations WHERE device=? AND active=1', (request['device_id'],)).fetchone():
            return {'status': 'not_sent', 'reason': 'device_operation_pending'}
        expiry = int(parse_qs(urlsplit(request['url']).query)['expires'][0])
        if not 30 < expiry - time.time() <= 900: raise ValueError()
        inspection = await self.inspect(request)
        if inspection['status'] != 'ready': return {'status': 'not_sent', 'reason': inspection['status']}
        handler = self.control.current(request)
        await handler.product_voice_gate.check_async()
        result = {'status': 'result_unknown'}
        try:
            self.db.execute('INSERT INTO operations VALUES (?,?,?,?,1)', (request['operation_id'], request['device_id'], serialized, json.dumps(result)))
            self.db.commit()
        except sqlite3.IntegrityError:
            self.db.rollback()
            return {'status': 'not_sent', 'reason': 'operation_conflict'}
        # This FULL-sync commit precedes the guarded wire send. Exceptions never cause resend.
        response = await self.control.rpc(handler, request, 'tools/call', {'name': 'self.upgrade_firmware', 'arguments': {'url': request['url']}})
        accepted = isinstance(response, dict) and not response.get('isError') and any(isinstance(c, dict) and c.get('text', '').strip() == 'true' for c in response.get('content', []))
        result = {'status': 'accepted_unconfirmed' if accepted else 'result_requires_verification'}
        self.save(request['operation_id'], result)
        return result
