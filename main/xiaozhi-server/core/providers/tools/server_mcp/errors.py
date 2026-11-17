"""服务端MCP安全异常。

对外异常只携带固定文案，不包含上游异常原文、URL、请求参数或服务端通知内容。
"""

MCP_INIT_FAILED_MESSAGE = "服务端MCP客户端初始化失败"
MCP_NOT_CONNECTED_MESSAGE = "服务端MCP客户端未连接"
MCP_EXECUTION_FAILED_MESSAGE = "服务端MCP工具执行失败"
MCP_TOOL_NOT_FOUND_MESSAGE = "服务端MCP工具不存在"


class MCPClientError(RuntimeError):
    """服务端MCP客户端的安全异常基类。"""


class MCPInitializationError(MCPClientError):
    """服务端MCP客户端初始化失败。"""


class MCPNotConnectedError(MCPClientError):
    """服务端MCP客户端未连接或已关闭。"""


class MCPToolExecutionError(MCPClientError):
    """服务端MCP工具执行失败。"""


class ToolNotFoundError(ValueError):
    """请求的服务端MCP工具未注册。"""
