"""Only local service-management protocol fixtures; never restart a running service."""
import json
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock
import pytest
import websockets
from core import management


def server():
    return SimpleNamespace(config={"read_config_from_api": True, "manager-api": {"secret": "fixture-service-secret"}},
                           update_config=AsyncMock(return_value=True))


async def exchange(service, token, payload):
    async def handler(socket):
        await management.handle_management(service, socket)
    async with websockets.serve(handler, "127.0.0.1", 0) as listener:
        port = listener.sockets[0].getsockname()[1]
        async with websockets.connect(f"ws://127.0.0.1:{port}{management.PATH}",
                                      additional_headers={"Authorization": "Bearer " + token}) as socket:
            await socket.send(json.dumps(payload))
            return json.loads(await socket.recv())


@pytest.mark.asyncio
async def test_update_has_real_success_and_failure_acknowledgements(monkeypatch):
    instance=server()
    restart=Mock();monkeypatch.setattr(management,"schedule_restart",restart)
    request={"type":"server","action":"update_config"}
    assert (await exchange(instance,"fixture-service-secret",request))["status"] == "success"
    instance.update_config.return_value=False
    assert (await exchange(instance,"fixture-service-secret",request))["status"] == "error"
    instance.update_config.side_effect=RuntimeError("fixture failure")
    assert (await exchange(instance,"fixture-service-secret",request))["status"] == "error"
    restart.assert_not_called()


@pytest.mark.asyncio
async def test_device_ticket_or_missing_service_secret_cannot_administer(monkeypatch):
    restart=Mock();monkeypatch.setattr(management,"schedule_restart",restart)
    for mode in ("bad-token","missing-secret","not-api"):
        instance=server()
        if mode=="missing-secret":instance.config["manager-api"]["secret"]=""
        if mode=="not-api":instance.config["read_config_from_api"]=False
        with pytest.raises(websockets.exceptions.ConnectionClosed):
            await exchange(instance,"device-ticket" if mode=="bad-token" else "fixture-service-secret",{"type":"server","action":"restart"})
        instance.update_config.assert_not_awaited()
    restart.assert_not_called()


@pytest.mark.asyncio
async def test_restart_only_accepts_explicit_authenticated_command(monkeypatch):
    restart=Mock();monkeypatch.setattr(management,"schedule_restart",restart)
    instance=server()
    for payload in ({"type":"hello","action":"restart"},{"type":"server","action":"unknown"},[]):
        assert (await exchange(instance,"fixture-service-secret",payload))["status"] == "error"
    restart.assert_not_called()
    result=await exchange(instance,"fixture-service-secret",{"type":"server","action":"restart"})
    assert result["status"] == "success" and result["content"]["action"] == "restart"
    restart.assert_called_once();instance.update_config.assert_not_awaited()


@pytest.mark.asyncio
async def test_restart_replaces_current_process_without_spawning_an_unmanaged_child(monkeypatch):
    replace=Mock();monkeypatch.setattr(management.os,"execv",replace)
    monkeypatch.setattr(management.asyncio,"sleep",AsyncMock())
    await management.restart_process()
    replace.assert_called_once_with(management.sys.executable,[management.sys.executable,*management.sys.argv])


@pytest.mark.asyncio
async def test_real_websocket_routing_keeps_management_outside_device_initialization(monkeypatch):
    import ast
    from pathlib import Path
    source=Path(__file__).parents[1]/"core/websocket_server.py"
    tree=ast.parse(source.read_text())
    cls=next(node for node in tree.body if isinstance(node,ast.ClassDef) and node.name=="WebSocketServer")
    method=next(node for node in cls.body if isinstance(node,ast.AsyncFunctionDef) and node.name=="_handle_connection")
    audio=Mock(side_effect=AssertionError("management must not initialize audio"))
    namespace={"asyncio":__import__("asyncio"),"websockets":websockets,"ConnectionHandler":audio,"AuthenticationError":RuntimeError}
    exec(compile(ast.Module(body=[method],type_ignores=[]),str(source),"exec"),namespace)
    instance=server();instance._handle_auth=AsyncMock(side_effect=RuntimeError("device denied"))
    socket=SimpleNamespace(request=SimpleNamespace(path=management.PATH,headers={"authorization":"Bearer fixture-service-secret"}),
                           recv=AsyncMock(return_value='{"type":"server","action":"update_config"}'),send=AsyncMock(),close=AsyncMock())
    await namespace["_handle_connection"](instance,socket)
    instance._handle_auth.assert_not_awaited();audio.assert_not_called()
    instance.update_config.assert_awaited_once()
    socket.request.path="/xiaozhi/v1/";socket.request.headers["device-id"]="not-a-device"
    instance.update_config.reset_mock()
    await namespace["_handle_connection"](instance,socket)
    instance._handle_auth.assert_awaited_once();instance.update_config.assert_not_awaited();audio.assert_not_called()
