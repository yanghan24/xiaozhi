"""统一工具处理器"""

import json
from typing import Dict, List, Any, Optional
from config.logger import setup_logging
from plugins_func.loadplugins import auto_import_modules

from .base import ToolType
from plugins_func.register import Action, ActionResponse
from .unified_tool_manager import ToolManager
from .server_plugins import ServerPluginExecutor
from .server_mcp import ServerMCPExecutor
from .device_iot import DeviceIoTExecutor
from .device_mcp import DeviceMCPExecutor
from .mcp_endpoint import MCPEndpointExecutor
from core.handle.sendAudioHandle import send_display_message
from core.product_knowledge import (
    ProductKnowledgeScope, PRODUCT_KNOWLEDGE_PROMPT, KNOWLEDGE_UNAVAILABLE, KNOWLEDGE_FAILED,
)
from core.product_voice import VoiceDenied


class UnifiedToolHandler:
    """统一工具处理器"""

    def __init__(self, conn):
        self.conn = conn
        self.config = conn.config
        self.logger = setup_logging()

        self.product_scope = (ProductKnowledgeScope(conn)
                              if getattr(conn, "product_voice_gate", None) else None)
        self.finish_init = False
        self._closed = False
        if self.product_scope is not None:
            self.server_mcp_executor = ServerMCPExecutor(
                conn, servers=self.product_scope.servers(), allowed_tools={"ask_knowledge"})
            return

        # 创建工具管理器
        self.tool_manager = ToolManager(conn)

        # 创建各类执行器
        self.server_plugin_executor = ServerPluginExecutor(conn)
        self.server_mcp_executor = ServerMCPExecutor(conn)
        self.device_iot_executor = DeviceIoTExecutor(conn)
        self.device_mcp_executor = DeviceMCPExecutor(conn)
        self.mcp_endpoint_executor = MCPEndpointExecutor(conn)

        # 注册执行器
        self.tool_manager.register_executor(
            ToolType.SERVER_PLUGIN, self.server_plugin_executor
        )
        self.tool_manager.register_executor(
            ToolType.SERVER_MCP, self.server_mcp_executor
        )
        self.tool_manager.register_executor(
            ToolType.DEVICE_IOT, self.device_iot_executor
        )
        self.tool_manager.register_executor(
            ToolType.DEVICE_MCP, self.device_mcp_executor
        )
        self.tool_manager.register_executor(
            ToolType.MCP_ENDPOINT, self.mcp_endpoint_executor
        )

        # 初始化标志
        self.finish_init = False

    async def _initialize(self):
        """异步初始化"""
        if self.product_scope is not None:
            try:
                if not self._closed and self.product_scope.matches():
                    await self.conn.product_voice_gate.check_async()
                    if not self._closed and self.product_scope.matches():
                        await self.server_mcp_executor.initialize()
                        self.finish_init = not self._closed
            except Exception:
                self.logger.warning("产品知识库初始化不可用")
            return
        try:
            # 自动导入插件模块
            auto_import_modules("plugins_func.functions")

            # 初始化服务端MCP
            await self.server_mcp_executor.initialize()

            # 初始化MCP接入点
            await self._initialize_mcp_endpoint()

            # 初始化Home Assistant（如果需要）
            self._initialize_home_assistant()

            self.finish_init = True
            self.logger.debug("统一工具处理器初始化完成")

            # 输出当前支持的所有工具列表
            self.current_support_functions()

        except Exception as e:
            self.logger.error(f"统一工具处理器初始化失败: {e}")

    async def _initialize_mcp_endpoint(self):
        """初始化MCP接入点"""
        try:
            from .mcp_endpoint import connect_mcp_endpoint

            # 从配置中获取MCP接入点URL
            mcp_endpoint_url = self.config.get("mcp_endpoint", "")

            if (
                mcp_endpoint_url
                and "你的" not in mcp_endpoint_url
                and mcp_endpoint_url != "null"
            ):
                self.logger.info(f"正在初始化MCP接入点: {mcp_endpoint_url}")
                mcp_endpoint_client = await connect_mcp_endpoint(
                    mcp_endpoint_url, self.conn
                )

                if mcp_endpoint_client:
                    # 将MCP接入点客户端保存到连接对象中
                    self.conn.mcp_endpoint_client = mcp_endpoint_client
                    self.logger.info("MCP接入点初始化成功")
                else:
                    self.logger.warning("MCP接入点初始化失败")

        except Exception as e:
            self.logger.error(f"初始化MCP接入点失败: {e}")

    def _initialize_home_assistant(self):
        """初始化Home Assistant提示词"""
        try:
            from plugins_func.functions.hass_init import append_devices_to_prompt

            append_devices_to_prompt(self.conn)
        except ImportError:
            pass  # 忽略导入错误
        except Exception as e:
            self.logger.error(f"初始化Home Assistant失败: {e}")

    @property
    def knowledge_status(self):
        """Advisory readiness; exposure and execution still recheck live permission."""
        if (self.product_scope is not None and self.finish_init and self.product_scope.matches()
                and self.server_mcp_executor.has_tool("ask_knowledge")):
            return "available"
        return "unavailable"

    @property
    def knowledge_prompt(self):
        return PRODUCT_KNOWLEDGE_PROMPT + (
            "" if self.knowledge_status == "available" else KNOWLEDGE_UNAVAILABLE)

    def get_functions(self) -> List[Dict[str, Any]]:
        """获取所有工具的函数描述"""
        if self.product_scope is not None:
            if not self.finish_init or not self.product_scope.matches():
                return []
            try:
                self.conn.product_voice_gate.check()
            except VoiceDenied:
                return []
            if self.product_scope.matches() and self.server_mcp_executor.has_tool("ask_knowledge"):
                return self.product_scope.functions()
            return []
        return self.tool_manager.get_function_descriptions()

    def current_support_functions(self) -> List[str]:
        """获取当前支持的函数名称列表"""
        if self.product_scope is not None:
            return [item["function"]["name"] for item in self.get_functions()]
        func_names = self.tool_manager.get_supported_tool_names()
        self.logger.info(f"当前支持的函数列表: {func_names}")
        return func_names

    def upload_functions_desc(self):
        """刷新函数描述列表"""
        if self.product_scope is not None:
            return
        self.tool_manager.refresh_tools()
        self.logger.info("函数描述列表已刷新")

    def has_tool(self, tool_name: str) -> bool:
        """检查是否有指定工具"""
        if self.product_scope is not None:
            return tool_name == "ask_knowledge" and bool(self.get_functions())
        return self.tool_manager.has_tool(tool_name)

    async def handle_llm_function_call(
        self, conn, function_call_data: Dict[str, Any], *, turn_id=None
    ) -> Optional[ActionResponse]:
        """处理LLM函数调用"""
        if self.product_scope is not None:
            if conn is not self.conn or not await self._product_call_is_current(turn_id):
                return ActionResponse(Action.NONE)
            arguments = self.product_scope.arguments(function_call_data)
            if arguments is None:
                return ActionResponse(Action.ERROR, response=KNOWLEDGE_FAILED)
            response = await self.server_mcp_executor.execute(
                self.conn, "ask_knowledge", arguments)
            if not await self._product_call_is_current(turn_id):
                return ActionResponse(Action.NONE)
            if response.action != Action.REQLLM:
                return ActionResponse(Action.ERROR, response=KNOWLEDGE_FAILED)
            return response
        try:
            # 处理多函数调用
            if "function_calls" in function_call_data:
                responses = []
                for call in function_call_data["function_calls"]:
                    result = await self.tool_manager.execute_tool(
                        call["name"], call.get("arguments", {})
                    )
                    responses.append(result)
                return self._combine_responses(responses)

            # 处理单函数调用
            function_name = function_call_data["name"]
            arguments = function_call_data.get("arguments", {})

            # 如果arguments是字符串，尝试解析为JSON
            if isinstance(arguments, str):
                try:
                    arguments = json.loads(arguments) if arguments else {}
                except json.JSONDecodeError:
                    self.logger.error(f"无法解析函数参数: {arguments}")
                    return ActionResponse(
                        action=Action.ERROR,
                        response="无法解析函数参数",
                    )

            self.logger.debug(f"调用函数: {function_name}, 参数: {arguments}")

            # 发送工具调用显示消息到设备
            try:
                await send_display_message(self.conn, f"% {function_name}")
            except Exception as e:
                self.logger.warning(f"发送工具调用显示消息失败: {e}")

            # 执行工具调用
            result = await self.tool_manager.execute_tool(function_name, arguments)
            return result

        except Exception as e:
            self.logger.error(f"处理function call错误: {e}")
            return ActionResponse(action=Action.ERROR, response=str(e))

    async def _product_call_is_current(self, turn_id):
        def current():
            return (turn_id is not None and self.finish_init and self.product_scope.matches()
                    and self.conn.product_turn_is_current(turn_id))
        if not current():
            return False
        try:
            await self.conn.product_voice_gate.check_async()
        except VoiceDenied:
            return False
        return current()

    def _combine_responses(self, responses: List[ActionResponse]) -> ActionResponse:
        """合并多个函数调用的响应"""
        if not responses:
            return ActionResponse(action=Action.NONE, response="无响应")

        # 如果有任何错误，返回第一个错误
        for response in responses:
            if response.action == Action.ERROR:
                return response

        # 合并所有成功的响应
        contents = []
        responses_text = []

        for response in responses:
            if response.content:
                contents.append(response.content)
            if response.response:
                responses_text.append(response.response)

        # 确定最终的动作类型
        final_action = Action.RESPONSE
        for response in responses:
            if response.action == Action.REQLLM:
                final_action = Action.REQLLM
                break

        return ActionResponse(
            action=final_action,
            result="; ".join(contents) if contents else None,
            response="; ".join(responses_text) if responses_text else None,
        )

    async def register_iot_tools(self, descriptors: List[Dict[str, Any]]):
        """注册IoT设备工具"""
        if self.product_scope is not None:
            return
        self.device_iot_executor.register_iot_tools(descriptors)
        self.tool_manager.refresh_tools()
        self.logger.info(f"注册了{len(descriptors)}个IoT设备的工具")

    def get_tool_statistics(self) -> Dict[str, int]:
        """获取工具统计信息"""
        if self.product_scope is not None:
            return {"server_mcp": len(self.get_functions())}
        return self.tool_manager.get_tool_statistics()

    async def cleanup(self):
        """清理资源"""
        if self.product_scope is not None:
            self._closed = True
            self.finish_init = False
            await self.server_mcp_executor.cleanup()
            return
        try:
            await self.server_mcp_executor.cleanup()

            # 清理MCP接入点连接
            if (
                hasattr(self.conn, "mcp_endpoint_client")
                and self.conn.mcp_endpoint_client
            ):
                await self.conn.mcp_endpoint_client.close()

            self.logger.info("工具处理器清理完成")
        except Exception as e:
            self.logger.error(f"工具处理器清理失败: {e}")
