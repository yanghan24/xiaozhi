"""Local MCP protocol fixture through the original tool dispatcher."""
import asyncio
import socket
import threading
from types import SimpleNamespace

import pytest
import uvicorn
from mcp.server.mcpserver import MCPServer
from core.providers.tools.base import ToolType
from core.providers.tools.server_mcp import ServerMCPExecutor
from core.providers.tools.server_mcp.mcp_manager import ServerMCPManager
from core.providers.tools.unified_tool_manager import ToolManager
from plugins_func.register import Action


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
    conn = SimpleNamespace(config={
        'speaker_identity_enabled': True,
        'mcp_servers': {
            'fixture': {
                'url': f'http://127.0.0.1:{listener.getsockname()[1]}/mcp',
                'transport': 'streamable-http',
                # A configuration from before the Java update may still carry this field.
                'tool_prefix': 'mcp_0123456789ab_',
            },
        },
    })
    executor = ServerMCPExecutor(conn)
    tools = ToolManager(conn)
    tools.register_executor(ToolType.SERVER_MCP, executor)
    try:
        for _ in range(100):
            if server.started:
                break
            await asyncio.sleep(.02)
        assert server.started
        await executor.initialize()
        assert {tool['function']['name'] for tool in tools.get_function_descriptions()} == {'multiply', 'greeting'}
        result = await tools.execute_tool('multiply', {'left': 6, 'right': 7})
        assert result.action == Action.REQLLM
        assert '42' in result.result
        result = await tools.execute_tool('greeting', {'name': 'speaker'})
        assert result.action == Action.REQLLM
        assert 'hello speaker' in result.result
        empty = ServerMCPManager(SimpleNamespace(config={'speaker_identity_enabled': True, 'mcp_servers': {}}))
        assert empty.load_config() == {}
    finally:
        await executor.cleanup()
        server.should_exit = True
        await asyncio.to_thread(thread.join, 5)
        listener.close()
