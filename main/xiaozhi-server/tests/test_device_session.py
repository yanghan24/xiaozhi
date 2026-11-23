"""Identity and role boundaries around the original runtime; no external requests."""
import asyncio
import concurrent.futures
import threading
from types import SimpleNamespace
from unittest.mock import AsyncMock
from unittest.mock import Mock

import pytest

from core.auth import AuthenticationError
from core.device_session import DeviceSession


@pytest.mark.asyncio
async def test_identity_opens_without_model_credentials(monkeypatch):
    api = AsyncMock(return_value={'allowed': True, 'device_id': 'fixture',
                                 'session': 'identity', 'memory_scope': 'owner-device-role'})
    monkeypatch.setattr('core.device_session.device_session', api)
    identity = await DeviceSession.open({'device-id': 'fixture', 'authorization': 'Bearer ticket'})
    assert identity.session == 'identity'
    assert identity.memory_scope == 'owner-device-role'
    api.assert_awaited_once_with('open', {'device_id': 'fixture', 'token': 'ticket'})


def connection():
    ready = asyncio.Event()
    ready.set()
    return SimpleNamespace(configuration_ready=ready, config={'configuration_revision': 'old'},
                           client_is_speaking=False, client_abort=False,
                           clear_queues=lambda: None, close=AsyncMock(),
                           reload_agent_configuration=AsyncMock())


@pytest.mark.asyncio
async def test_role_switch_waits_for_current_generation_and_does_not_stop_it():
    identity = DeviceSession('fixture')
    identity.status = AsyncMock(return_value={'configuration_revision': 'new', 'memory_scope': 'new-owner-role'})
    conn = connection()
    previous = concurrent.futures.Future()
    conn.chat_future = previous
    refresh = asyncio.create_task(identity.before_turn(conn))
    await asyncio.sleep(0)
    await asyncio.sleep(0)
    conn.reload_agent_configuration.assert_not_awaited()
    assert conn.client_abort is False
    previous.set_result(None)
    await refresh
    conn.reload_agent_configuration.assert_awaited_once()
    assert conn.memory_scope == 'new-owner-role'
    conn.close.assert_not_awaited()


@pytest.mark.asyncio
async def test_unchanged_role_keeps_original_providers_and_context():
    identity = DeviceSession('fixture')
    identity.status = AsyncMock(return_value={'configuration_revision': 'old'})
    conn = connection()
    await identity.before_turn(conn)
    conn.reload_agent_configuration.assert_not_awaited()
    conn.close.assert_not_awaited()


@pytest.mark.asyncio
async def test_revocation_closes_the_original_connection(monkeypatch):
    identity = DeviceSession('fixture')
    identity.status = AsyncMock(side_effect=AuthenticationError('revoked'))
    conn = connection()
    conn.stop_event = threading.Event()
    async def tick(_):
        pass
    monkeypatch.setattr('core.device_session.asyncio.sleep', tick)
    await identity.watch(conn)
    assert conn.client_abort is True
    conn.close.assert_awaited_once()


@pytest.mark.asyncio
async def test_original_role_reload_removes_old_optional_providers_and_context(monkeypatch):
    from concurrent.futures import ThreadPoolExecutor
    from core.connection import ConnectionHandler
    from core.utils.dialogue import Dialogue, Message
    conn = ConnectionHandler.__new__(ConnectionHandler)
    conn.configuration_ready = asyncio.Event()
    conn.common_config = {'selected_module': {}, 'speaker_identity_enabled': True}
    conn.config = {'VLLM': {'old': {}}, 'mcp_servers': {'old': {}}, 'plugins': {'old': {}}, 'prompt': 'old'}
    conn.tts = SimpleNamespace(close=AsyncMock())
    conn.asr = SimpleNamespace(close=AsyncMock())
    conn.vad = None
    conn.clear_queues = Mock()
    conn.reset_audio_states = Mock()
    conn.func_handler = SimpleNamespace(cleanup=AsyncMock())
    conn.logger = Mock()
    conn.dialogue = Dialogue()
    conn.dialogue.put(Message(role='user', content='private old role context'))
    conn.introduced_speakers = {'old'}
    conn.system_introduced_speakers = {'old'}
    conn.need_bind = False
    conn.identity_session = SimpleNamespace(configuration_loaded=AsyncMock())
    conn._initialize_private_config_async = AsyncMock()
    conn._initialize_components = Mock()
    monkeypatch.setattr('core.connection.PromptManager', Mock())
    with ThreadPoolExecutor(max_workers=1) as conn.executor:
        await conn.reload_agent_configuration()
    assert conn.dialogue.dialogue == []
    assert conn.config == conn.common_config and conn.config is not conn.common_config
    assert conn.llm is conn.memory is conn.intent is None
    assert conn.func_handler is None
    assert conn.configuration_ready.is_set()
    conn._initialize_private_config_async.assert_awaited_once()
    conn._initialize_components.assert_called_once()
