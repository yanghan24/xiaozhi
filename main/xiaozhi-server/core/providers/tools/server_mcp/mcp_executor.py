"""服务端MCP工具执行器"""

import asyncio
from typing import Dict, Any, Optional
from ..base import ToolType, ToolDefinition, ToolExecutor
from plugins_func.register import Action, ActionResponse
from .mcp_manager import ServerMCPManager
from .errors import ToolNotFoundError

_MCP_NOT_INITIALIZED = "MCP管理器未初始化"
_MCP_TOOL_NOT_FOUND = "MCP工具不存在"
_MCP_TOOL_FAILED = "MCP工具执行失败"
_MCP_INVALID_RESULT = "MCP工具未返回有效结果"


def _is_valid_content_block(block: Any) -> bool:
    """只校验内容块类型字段，不读取其业务内容。"""
    if isinstance(block, dict):
        block_type = block.get("type")
        return isinstance(block_type, str) and bool(block_type)
    block_type = getattr(block, "type", None)
    return isinstance(block_type, str) and bool(block_type)


def _is_valid_call_tool_result(result: Any) -> bool:
    """只按MCP协议结构判断结果是否可用，不解析业务payload。"""
    if result is None or isinstance(result, (dict, list, str, bytes)):
        return False
    missing = object()
    error_fields = [getattr(result, name, missing) for name in ("is_error", "isError")]
    present = [value for value in error_fields if value is not missing]
    if not present or any(value is not False for value in present):
        return False
    content = getattr(result, "content", None)
    if not isinstance(content, list):
        return False
    if not content:
        # MCP structured-only results are valid too; no knowledge-specific
        # status/generated assumptions belong in the generic executor.
        return any(isinstance(getattr(result, name, None), dict)
                   for name in ("structured_content", "structuredContent"))
    return all(_is_valid_content_block(block) for block in content)


class ServerMCPExecutor(ToolExecutor):
    """服务端MCP工具执行器"""

    def __init__(self, conn, *, servers=None, allowed_tools=None):
        self.conn = conn
        self._servers = servers
        self._allowed_tools = allowed_tools
        self.mcp_manager: Optional[ServerMCPManager] = None
        self._initialized = False

    async def initialize(self):
        """初始化MCP管理器"""
        if not self._initialized:
            self.mcp_manager = ServerMCPManager(
                self.conn, servers=self._servers, allowed_tools=self._allowed_tools)
            self._initialized = True
            await self.mcp_manager.initialize_servers()

    async def execute(
        self, conn, tool_name: str, arguments: Dict[str, Any]
    ) -> ActionResponse:
        """执行服务端MCP工具"""
        if not self._initialized or not self.mcp_manager:
            return ActionResponse(
                action=Action.ERROR,
                response=_MCP_NOT_INITIALIZED,
            )

        # 移除mcp_前缀（如果有）
        actual_tool_name = tool_name
        if tool_name.startswith("mcp_"):
            actual_tool_name = tool_name[4:]

        try:
            result = await self.mcp_manager.execute_tool(actual_tool_name, arguments)
        except ToolNotFoundError:
            return ActionResponse(
                action=Action.NOTFOUND,
                response=_MCP_TOOL_NOT_FOUND,
            )
        except asyncio.CancelledError:
            raise
        except Exception:
            return ActionResponse(
                action=Action.ERROR,
                response=_MCP_TOOL_FAILED,
            )

        try:
            valid_result = _is_valid_call_tool_result(result)
        except Exception:
            valid_result = False

        if not valid_result:
            return ActionResponse(
                action=Action.ERROR,
                response=_MCP_INVALID_RESULT,
            )

        try:
            rendered = str(result)
        except Exception:
            return ActionResponse(
                action=Action.ERROR,
                response=_MCP_INVALID_RESULT,
            )

        return ActionResponse(action=Action.REQLLM, result=rendered)

    def get_tools(self) -> Dict[str, ToolDefinition]:
        """获取所有服务端MCP工具"""
        if not self._initialized or not self.mcp_manager:
            return {}

        tools = {}
        mcp_tools = self.mcp_manager.get_all_tools()

        for tool in mcp_tools:
            if not isinstance(tool, dict):
                continue
            func_def = tool.get("function")
            if not isinstance(func_def, dict):
                continue
            tool_name = func_def.get("name", "")
            if not isinstance(tool_name, str) or tool_name == "":
                continue
            tools[tool_name] = ToolDefinition(
                name=tool_name, description=tool, tool_type=ToolType.SERVER_MCP
            )

        return tools

    def has_tool(self, tool_name: str) -> bool:
        """检查是否有指定的服务端MCP工具"""
        if not self._initialized or not self.mcp_manager:
            return False

        # 移除mcp_前缀（如果有）
        actual_tool_name = tool_name
        if tool_name.startswith("mcp_"):
            actual_tool_name = tool_name[4:]

        return self.mcp_manager.is_mcp_tool(actual_tool_name)

    async def cleanup(self):
        """清理MCP连接"""
        if self.mcp_manager:
            await self.mcp_manager.cleanup_all()
