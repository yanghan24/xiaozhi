"""Device identity around the upstream runtime; no audio, model or tool execution."""
import asyncio
import contextlib

from config.manage_api_client import MarketCredentialError, device_session
from core.auth import AuthenticationError
from core.providers.market_credential import MarketSessionCredential, notify_market_credential_error


def diagnostic_deadline(value):
    # The management API serializes Java Long values as strings.
    if type(value) is int:
        return max(0, value)
    if isinstance(value, str) and 0 < len(value) <= 16 and value.isascii() and value.isdecimal():
        return int(value)
    return 0


class DeviceSession:
    def __init__(self, session, memory_scope=None, diagnostics_until_ms=0):
        self.session = session
        self.memory_scope = memory_scope
        self.diagnostics_until_ms = diagnostic_deadline(diagnostics_until_ms)
        self.lock = asyncio.Lock()
        self.market_credential = None

    def install_market_credential(self, conn, config):
        data = config.get("market_credential")
        self.market_credential = None
        if not isinstance(data, dict):
            return
        self.market_credential = MarketSessionCredential(conn.device_id, self.session, conn.loop, data)
        for kind in ("ASR", "TTS", "LLM", "VLLM", "SLM"):
            for provider in (config.get(kind) or {}).values():
                if isinstance(provider, dict) and provider.get("source") == "aimarket":
                    provider["market_credential_handle"] = self.market_credential

    def invalidate_market_credential(self):
        if self.market_credential:
            self.market_credential.invalidate()

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
            return cls(status['session'], status['memory_scope'], status.get('diagnostics_until_ms', 0))
        except Exception:
            raise AuthenticationError('Device login required') from None

    async def status(self):
        status = await device_session('check', {'session': self.session})
        if status.get('allowed') is not True:
            raise AuthenticationError('Device login required')
        self.diagnostics_until_ms = diagnostic_deadline(status.get('diagnostics_until_ms'))
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
            self.invalidate_market_credential()
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
                elif self.market_credential and "market_credential_generation" in status:
                    await self.market_credential.synchronize(status["market_credential_generation"])
            except Exception as failure:
                if isinstance(failure, MarketCredentialError):
                    await notify_market_credential_error(conn, failure)
                self.invalidate_market_credential()
                conn.client_abort = True
                conn.clear_queues()
                await conn.close()
                raise AuthenticationError('Device configuration unavailable') from None

    async def close(self):
        with contextlib.suppress(Exception):
            await device_session('close', {'session': self.session})
