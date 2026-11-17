"""Local MCP protocol fixture through the original multi-tool manager/client."""
import asyncio
import socket
import threading
from types import SimpleNamespace

import pytest
import uvicorn
from mcp.server.mcpserver import MCPServer
from core.providers.tools.server_mcp.mcp_manager import ServerMCPManager


@pytest.mark.asyncio
async def test_catalogue_discovers_and_executes_generic_tools_and_ignores_global_file():
    service = MCPServer('local-protocol-fixture')

    @service.tool()
    def multiply(left: int, right: int) -> int:
        """Multiply two integers for the protocol check."""
        return left * right

    @service.tool()
    def greeting(name: str) -> str:
        """Return a fixture greeting."""
        return 'hello ' + name

    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    server = uvicorn.Server(uvicorn.Config(service.streamable_http_app(stateless_http=True), log_level='critical'))
    thread = threading.Thread(target=lambda: server.run(sockets=[listener]), daemon=True)
    thread.start()
    manager = ServerMCPManager(SimpleNamespace(config={'speaker_identity_enabled': True, 'mcp_servers': {
        'fixture': {'url': f'http://127.0.0.1:{listener.getsockname()[1]}/mcp',
                    'transport': 'streamable-http', 'tool_prefix': 'fixture_'}
    }}))
    try:
        for _ in range(100):
            if server.started:
                break
            await asyncio.sleep(.02)
        assert server.started
        await manager.initialize_servers()
        assert {tool['function']['name'] for tool in manager.get_all_tools()} == {'fixture_multiply', 'fixture_greeting'}
        result = await manager.execute_tool('fixture_multiply', {'left': 6, 'right': 7})
        assert result.is_error is False
        assert '42' in result.content[0].text
        result = await manager.execute_tool('fixture_greeting', {'name': 'speaker'})
        assert 'hello speaker' in result.content[0].text
        empty = ServerMCPManager(SimpleNamespace(config={'speaker_identity_enabled': True, 'mcp_servers': {}}))
        assert empty.load_config() == {}
    finally:
        await manager.cleanup_all()
        server.should_exit = True
        await asyncio.to_thread(thread.join, 5)
        listener.close()
