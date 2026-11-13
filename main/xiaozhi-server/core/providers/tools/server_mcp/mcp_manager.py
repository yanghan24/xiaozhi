"""服务端MCP管理器"""

import asyncio
import os
import json
from typing import Dict, Any, List, Set

from mcp.types import LoggingMessageNotificationParams

from config.config_loader import get_project_dir
from config.logger import setup_logging
from .mcp_client import ServerMCPClient
from .errors import (
    MCPToolExecutionError,
    ToolNotFoundError,
    MCP_EXECUTION_FAILED_MESSAGE,
    MCP_TOOL_NOT_FOUND_MESSAGE,
)

TAG = __name__
logger = setup_logging()

_INIT_TIMEOUT_SECONDS = 10
_CLEANUP_TIMEOUT_SECONDS = 20


class _ToolRegistrationRejected(Exception):
    """工具注册被拒绝，仅用于管理器内部控制流。"""


class _ManagerClosed(Exception):
    """管理器已关闭，不再接受初始化或注册。"""


class ServerMCPManager:
    """管理多个服务端MCP服务的集中管理器"""

    def __init__(self, conn, *, servers=None, allowed_tools=None) -> None:
        """初始化MCP管理器"""
        self.conn = conn
        self._servers = servers
        self._allowed_tools = None if allowed_tools is None else frozenset(allowed_tools)
        self.config_path = ""
        if servers is None:
            self.config_path = get_project_dir() + "data/.mcp_server_settings.json"
            if not os.path.exists(self.config_path):
                self.config_path = ""
                logger.bind(tag=TAG).debug("MCP服务配置文件不存在")
        self.clients: Dict[str, ServerMCPClient] = {}
        self.tools = []
        self._init_lock = asyncio.Lock()
        self._closed = False
        self._pending_clients: Set[ServerMCPClient] = set()

    def load_config(self) -> Dict[str, Any]:
        """加载MCP服务配置"""
        if self._servers is not None:
            return self._servers
        if len(self.config_path) == 0:
            return {}

        try:
            with open(self.config_path, "r", encoding="utf-8") as f:
                config = json.load(f)
            servers = config.get("mcpServers", {})
            return servers if isinstance(servers, dict) else {}
        except Exception:
            logger.bind(tag=TAG).error("MCP服务配置加载失败")
            return {}

    @staticmethod
    def _tool_names(tools: List[Dict[str, Any]]) -> Set[str]:
        names: Set[str] = set()
        for tool in tools:
            if not isinstance(tool, dict):
                continue
            function = tool.get("function")
            if not isinstance(function, dict):
                continue
            name = function.get("name")
            if isinstance(name, str) and name:
                names.add(name)
        return names

    async def _init_server(self, name: str, srv_config: Dict[str, Any]):
        """初始化单个MCP服务；关闭、失败或冲突时不注册任何工具。"""
        client = ServerMCPClient(srv_config)
        registered = False
        cleaned = False
        try:
            async with self._init_lock:
                if self._closed:
                    raise _ManagerClosed()
                self._pending_clients.add(client)

            logger.bind(tag=TAG).info("开始初始化服务端MCP客户端")

            await asyncio.wait_for(
                client.initialize(logging_callback=self.logging_callback),
                timeout=_INIT_TIMEOUT_SECONDS,
            )
            client_tools = self._client_tools(client)

            async with self._init_lock:
                if self._closed:
                    raise _ManagerClosed()
                if name in self.clients:
                    raise _ToolRegistrationRejected()

                existing_names = self._tool_names(self.tools)
                new_names: Set[str] = set()
                for tool in client_tools:
                    if not isinstance(tool, dict):
                        raise _ToolRegistrationRejected()
                    function = tool.get("function")
                    if not isinstance(function, dict):
                        raise _ToolRegistrationRejected()
                    tool_name = function.get("name")
                    if not isinstance(tool_name, str) or not tool_name:
                        raise _ToolRegistrationRejected()
                    if tool_name in existing_names or tool_name in new_names:
                        raise _ToolRegistrationRejected()
                    new_names.add(tool_name)

                # 只有完整校验通过才一次性提交，失败不会留下半套客户端或工具。
                self.clients[name] = client
                self.tools = self.tools + client_tools
                registered = True

            logger.bind(tag=TAG).info(
                f"服务端MCP客户端初始化成功，工具数量: {len(client_tools)}"
            )

        except asyncio.TimeoutError:
            logger.bind(tag=TAG).error("服务端MCP客户端初始化超时")
        except asyncio.CancelledError:
            raise
        except _ManagerClosed:
            logger.bind(tag=TAG).warning("服务端MCP管理器已关闭，拒绝初始化")
        except _ToolRegistrationRejected:
            logger.bind(tag=TAG).warning("服务端MCP工具名称冲突，已拒绝注册")
        except Exception:
            logger.bind(tag=TAG).error("服务端MCP客户端初始化失败")
        finally:
            if not registered:
                cleaned = await self._cleanup_client(client)
            if registered or cleaned:
                async with self._init_lock:
                    self._pending_clients.discard(client)

    async def initialize_servers(self) -> None:
        """初始化所有MCP服务"""
        config = self.load_config()
        tasks = []
        skipped = 0
        for name, srv_config in config.items():
            if not isinstance(srv_config, dict):
                skipped += 1
                continue
            if not srv_config.get("command") and not srv_config.get("url"):
                skipped += 1
                continue

            tasks.append(self._init_server(name, srv_config))

        if skipped:
            logger.bind(tag=TAG).warning(
                f"跳过 {skipped} 个配置不完整或无效的MCP服务"
            )

        if tasks:
            await asyncio.gather(*tasks)

        # 输出当前支持的服务端MCP工具列表
        if self._servers is None and hasattr(self.conn, "func_handler") and self.conn.func_handler:
            # 刷新工具缓存以确保服务端MCP工具被正确加载
            if hasattr(self.conn.func_handler, "tool_manager"):
                self.conn.func_handler.tool_manager.refresh_tools()
            self.conn.func_handler.current_support_functions()

    def get_all_tools(self) -> List[Dict[str, Any]]:
        """获取所有服务的工具function定义"""
        return self.tools

    def _client_tools(self, client):
        tools = client.get_available_tools()
        if self._allowed_tools is None:
            return tools
        # The SDK client sanitizes names. A different original name must never
        # acquire a restricted tool's authority through that alias.
        return [tool for tool in tools
                if tool["function"]["name"] in self._allowed_tools
                and client.name_mapping.get(tool["function"]["name"]) == tool["function"]["name"]]

    def is_mcp_tool(self, tool_name: str) -> bool:
        """检查是否是MCP工具"""
        for tool in self.tools:
            if not isinstance(tool, dict):
                continue
            function = tool.get("function")
            if isinstance(function, dict) and function.get("name") == tool_name:
                return True
        return False

    async def execute_tool(self, tool_name: str, arguments: Dict[str, Any]) -> Any:
        """执行工具调用。单次提交，不自动重放结果未知的调用。"""
        logger.bind(tag=TAG).info("开始执行服务端MCP工具")
        if self._closed:
            raise MCPToolExecutionError(MCP_EXECUTION_FAILED_MESSAGE)
        if self._allowed_tools is not None and not self.is_mcp_tool(tool_name):
            raise ToolNotFoundError(MCP_TOOL_NOT_FOUND_MESSAGE)

        target_client = None
        for client in list(self.clients.values()):
            if client.has_tool(tool_name):
                target_client = client
                break

        if target_client is None:
            raise ToolNotFoundError(MCP_TOOL_NOT_FOUND_MESSAGE)

        try:
            return await target_client.call_tool(
                tool_name, arguments, progress_callback=self.progress_callback
            )
        except asyncio.CancelledError:
            raise
        except ToolNotFoundError:
            raise
        except Exception:
            logger.bind(tag=TAG).error(MCP_EXECUTION_FAILED_MESSAGE)
            raise MCPToolExecutionError(MCP_EXECUTION_FAILED_MESSAGE) from None

    async def _cleanup_client(self, client: ServerMCPClient) -> bool:
        """清理单个客户端；自身超时按失败返回，调用方取消继续传播。"""
        try:
            await asyncio.wait_for(
                client.cleanup(), timeout=_CLEANUP_TIMEOUT_SECONDS
            )
            return True
        except asyncio.TimeoutError:
            logger.bind(tag=TAG).error("服务端MCP客户端关闭超时")
            return False
        except asyncio.CancelledError:
            raise
        except Exception:
            logger.bind(tag=TAG).error("服务端MCP客户端关闭失败")
            return False

    def _refresh_tools_from_clients(self) -> None:
        """只保留仍留在管理器中的客户端工具，避免清理后残留。"""
        tools = []
        for client in self.clients.values():
            try:
                tools.extend(self._client_tools(client))
            except Exception:
                logger.bind(tag=TAG).error("服务端MCP工具列表刷新失败")
        self.tools = tools

    async def cleanup_all(self) -> None:
        """关闭所有 MCP客户端；取消时保留未完成client便于重试。"""
        self._closed = True
        async with self._init_lock:
            registered = list(self.clients.items())
            pending = list(self._pending_clients)

        cleaned = 0
        seen: Set[int] = set()
        try:
            for name, client in registered:
                if id(client) in seen:
                    continue
                seen.add(id(client))
                if await self._cleanup_client(client):
                    cleaned += 1
                    async with self._init_lock:
                        if self.clients.get(name) is client:
                            del self.clients[name]
                        self._pending_clients = {
                            pending_client for pending_client in self._pending_clients
                            if pending_client is not client
                        }

            for client in pending:
                if id(client) in seen:
                    continue
                seen.add(id(client))
                if await self._cleanup_client(client):
                    cleaned += 1
                    async with self._init_lock:
                        self._pending_clients.discard(client)
        finally:
            self._refresh_tools_from_clients()

        if cleaned:
            logger.bind(tag=TAG).info(
                f"服务端MCP客户端已清理，数量: {cleaned}"
            )

    # 可选回调方法

    async def logging_callback(self, params: LoggingMessageNotificationParams):
        # 服务端通知内容不进入日志。
        logger.bind(tag=TAG).info("收到服务端MCP日志通知")

    async def progress_callback(self, progress: float, total: float | None, message: str | None) -> None:
        # 进度文本可能包含外部数据，不进入日志。
        logger.bind(tag=TAG).info("收到服务端MCP进度通知")
