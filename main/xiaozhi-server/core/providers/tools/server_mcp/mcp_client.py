"""服务端MCP客户端"""

from __future__ import annotations

from datetime import timedelta
import asyncio
import os
import shutil
import concurrent.futures
from contextlib import AsyncExitStack
from typing import Optional, List, Dict, Any

import httpx2
from mcp import ClientSession, StdioServerParameters, Implementation
from mcp.client.session import SamplingFnT, ElicitationFnT, ListRootsFnT, LoggingFnT, MessageHandlerFnT
from mcp.client.stdio import stdio_client
from mcp.client.sse import sse_client
from mcp.client.streamable_http import streamable_http_client
from mcp.shared.dispatcher import ProgressFnT  # mcp 2.x：从 mcp.shared.session 移来

from config.logger import setup_logging
from core.utils.util import sanitize_tool_name
from .errors import (
    MCPInitializationError,
    MCPNotConnectedError,
    MCP_INIT_FAILED_MESSAGE,
    MCP_NOT_CONNECTED_MESSAGE,
)

TAG = __name__

_WORKER_GRACE_SECONDS = 15
_WORKER_CANCEL_WAIT_SECONDS = 2


class _WorkerAttempt:
    """一次连接尝试的独立状态，避免初始化与清理相互覆盖。"""

    def __init__(self) -> None:
        self.ready_evt = asyncio.Event()
        self.shutdown_evt = asyncio.Event()
        self.task: Optional[asyncio.Task] = None
        self.startup_error: Optional[BaseException] = None
        self.ready_ok = False
        self.closing = False
        self.session: Optional[ClientSession] = None
        self.tools: List = []
        self.tools_dict: Dict[str, Any] = {}
        self.name_mapping: Dict[str, str] = {}


