"""Device identity around the upstream runtime; no audio, model or tool execution."""
import asyncio
import contextlib

from config.manage_api_client import device_session
from core.auth import AuthenticationError


class DeviceSession:
    def __init__(self, session, memory_scope=None):
        self.session = session
        self.memory_scope = memory_scope
        self.lock = asyncio.Lock()

    @classmethod
    async def open(cls, headers):
        token = headers.get('authorization', '')
        device = headers.get('device-id', '')
        if not token.startswith('Bearer ') or not device:
            raise AuthenticationError('Device login required')
        try:
            status = await device_session('open', {'device_id': device, 'token': token[7:]})
            if status.get('allowed') is not True or status.get('device_id') != device:
                raise ValueError()
            return cls(status['session'], status['memory_scope'])
        except Exception:
            raise AuthenticationError('Device login required') from None

    async def status(self):
        status = await device_session('check', {'session': self.session})
        if status.get('allowed') is not True:
            raise AuthenticationError('Device login required')
        return status

    async def configuration_loaded(self, conn):
        receipt = await device_session('conversation', {'session': self.session,
            'configuration_revision': conn.config['configuration_revision']})
        conn.history_session_id = receipt['history_id']

    async def watch(self, conn):
        try:
            while not conn.stop_event.is_set():
                await asyncio.sleep(2)
                await self.status()
        except asyncio.CancelledError:
            raise
        except Exception:
            conn.client_abort = True
            conn.clear_queues()
            await conn.close()

    async def before_turn(self, conn):
        """Changes apply between answers, reusing upstream module initialization."""
        async with self.lock:
            await asyncio.wait_for(conn.configuration_ready.wait(), 30)
            try:
                status = await self.status()
                if status['configuration_revision'] != conn.config.get('configuration_revision'):
                    previous = getattr(conn, 'chat_future', None)
                    if previous is not None and not previous.done():
                        await asyncio.wait_for(asyncio.shield(asyncio.wrap_future(previous)), 30)
                    for _ in range(300):
                        if not conn.client_is_speaking:
                            break
                        await asyncio.sleep(.1)
                    if conn.client_is_speaking:
                        raise RuntimeError('Previous answer still playing')
                    conn.memory_scope = status['memory_scope']
                    await conn.reload_agent_configuration()
            except Exception:
                conn.client_abort = True
                conn.clear_queues()
                await conn.close()
                raise AuthenticationError('Device configuration unavailable') from None

    async def close(self):
        with contextlib.suppress(Exception):
            await device_session('close', {'session': self.session})