class ServerMCPClient:
    """服务端MCP客户端，用于连接和管理MCP服务"""

    def __init__(self, config: Dict[str, Any]):
        """初始化服务端MCP客户端

        Args:
            config: MCP服务配置字典
        """
        self.logger = setup_logging()
        self.config = config

        self._init_lock = asyncio.Lock()
        self._attempt: Optional[_WorkerAttempt] = None
        self._worker_task: Optional[asyncio.Task] = None
        self._ready_evt = asyncio.Event()
        self._shutdown_evt = asyncio.Event()
        self._startup_error: Optional[BaseException] = None

        self.session: Optional[ClientSession] = None
        self.tools: List = []  # 原始工具对象
        self.tools_dict: Dict[str, Any] = {}
        self.name_mapping: Dict[str, str] = {}

    async def initialize(self, read_timeout_seconds: timedelta | None = None,
             sampling_callback: SamplingFnT | None = None,
             elicitation_callback: ElicitationFnT | None = None,
             list_roots_callback: ListRootsFnT | None = None,
             logging_callback: LoggingFnT | None = None,
             message_handler: MessageHandlerFnT | None = None,
             client_info: Implementation | None = None):
        """初始化MCP客户端连接。只有传输、initialize 和 list_tools 全部成功才返回。"""
        async with self._init_lock:
            attempt = self._attempt
            if attempt is not None and attempt.task is not None and not attempt.task.done():
                if attempt.closing:
                    raise MCPNotConnectedError(MCP_NOT_CONNECTED_MESSAGE)
                # 已有初始化在进行，复用同一个 ready 事件等待。
                pass
            else:
                if attempt is not None:
                    self._release_attempt(attempt)
                attempt = _WorkerAttempt()
                attempt.task = asyncio.create_task(
                    self._worker(read_timeout_seconds=read_timeout_seconds,
                                sampling_callback=sampling_callback,
                                elicitation_callback=elicitation_callback,
                                list_roots_callback=list_roots_callback,
                                logging_callback=logging_callback,
                                message_handler=message_handler,
                                client_info=client_info,
                                attempt=attempt),
                    name="ServerMCPClientWorker",
                )
                self._attempt = attempt
                self._worker_task = attempt.task
                self._ready_evt = attempt.ready_evt
                self._shutdown_evt = attempt.shutdown_evt
                self._startup_error = None
                self.session = None
                self.tools = []
                self.tools_dict = {}
                self.name_mapping = {}

        await attempt.ready_evt.wait()

        # 与 cleanup 使用同一把锁发布状态；清理已开始时不得把旧状态重新写回。
        async with self._init_lock:
            if attempt.startup_error is not None:
                if self._attempt is attempt:
                    self._startup_error = attempt.startup_error
                raise MCPInitializationError(MCP_INIT_FAILED_MESSAGE) from None

            if (attempt.closing or self._attempt is not attempt
                    or not attempt.ready_ok or attempt.session is None):
                raise MCPNotConnectedError(MCP_NOT_CONNECTED_MESSAGE)

            self.session = attempt.session
            self.tools = attempt.tools
            self.tools_dict = attempt.tools_dict
            self.name_mapping = attempt.name_mapping
            tool_count = len(self.tools_dict)

        self.logger.bind(tag=TAG).info(
            f"服务端MCP客户端已连接，工具数量: {tool_count}"
        )

    async def cleanup(self):
        """清理MCP客户端资源，有界等待并取消未退出的 worker。"""
        async with self._init_lock:
            attempt = self._attempt
            task = attempt.task if attempt is not None else self._worker_task

            if attempt is not None:
                attempt.closing = True
                attempt.shutdown_evt.set()

            try:
                if task is not None and not task.done():
                    await self._wait_for_worker(task)

                # 消费 worker 可能携带的异常，避免 Task exception was never retrieved。
                if task is not None and task.done() and not task.cancelled():
                    try:
                        task.exception()
                    except (asyncio.CancelledError, Exception):
                        pass
            finally:
                pending = task is not None and not task.done()
                if pending:
                    task.cancel()

                if attempt is not None:
                    attempt.ready_evt.set()
                    attempt.session = None
                    attempt.tools = []
                    attempt.tools_dict = {}
                    attempt.name_mapping = {}

                # 关闭尚未完成时保留原尝试，后续 cleanup 继续等待同一 worker。
                if not pending:
                    self._attempt = None
                    self._worker_task = None
                self._startup_error = None
                self.session = None
                self.tools = []
                self.tools_dict = {}
                self.name_mapping = {}

    async def _wait_for_worker(self, task: asyncio.Task) -> None:
        """先给 worker 较小的关闭宽限，再取消并做一次有界收尾。"""
        _done, pending = await asyncio.wait(
            {task}, timeout=_WORKER_GRACE_SECONDS
        )
        if not pending:
            return

        task.cancel()
        _done, pending = await asyncio.wait(
            {task}, timeout=_WORKER_CANCEL_WAIT_SECONDS
        )
        if pending:
            raise asyncio.TimeoutError("服务端MCP客户端关闭未完成")

    def _release_attempt(self, attempt: _WorkerAttempt) -> None:
        """释放已结束的旧尝试，允许受控重试。"""
        attempt.closing = True
        attempt.shutdown_evt.set()
        attempt.ready_evt.set()

        task = attempt.task
        if task is not None and task.done() and not task.cancelled():
            try:
                task.exception()
            except (asyncio.CancelledError, Exception):
                pass

    def _mark_startup_failure(self, attempt: _WorkerAttempt, log: bool = True) -> None:
        if attempt.startup_error is not None:
            return
        attempt.startup_error = MCPInitializationError(MCP_INIT_FAILED_MESSAGE)
        if log:
            self.logger.bind(tag=TAG).error(MCP_INIT_FAILED_MESSAGE)

    def has_tool(self, name: str) -> bool:
        """检查是否包含指定工具

        Args:
            name: 工具名称

        Returns:
            bool: 是否包含该工具
        """
        return name in self.tools_dict

    def get_available_tools(self) -> List[Dict[str, Any]]:
        """获取所有可用工具的定义

        Returns:
            List[Dict[str, Any]]: 工具定义列表
        """
        return [
            {
                "type": "function",
                "function": {
                    "name": name,
                    "description": tool.description,
                    "parameters": tool.input_schema,
                },
            }
            for name, tool in self.tools_dict.items()
        ]

    async def call_tool(self, name: str, arguments: dict, read_timeout_seconds: timedelta | None = None, progress_callback: ProgressFnT | None = None, *, meta: dict[str, Any] | None = None) -> Any:
        """调用指定工具

        Args:
            name: 工具名称
            arguments: 工具参数
            read_timeout_seconds:
            progress_callback: 进度回调函数
            meta:

        Returns:
            Any: 工具执行结果

        Raises:
            MCPNotConnectedError: 客户端未初始化或已关闭时抛出
        """
        attempt = self._attempt
        task = self._worker_task
        if (attempt is None or attempt is not self._attempt or task is None
                or attempt.task is not task or task.done()
                or not attempt.ready_ok or attempt.closing
                or self.session is None):
            raise MCPNotConnectedError(MCP_NOT_CONNECTED_MESSAGE)

        session = self.session
        real_name = self.name_mapping.get(name, name)
        loop = task.get_loop()
        timeout = (read_timeout_seconds.total_seconds()
                   if isinstance(read_timeout_seconds, timedelta) else read_timeout_seconds)
        coro = session.call_tool(real_name, arguments=arguments, read_timeout_seconds=timeout, progress_callback=progress_callback, meta=meta)

        if loop is asyncio.get_running_loop():
            return await coro

        try:
            fut: concurrent.futures.Future = asyncio.run_coroutine_threadsafe(coro, loop)
        except RuntimeError:
            raise MCPNotConnectedError(MCP_NOT_CONNECTED_MESSAGE) from None

        try:
            return await asyncio.wrap_future(fut)
        except asyncio.CancelledError:
            # 调用方取消会传播；同时撤销尚未在线程循环中开始的任务。
            fut.cancel()
            raise

    def is_connected(self) -> bool:
        """检查MCP客户端是否连接正常

        Returns:
            bool: 如果客户端已连接并正常工作，返回True，否则返回False
        """
        attempt = self._attempt
        task = attempt.task if attempt is not None else None
        return bool(
            attempt is not None
            and attempt.ready_ok
            and not attempt.closing
            and task is not None
            and not task.done()
            and self.session is not None
        )

    async def _worker(self, read_timeout_seconds: timedelta | None = None,
             sampling_callback: SamplingFnT | None = None,
             elicitation_callback: ElicitationFnT | None = None,
             list_roots_callback: ListRootsFnT | None = None,
             logging_callback: LoggingFnT | None = None,
             message_handler: MessageHandlerFnT | None = None,
             client_info: Implementation | None = None,
             attempt: Optional[_WorkerAttempt] = None):
        """MCP客户端工作协程"""
        if attempt is None:
            return

        try:
            if attempt.closing:
                return

            async with AsyncExitStack() as stack:
                # 建立 StdioClient
                if "command" in self.config:
                    cmd = (
                        shutil.which("npx")
                        if self.config["command"] == "npx"
                        else self.config["command"]
                    )
                    env = {**os.environ, **self.config.get("env", {})}
                    params = StdioServerParameters(
                        command=cmd,
                        args=self.config.get("args", []),
                        env=env,
                    )
                    stdio_r, stdio_w = await stack.enter_async_context(
                        stdio_client(params)
                    )
                    read_stream, write_stream = stdio_r, stdio_w

                # 建立SSEClient
                elif "url" in self.config:
                    headers = dict(self.config.get("headers", {}))
                    # TODO 兼容旧版本
                    if "API_ACCESS_TOKEN" in self.config:
                        headers["Authorization"] = f"Bearer {self.config['API_ACCESS_TOKEN']}"
                        self.logger.bind(tag=TAG).warning(
                            "检测到旧版 API_ACCESS_TOKEN 配置，请改用新的鉴权配置方式"
                        )

                    # 根据transport类型选择不同的客户端，默认为SSE
                    transport_type = self.config.get("transport", "sse")

                    if transport_type == "streamable-http" or transport_type == "http":
                        # 使用 Streamable HTTP 传输
                        # mcp 2.x：headers/timeout 改由 http_client 承载，产出从 3 元组变为 2 元组
                        http_client = httpx2.AsyncClient(
                            headers=headers,
                            timeout=httpx2.Timeout(
                                self.config.get("timeout", 30),
                                read=self.config.get("sse_read_timeout", 60 * 5),
                            ),
                        )
                        stack.push_async_callback(http_client.aclose)

                        http_r, http_w = await stack.enter_async_context(
                            streamable_http_client(
                                url=self.config["url"],
                                http_client=http_client,
                                terminate_on_close=self.config.get("terminate_on_close", True),
                            )
                        )
                        read_stream, write_stream = http_r, http_w
                    else:
                        # 使用传统的 SSE 传输
                        sse_r, sse_w = await stack.enter_async_context(
                            sse_client(
                                url=self.config["url"],
                                headers=headers,
                                timeout=self.config.get("timeout", 5),
                                sse_read_timeout=self.config.get("sse_read_timeout", 60 * 5)
                            )
                        )
                        read_stream, write_stream = sse_r, sse_w

                else:
                    self._mark_startup_failure(attempt)
                    return

                # MCP 2.3.0 接收秒数；保留现有调用方的 timedelta 入口。
                timeout = (read_timeout_seconds.total_seconds()
                           if isinstance(read_timeout_seconds, timedelta) else read_timeout_seconds)
                session = await stack.enter_async_context(
                    ClientSession(
                        read_stream=read_stream,
                        write_stream=write_stream,
                        read_timeout_seconds=timeout,
                        sampling_callback=sampling_callback,
                        elicitation_callback=elicitation_callback,
                        list_roots_callback=list_roots_callback,
                        logging_callback=logging_callback,
                        message_handler=message_handler,
                        client_info=client_info
                    )
                )
                attempt.session = session

                if attempt.closing:
                    return

                await session.initialize()

                if attempt.closing:
                    return

                # 获取工具
                tools = list((await session.list_tools()).tools)
                tools_dict: Dict[str, Any] = {}
                name_mapping: Dict[str, str] = {}
                for tool in tools:
                    sanitized = sanitize_tool_name(tool.name)
                    if sanitized in tools_dict:
                        # 清洗后重名会覆盖定义，按失败关闭处理。
                        self._mark_startup_failure(attempt)
                        return
                    tools_dict[sanitized] = tool
                    name_mapping[sanitized] = tool.name

                if attempt.closing:
                    return

                attempt.tools = tools
                attempt.tools_dict = tools_dict
                attempt.name_mapping = name_mapping
                attempt.ready_ok = True
                attempt.ready_evt.set()

                # 挂起等待关闭
                await attempt.shutdown_evt.wait()

        except asyncio.CancelledError:
            # 取消必须向上传播；finally 负责解除初始化等待。
            raise
        except Exception:
            if attempt.ready_ok:
                self.logger.bind(tag=TAG).error("服务端MCP客户端关闭失败")
            else:
                self._mark_startup_failure(attempt)
        finally:
            if not attempt.ready_ok:
                self._mark_startup_failure(attempt, log=not attempt.closing)
            attempt.session = None
            attempt.ready_evt.set()
            if self._attempt is attempt:
                self.session = None
