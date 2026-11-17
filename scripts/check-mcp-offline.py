#!/usr/bin/env python3
"""Issue #12 diagnostics: production MCP code, synthetic IO, no credentials.

Run explicitly (not pytest):
    python -I -B scripts/check-mcp-offline.py --sdk stub --group all --output DIR

``stub`` does not verify SDK compatibility. ``installed`` imports the installed
SDK's real models but replaces every transport/session with an in-memory fake.
Requirement tests deliberately fail on the unmodified implementation; no xfail
or expectedFailure hides them. Synthetic field names are NOT a service schema.
"""
from __future__ import annotations

import argparse
import asyncio
from contextlib import asynccontextmanager
from datetime import timedelta
import hashlib
import importlib
import importlib.util
import io
import json
from pathlib import Path
import re
import socket
import sys
import tempfile
import types
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
VOICE = ROOT / "main" / "xiaozhi-server"
MCP_PATH = VOICE / "core" / "providers" / "tools" / "server_mcp"
SYNTHETIC_SECRET = "synthetic-private-marker-do-not-publish"
TOOL = "synthetic_knowledge"
SCHEMA = {"type": "object", "properties": {"question": {"type": "string"}},
          "required": ["question"], "additionalProperties": False}


class OfflineAccessBlocked(BaseException):
    """Not caught by production except Exception/retry handlers."""


ACCESS_ATTEMPTS = {}


def deny_access(kind):
    ACCESS_ATTEMPTS[kind] = ACCESS_ATTEMPTS.get(kind, 0) + 1
    raise OfflineAccessBlocked("OFFLINE_ACCESS_BLOCKED")


def blocked(*args, **kwargs):
    return deny_access("unexpected_transport")


class Logger:
    def __init__(self):
        self.messages = []

    def bind(self, **kwargs):
        return self

    def __getattr__(self, name):
        if name not in ("debug", "info", "warning", "error", "exception"):
            raise AttributeError(name)
        return lambda text, *args, **kwargs: self.messages.append(str(text))


class SyntheticClient(types.SimpleNamespace):
    __hash__ = object.__hash__


class ToolModel:
    def __init__(self, name=TOOL, description="Synthetic tool only", input_schema=None):
        self.name = name
        self.description = description
        self.input_schema = SCHEMA if input_schema is None else input_schema


class SyntheticResult:
    def __init__(self, payload=None, is_error=False):
        self.isError = is_error
        payload = {"status": "ok", "generated": True} if payload is None else payload
        self.content = [{"type": "text", "text": json.dumps(payload)}]

    def __str__(self):
        return json.dumps({"isError": self.isError, "content": self.content})


class Harness:
    def __init__(self, sdk, directory):
        self.sdk = sdk
        self.directory = Path(directory)
        self.logger = Logger()
        self.sessions = []
        self.events = []
        self.tools = [ToolModel()]
        self.originals = {}
        self.installed_metadata = {}
        self.sdk_session_class = None
        self._prepare_imports()

    def module(self, name, **attrs):
        value = types.ModuleType(name)
        value.__dict__.update(attrs)
        return value

    def install(self, name, value):
        if name not in self.originals:
            self.originals[name] = sys.modules.get(name)
        sys.modules[name] = value

    def package(self, name, path=None):
        self.install(name, self.module(name, __path__=[] if path is None else [str(path)]))

    def load(self, name, path, package=False):
        spec = importlib.util.spec_from_file_location(
            name, path, submodule_search_locations=[str(path.parent)] if package else None)
        value = importlib.util.module_from_spec(spec)
        self.install(name, value)
        spec.loader.exec_module(value)
        return value

    def _prepare_imports(self):
        self.package("config")
        self.install("config.logger", self.module("config.logger", setup_logging=lambda: self.logger))
        self.install("config.config_loader", self.module(
            "config.config_loader", get_project_dir=lambda: str(self.directory) + "/"))
        self.package("core")
        self.package("core.utils")
        # Load the production sanitizer function only, avoiding unrelated import
        # side effects (audio/device/config). The exact AST body is preserved.
        import ast
        util = VOICE / "core" / "utils" / "util.py"
        parsed = ast.parse(util.read_text(encoding="utf-8"))
        fn = next(n for n in parsed.body if isinstance(n, ast.FunctionDef)
                  and n.name == "sanitize_tool_name")
        namespace = {"re": re}
        exec(compile(ast.Module(body=[fn], type_ignores=[]), str(util), "exec"), namespace)
        self.install("core.utils.util", self.module(
            "core.utils.util", sanitize_tool_name=namespace["sanitize_tool_name"]))

        if self.sdk == "stub":
            for name in ("mcp", "mcp.client", "mcp.shared"):
                self.package(name)
            for name in ("mcp.client.session", "mcp.client.stdio", "mcp.client.sse",
                         "mcp.client.streamable_http", "mcp.shared.dispatcher", "mcp.types"):
                self.install(name, self.module(name))
            for name in ("SamplingFnT", "ElicitationFnT", "ListRootsFnT", "LoggingFnT", "MessageHandlerFnT"):
                setattr(sys.modules["mcp.client.session"], name, object)
            sys.modules["mcp.shared.dispatcher"].ProgressFnT = object
            sys.modules["mcp.types"].LoggingMessageNotificationParams = object
            sys.modules["mcp"].Implementation = lambda **kwargs: types.SimpleNamespace(**kwargs)
            sys.modules["mcp"].StdioServerParameters = lambda **kwargs: types.SimpleNamespace(**kwargs)
            self.install("httpx2", self.module("httpx2", AsyncClient=object,
                                                Timeout=lambda *a, **kw: (a, kw)))
        else:
            from importlib import metadata
            for name in ("mcp", "mcp-types", "httpx2", "httpx", "openai", "anyio"):
                try:
                    self.installed_metadata[name] = metadata.version(name)
                except metadata.PackageNotFoundError:
                    self.installed_metadata[name] = "NOT_INSTALLED"
            for name in ("mcp", "mcp.client.session", "mcp.client.stdio", "mcp.client.sse",
                         "mcp.client.streamable_http", "mcp.shared.dispatcher", "mcp.types", "httpx2"):
                importlib.import_module(name)
            self.sdk_session_class = sys.modules["mcp"].ClientSession

        # Guard imported transports too: code under test must use injected IO.
        for mod, name in (("mcp", "ClientSession"), ("mcp.client.stdio", "stdio_client"),
                          ("mcp.client.sse", "sse_client"),
                          ("mcp.client.streamable_http", "streamable_http_client")):
            setattr(sys.modules[mod], name, blocked)
        self.package("plugins_func")
        self.register = self.load("plugins_func.register", VOICE / "plugins_func" / "register.py")
        self.package("issue12_tools")
        self.base = self.load("issue12_tools.base", VOICE / "core/providers/tools/base/__init__.py", True)
        self.package("issue12_tools.server_mcp", MCP_PATH)
        self.client = self.load("issue12_tools.server_mcp.mcp_client", MCP_PATH / "mcp_client.py")
        self.manager = self.load("issue12_tools.server_mcp.mcp_manager", MCP_PATH / "mcp_manager.py")
        self.executor = self.load("issue12_tools.server_mcp.mcp_executor", MCP_PATH / "mcp_executor.py")
        self.load("core.product_rejections", VOICE / "core/product_rejections.py")
        self.voice = self.load("issue12_tools.product_voice", VOICE / "core/product_voice.py")
        self.timeout_seam = self._timeout_seam()

    def _timeout_seam(self):
        # Execute the two exact production submission/wait loops, not a rewrite
        # of the whole chat/audio stack. The caller supplies a small timeout;
        # configuration parsing and later TTS handling are outside this seam.
        import ast
        source = (VOICE / "core/connection.py").read_text(encoding="utf-8")
        tree = ast.parse(source)
        cls = next(n for n in tree.body if isinstance(n, ast.ClassDef)
                   and n.name == "ConnectionHandler")
        chat = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == "chat")
        loops = []
        for name in ("tool_calls_list", "futures_with_data"):
            found = [n for n in ast.walk(chat) if isinstance(n, ast.For)
                     and isinstance(n.iter, ast.Name) and n.iter.id == name]
            if name == "tool_calls_list":
                found = [n for n in found if any(
                    isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute)
                    and call.func.attr == "run_coroutine_threadsafe" for call in ast.walk(n))]
            if len(found) != 1:
                raise RuntimeError("PRODUCTION_TIMEOUT_SEAM_CHANGED")
            loops.append(found[0])
        wrapper = ast.parse(
            "def timeout_seam(self, tool_calls_list, tool_call_timeout):\n"
            "    futures_with_data = []\n"
            "    tool_results = []\n").body[0]
        wrapper.body.extend(loops)
        wrapper.body.append(ast.Return(value=ast.Name(id="tool_results", ctx=ast.Load())))
        namespace = {"asyncio": asyncio, "json": json, "TAG": "synthetic-timeout-seam",
                     "ActionResponse": self.register.ActionResponse, "Action": self.register.Action,
                     "enqueue_tool_report": lambda *a, **kw: None}
        compiled = ast.fix_missing_locations(ast.Module(body=[wrapper], type_ignores=[]))
        exec(compile(compiled, str(VOICE / "core/connection.py"), "exec"), namespace)
        return namespace["timeout_seam"]

    def result(self, payload=None, is_error=False):
        if self.sdk == "stub":
            return SyntheticResult(payload, is_error)
        from mcp.types import CallToolResult, TextContent
        payload = {"status": "ok", "generated": True} if payload is None else payload
        # Installed 2.3.0 models use snake_case; older models used the wire
        # alias. Select only a field actually present in that model.
        fields = CallToolResult.model_fields
        error_field = "is_error" if "is_error" in fields else "isError"
        return CallToolResult(content=[TextContent(type="text", text=json.dumps(payload))],
                              **{error_field: is_error})

    @asynccontextmanager
    async def transport(self, kind, **kwargs):
        self.events.append((kind, kwargs))
        try:
            yield object(), object()
        finally:
            self.events.append((kind + "_closed", {}))

    def session_factory(self, **kwargs):
        harness = self

        class Session:
            def __init__(self):
                self.options = kwargs
                self.calls = []
                self.closed = False

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                self.closed = True

            async def initialize(self):
                harness.events.append(("initialize", {}))

            async def list_tools(self):
                harness.events.append(("list_tools", {}))
                return types.SimpleNamespace(tools=harness.tools)

            async def call_tool(self, name, **kwargs):
                self.calls.append((name, kwargs))
                return harness.result()

        session = Session()
        self.sessions.append(session)
        return session

    def close(self):
        for name in list(sys.modules):
            if name.startswith("issue12_tools.") and name not in self.originals:
                del sys.modules[name]
        for name, value in reversed(list(self.originals.items())):
            if value is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = value


H = None


class AsyncBase(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        H.logger.messages.clear()
        H.events.clear()
        H.sessions.clear()
        H.tools = [ToolModel()]
        self.patches = mock.patch.multiple(
            H.client, ClientSession=H.session_factory,
            sse_client=lambda **kw: H.transport("sse", **kw),
            streamable_http_client=lambda **kw: H.transport("http", **kw),
            stdio_client=lambda params: H.transport("stdio", params=params))
        self.patches.start()
        self.addCleanup(self.patches.stop)

    async def initialized(self, config=None):
        client = H.client.ServerMCPClient(config or {"url": "https://synthetic.invalid/sse"})
        await client.initialize()
        self.addAsyncCleanup(client.cleanup)
        return client

    def manager_with(self, outcome):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        client = types.SimpleNamespace(has_tool=lambda name: name == TOOL,
                                       call_tool=mock.AsyncMock(side_effect=outcome),
                                       cleanup=mock.AsyncMock())
        manager.clients["synthetic-server"] = client
        return manager, client

    def executor_with(self, result=None, error=None):
        executor = H.executor.ServerMCPExecutor(types.SimpleNamespace())
        executor._initialized = True
        executor.mcp_manager = types.SimpleNamespace(execute_tool=mock.AsyncMock(
            return_value=result, side_effect=error))
        return executor


class ExistingBehavior(AsyncBase):
    """Characterization only: passing is not Issue #12 final acceptance."""

    async def test_default_transport_is_sse_not_streamable_http(self):
        client = await self.initialized()
        self.assertTrue(client.is_connected())
        self.assertEqual(H.events[0][0], "sse")
        self.assertEqual([e[0] for e in H.events[:3]], ["sse", "initialize", "list_tools"])

    async def test_sse_timeout_defaults_are_forwarded(self):
        await self.initialized()
        options = H.events[0][1]
        self.assertEqual((options["timeout"], options["sse_read_timeout"]), (5, 300))

    async def test_sse_headers_and_custom_timeouts_are_forwarded(self):
        await self.initialized({"url": "https://synthetic.invalid/sse", "timeout": 2,
                                "sse_read_timeout": 7,
                                "headers": {"Authorization": "Bearer " + SYNTHETIC_SECRET}})
        options = H.events[0][1]
        self.assertEqual((options["timeout"], options["sse_read_timeout"]), (2, 7))
        self.assertTrue(options["headers"]["Authorization"].endswith(SYNTHETIC_SECRET))
        self.assertFalse(any(SYNTHETIC_SECRET in v for v in H.logger.messages))

    async def test_streamable_http_requires_explicit_choice(self):
        class FakeHTTP:
            def __init__(self, **kwargs):
                self.options = kwargs
                self.closed = False

            async def aclose(self):
                self.closed = True
        with mock.patch.object(H.client.httpx2, "AsyncClient", FakeHTTP):
            client = await self.initialized({"url": "https://synthetic.invalid/mcp", "transport": "http"})
            self.assertEqual(H.events[0][0], "http")
            http = H.events[0][1]["http_client"]
            await client.cleanup()
            self.assertTrue(http.closed)

    async def test_stdio_is_replaced_without_spawning_a_process(self):
        await self.initialized({"command": "synthetic-never-execute", "args": ["fake"]})
        self.assertEqual(H.events[0][0], "stdio")
        self.assertEqual(H.events[0][1]["params"].command, "synthetic-never-execute")

    async def test_discovered_synthetic_schema_is_preserved(self):
        client = await self.initialized()
        definition = client.get_available_tools()[0]["function"]
        self.assertEqual(definition["name"], TOOL)
        self.assertEqual(definition["parameters"], SCHEMA)
        self.assertTrue(client.has_tool(TOOL))

    async def test_sanitized_name_maps_back_to_original(self):
        H.tools = [ToolModel(name="synthetic.name")]
        client = await self.initialized()
        await client.call_tool("synthetic_name", {"question": "synthetic"})
        self.assertEqual(H.sessions[0].calls[0][0], "synthetic.name")

    async def test_call_options_are_passed_to_the_session(self):
        client = await self.initialized()
        callback = mock.AsyncMock()
        await client.call_tool(TOOL, {"question": "synthetic"}, timedelta(seconds=2),
                               callback, meta={"synthetic": True})
        options = H.sessions[0].calls[0][1]
        self.assertEqual(options["read_timeout_seconds"], 2.0)
        self.assertIsInstance(options["read_timeout_seconds"], float)
        self.assertIs(options["progress_callback"], callback)
        self.assertEqual(options["meta"], {"synthetic": True})

    async def test_call_before_initialize_raises(self):
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        with self.assertRaises(RuntimeError):
            await client.call_tool(TOOL, {})

    async def test_cleanup_closes_worker_and_transport(self):
        client = await self.initialized()
        await client.cleanup()
        self.assertFalse(client.is_connected())
        self.assertTrue(H.sessions[0].closed)
        self.assertTrue(any(e[0] == "sse_closed" for e in H.events))

    async def test_separate_clients_do_not_share_session_objects(self):
        first = await self.initialized()
        second = await self.initialized()
        self.assertIsNot(first.session, second.session)
        self.assertIsNot(first._worker_task, second._worker_task)
        # This says nothing about production credential selection per account.

    async def test_manager_first_initialize_has_ten_second_deadline(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        seen = []
        initialize = mock.AsyncMock()
        fake = SyntheticClient(initialize=initialize, cleanup=mock.AsyncMock(),
                                     get_available_tools=lambda: [])
        async def wait(awaitable, timeout):
            seen.append(timeout)
            return await awaitable
        with mock.patch.object(H.manager, "ServerMCPClient", return_value=fake), \
                mock.patch.object(H.manager.asyncio, "wait_for", side_effect=wait):
            await manager._init_server("synthetic-server", {})
        self.assertEqual(seen, [10])
        self.assertIs(manager.clients["synthetic-server"], fake)

    async def test_unknown_manager_tool_is_rejected(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        with self.assertRaises(ValueError):
            await manager.execute_tool("synthetic_missing", {})

    async def test_manager_success_submits_once(self):
        manager, client = self.manager_with([H.result()])
        result = await manager.execute_tool(TOOL, {"question": "synthetic"})
        self.assertIsNotNone(result)
        self.assertEqual(client.call_tool.await_count, 1)

    async def test_manager_failure_submits_once_without_retry_wait(self):
        manager, client = self.manager_with(RuntimeError("SYNTHETIC_OUTCOME_UNKNOWN"))
        with mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock) as sleep:
            with self.assertRaises(RuntimeError):
                await manager.execute_tool(TOOL, {"question": "synthetic"})
        self.assertEqual(client.call_tool.await_count, 1)
        self.assertEqual(client.cleanup.await_count, 0)
        sleep.assert_not_awaited()

    async def test_local_cancellation_propagates_without_retry(self):
        started = asyncio.Event()
        cancelled = asyncio.Event()
        async def call(*args, **kwargs):
            started.set()
            try:
                await asyncio.Event().wait()
            finally:
                cancelled.set()
        manager, client = self.manager_with(call)
        task = asyncio.create_task(manager.execute_tool(TOOL, {}))
        await started.wait()
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(cancelled.is_set())
        self.assertEqual(client.call_tool.await_count, 1)
        # An in-memory coroutine cancellation does not prove upstream stopped.

    async def test_executor_not_initialized_returns_error(self):
        executor = H.executor.ServerMCPExecutor(types.SimpleNamespace())
        response = await executor.execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR)

    async def test_executor_strips_mcp_prefix(self):
        executor = self.executor_with(H.result())
        response = await executor.execute(None, "mcp_" + TOOL, {"question": "synthetic"})
        self.assertIs(response.action, H.register.Action.REQLLM)
        self.assertEqual(executor.mcp_manager.execute_tool.await_args.args[0], TOOL)

    async def test_executor_forwards_incomplete_markers_without_classification(self):
        executor = self.executor_with(H.result({"status": "incomplete", "generated": False}))
        response = await executor.execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.REQLLM)
        self.assertTrue("incomplete" in response.result)
        # REQLLM means ask the model again, not a business success verdict.
        # Final-answer classification is therefore still unverified here.

    async def test_executor_rejects_explicit_mcp_error_before_model_continuation(self):
        executor = self.executor_with(H.result({"status": "synthetic_error"}, True))
        response = await executor.execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR)
        self.assertIsNone(response.result)

    async def test_executor_only_maps_registered_notfound_error_to_notfound(self):
        error = H.manager.ToolNotFoundError("SYNTHETIC_NOT_FOUND")
        response = await self.executor_with(error=error).execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.NOTFOUND)
        self.assertNotIn("SYNTHETIC_NOT_FOUND", response.response)

    async def test_executor_maps_other_exception_to_error(self):
        response = await self.executor_with(error=RuntimeError("SYNTHETIC_FAILURE")).execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR)


class RequiredBehavior(AsyncBase):
    """Issue-aligned safeguards. Failures are evidence, not hidden xfails."""

    async def test_initialization_failure_is_not_reported_as_connected(self):
        client = H.client.ServerMCPClient({})
        rejected = False
        try:
            try:
                await client.initialize()
            except RuntimeError:
                rejected = True
            self.assertFalse(client.is_connected())
        finally:
            await client.cleanup()
        self.assertTrue(rejected, "FAILED_INITIALIZATION_RETURNED_SUCCESS")

    async def test_uncertain_failure_is_not_automatically_replayed(self):
        manager, client = self.manager_with(RuntimeError("SYNTHETIC_OUTCOME_UNKNOWN"))
        with mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock):
            with self.assertRaises(RuntimeError):
                await manager.execute_tool(TOOL, {})
        self.assertTrue(client.call_tool.await_count == 1, "UNKNOWN_OUTCOME_WAS_REPLAYED")

    async def test_tool_timeout_is_not_automatically_replayed(self):
        manager, client = self.manager_with(asyncio.TimeoutError())
        with mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock):
            with self.assertRaises(RuntimeError):
                await manager.execute_tool(TOOL, {})
        self.assertTrue(client.call_tool.await_count == 1, "TIMED_OUT_REQUEST_WAS_REPLAYED")

    async def test_tool_arguments_do_not_leak_into_logs(self):
        manager, _ = self.manager_with([H.result()])
        await manager.execute_tool(TOOL, {"question": SYNTHETIC_SECRET})
        self.assertFalse(any(SYNTHETIC_SECRET in s for s in H.logger.messages), "ARGUMENT_LOG_LEAK")

    async def test_upstream_exception_does_not_leak_into_logs(self):
        manager, _ = self.manager_with(RuntimeError(SYNTHETIC_SECRET))
        with mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock):
            with self.assertRaises(RuntimeError):
                await manager.execute_tool(TOOL, {})
        self.assertFalse(any(SYNTHETIC_SECRET in s for s in H.logger.messages), "EXCEPTION_LOG_LEAK")

    async def test_progress_text_does_not_leak_into_logs(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        await manager.progress_callback(1, 2, SYNTHETIC_SECRET)
        self.assertFalse(any(SYNTHETIC_SECRET in s for s in H.logger.messages), "PROGRESS_LOG_LEAK")

    async def test_server_notification_does_not_leak_into_logs(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        await manager.logging_callback(types.SimpleNamespace(level="info", data=SYNTHETIC_SECRET))
        self.assertFalse(any(SYNTHETIC_SECRET in s for s in H.logger.messages), "SERVER_LOG_LEAK")

    async def test_executor_exception_is_not_echoed_in_public_response(self):
        response = await self.executor_with(error=RuntimeError(SYNTHETIC_SECRET)).execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR)
        self.assertFalse(SYNTHETIC_SECRET in (response.response or ""), "PUBLIC_EXCEPTION_LEAK")

    async def test_executor_value_error_is_not_echoed_in_public_response(self):
        response = await self.executor_with(error=ValueError(SYNTHETIC_SECRET)).execute(None, TOOL, {})
        self.assertFalse(SYNTHETIC_SECRET in (response.response or ""), "PUBLIC_VALUE_ERROR_LEAK")


class ExtendedBehavior(AsyncBase):
    """Additional observations, explicitly not a real service contract."""

    async def test_empty_tool_list_preserves_empty_availability(self):
        H.tools = []
        client = await self.initialized()
        self.assertEqual(client.get_available_tools(), [])
        self.assertTrue(client.is_connected())

    async def test_initialization_is_idempotent_while_running(self):
        client = await self.initialized()
        await client.initialize()
        self.assertEqual(len(H.sessions), 1)
        self.assertEqual([e[0] for e in H.events].count("initialize"), 1)

    async def test_shutdown_clears_worker_connection_state(self):
        client = await self.initialized()
        await client.cleanup()
        self.assertIsNone(client._worker_task)
        self.assertFalse(client.is_connected())

    async def test_client_call_cancel_propagates_to_synthetic_session(self):
        client = await self.initialized()
        started, cancelled = asyncio.Event(), asyncio.Event()
        async def slow(*args, **kwargs):
            started.set()
            try:
                await asyncio.Event().wait()
            finally:
                cancelled.set()
        client.session.call_tool = mock.AsyncMock(side_effect=slow)
        task = asyncio.create_task(client.call_tool(TOOL, {}))
        await asyncio.wait_for(started.wait(), 2)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(cancelled.is_set())
        self.assertEqual(client.session.call_tool.await_count, 1)

    async def test_executor_cancellation_is_not_rewritten_as_error(self):
        executor = self.executor_with(error=asyncio.CancelledError())
        with self.assertRaises(asyncio.CancelledError):
            await executor.execute(None, TOOL, {})

    async def test_failure_does_not_enter_a_retry_sleep_or_cleanup_path(self):
        manager, client = self.manager_with(RuntimeError("SYNTHETIC_OUTCOME_UNKNOWN"))
        with mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock,
                               side_effect=AssertionError("UNEXPECTED_RETRY_SLEEP")) as sleep:
            with self.assertRaises(RuntimeError):
                await manager.execute_tool(TOOL, {})
        self.assertEqual(client.call_tool.await_count, 1)
        self.assertEqual(client.cleanup.await_count, 0)
        sleep.assert_not_awaited()

    async def test_manager_init_timeout_does_not_register_client(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        fake = SyntheticClient(initialize=mock.AsyncMock(side_effect=asyncio.TimeoutError()),
                                     cleanup=mock.AsyncMock(), get_available_tools=lambda: [])
        with mock.patch.object(H.manager, "ServerMCPClient", return_value=fake):
            await manager._init_server("synthetic-server", {})
        self.assertEqual(manager.clients, {})
        self.assertEqual(manager.tools, [])
        fake.cleanup.assert_awaited_once()

    async def test_result_payloads_are_forwarded_without_a_completion_verdict(self):
        # Synthetic variants do not define the real ask_knowledge response.
        payloads = [
            {"status": "ok", "generated": True, "answer": "synthetic answer", "sources": []},
            {"status": "no_match", "generated": False, "answer": "", "sources": []},
            {"status": "incomplete", "generated": False, "answer": "synthetic excerpt"},
            {"status": "unknown", "generated": True},
            {"generated": True}, {}, [], False, "synthetic unstructured text",
        ]
        for index, payload in enumerate(payloads):
            with self.subTest(variant=index):
                result = H.result(payload)
                response = await self.executor_with(result).execute(None, TOOL, {})
                self.assertIs(response.action, H.register.Action.REQLLM)
                self.assertEqual(response.result, str(result))
                self.assertIsNone(response.response)

    async def test_http_status_labels_are_generic_errors_not_credit_verdicts(self):
        for code in (401, 403, 429, 500, 503):
            with self.subTest(status=code):
                error = RuntimeError("synthetic HTTP %d" % code)
                response = await self.executor_with(error=error).execute(None, TOOL, {})
                self.assertIs(response.action, H.register.Action.ERROR)
                self.assertNotIn(str(code), response.response)
                self.assertNotIn(str(error), response.response)
        # This is local safe mapping, not a real upstream/credit verdict.

    async def test_manager_passes_client_read_timeout_default_as_unspecified(self):
        manager, client = self.manager_with([H.result()])
        await manager.execute_tool(TOOL, {})
        self.assertNotIn("read_timeout_seconds", client.call_tool.await_args.kwargs)

    async def test_available_reconnect_config_does_not_trigger_replay(self):
        manager, old = self.manager_with(RuntimeError("SYNTHETIC_OUTCOME_UNKNOWN"))
        new = types.SimpleNamespace(
            initialize=mock.AsyncMock(), cleanup=mock.AsyncMock(),
            call_tool=mock.AsyncMock(return_value=H.result()), has_tool=lambda name: True)
        with mock.patch.object(manager, "load_config", return_value={"synthetic-server": {}}), \
                mock.patch.object(H.manager, "ServerMCPClient", return_value=new), \
                mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock):
            with self.assertRaises(RuntimeError):
                await manager.execute_tool(TOOL, {"question": "synthetic repeat"})
        old.cleanup.assert_not_awaited()
        self.assertEqual(old.call_tool.await_count, 1)
        new.call_tool.assert_not_awaited()
        new.initialize.assert_not_awaited()
        self.assertIs(manager.clients["synthetic-server"], old)


class ExtendedRequirements(AsyncBase):
    """Regression candidates. Failures do not authorize a product rewrite."""

    async def test_discovery_failure_propagates_and_is_not_connected(self):
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        original = H.session_factory
        discovery = mock.AsyncMock(side_effect=RuntimeError("SYNTHETIC_DISCOVERY_ERROR"))
        def broken_session(**kwargs):
            session = original(**kwargs)
            session.list_tools = discovery
            return session
        rejected = False
        try:
            with mock.patch.object(H.client, "ClientSession", side_effect=broken_session):
                try:
                    await client.initialize()
                except RuntimeError as exc:
                    rejected = isinstance(exc, H.client.MCPInitializationError)
                    self.assertNotIn("SYNTHETIC_DISCOVERY_ERROR", str(exc))
            connected = client.is_connected()
        finally:
            await client.cleanup()
        self.assertEqual(discovery.await_count, 1, "DISCOVERY_STIMULUS_NOT_REACHED")
        self.assertTrue(rejected and not connected, "DISCOVERY_FAILURE_NOT_PROPAGATED")

    async def test_sanitized_tool_name_collision_is_rejected(self):
        H.tools = [ToolModel(name="synthetic.name"), ToolModel(name="synthetic/name")]
        sanitize = H.client.sanitize_tool_name
        self.assertEqual(sanitize(H.tools[0].name), sanitize(H.tools[1].name),
                         "COLLISION_PRECONDITION_NOT_MET")
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        rejected = False
        try:
            try:
                await client.initialize()
            except H.client.MCPInitializationError:
                rejected = True
        finally:
            await client.cleanup()
        self.assertTrue(rejected, "COLLIDING_TOOL_DEFINITIONS_SILENTLY_OVERWRITTEN")

    async def test_call_after_cleanup_has_controlled_error(self):
        client = await self.initialized()
        await client.cleanup()
        controlled = False
        try:
            await client.call_tool(TOOL, {})
        except RuntimeError:
            controlled = True
        except Exception:
            pass
        self.assertTrue(controlled, "CLOSED_CLIENT_FAILURE_NOT_CONTROLLED")

    async def test_executor_does_not_forward_none_as_a_tool_result(self):
        response = await self.executor_with(result=None).execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR, "NONE_RESULT_FORWARDED_TO_MODEL")

    async def test_client_worker_error_is_not_logged_verbatim(self):
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        @asynccontextmanager
        async def broken_transport(**kwargs):
            raise RuntimeError(SYNTHETIC_SECRET)
            yield  # make the synthetic transport an async context manager
        try:
            with mock.patch.object(H.client, "sse_client", broken_transport):
                try:
                    await client.initialize()
                except Exception:
                    pass
        finally:
            await client.cleanup()
        self.assertFalse(any(SYNTHETIC_SECRET in v for v in H.logger.messages),
                         "WORKER_LOG_LEAK")

    async def test_manager_init_error_is_not_logged_verbatim(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        fake = SyntheticClient(initialize=mock.AsyncMock(side_effect=RuntimeError(SYNTHETIC_SECRET)),
                                     cleanup=mock.AsyncMock(), get_available_tools=lambda: [])
        with mock.patch.object(H.manager, "ServerMCPClient", return_value=fake):
            await manager._init_server("synthetic-server", {})
        self.assertFalse(any(SYNTHETIC_SECRET in v for v in H.logger.messages),
                         "MANAGER_INIT_LOG_LEAK")

    async def test_no_reconnect_initialization_or_exception_logging_after_failed_call(self):
        manager, old = self.manager_with(RuntimeError("SYNTHETIC_OUTCOME_UNKNOWN"))
        new = types.SimpleNamespace(initialize=mock.AsyncMock(side_effect=RuntimeError(SYNTHETIC_SECRET)))
        with mock.patch.object(manager, "load_config", return_value={"synthetic-server": {}}), \
                mock.patch.object(H.manager, "ServerMCPClient", return_value=new), \
                mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock):
            with self.assertRaises(RuntimeError):
                await manager.execute_tool(TOOL, {})
        new.initialize.assert_not_awaited()
        self.assertEqual(old.call_tool.await_count, 1)
        self.assertFalse(any(SYNTHETIC_SECRET in v for v in H.logger.messages),
                         "RECONNECT_LOG_LEAK")

    async def test_successful_reconnect_does_not_authorize_unknown_replay(self):
        manager, old = self.manager_with(RuntimeError("SYNTHETIC_OUTCOME_UNKNOWN"))
        new = types.SimpleNamespace(initialize=mock.AsyncMock(), call_tool=mock.AsyncMock(return_value=H.result()))
        with mock.patch.object(manager, "load_config", return_value={"synthetic-server": {}}), \
                mock.patch.object(H.manager, "ServerMCPClient", return_value=new), \
                mock.patch.object(H.manager.asyncio, "sleep", new_callable=mock.AsyncMock):
            try:
                await manager.execute_tool(TOOL, {})
            except RuntimeError:
                pass
        self.assertTrue(old.call_tool.await_count == 1 and new.call_tool.await_count == 0,
                        "RECONNECT_REPLAYED_UNCERTAIN_REQUEST")


class LocalVoiceGuard(AsyncBase):
    """Real local permission guards with synthetic authority responses only."""

    def grant(self, subject="a", version=7):
        return {"allowed": True, "permission_version": version,
                "authorization_scope": "S" * 43, "authorization_subject": subject * 64,
                "permission_authorization_order": 4}

    def authority(self):
        authority = object.__new__(H.voice.VoiceAuthority)
        authority.rejections = types.SimpleNamespace(blocked=mock.Mock(return_value=False),
                                                     reject=mock.Mock())
        return authority

    async def test_valid_scope_passes_local_format_and_quarantine_guard(self):
        authority = self.authority()
        authority.check_scope(self.grant())
        authority.rejections.blocked.assert_called_once_with("S" * 43, "a" * 64, 4)

    async def test_invalid_scope_subject_and_order_fail_closed(self):
        invalid = [("authorization_scope", "S" * 42), ("authorization_scope", "S" * 44),
                   ("authorization_scope", "!" * 43), ("authorization_subject", "A" * 64),
                   ("authorization_subject", "a" * 63), ("permission_authorization_order", True),
                   ("permission_authorization_order", -1), ("permission_authorization_order", 1.5)]
        for field, value in invalid:
            with self.subTest(field=field, kind=type(value).__name__):
                grant = self.grant()
                grant[field] = value
                with self.assertRaises(H.voice.VoiceDenied):
                    self.authority().check_scope(grant)

    async def test_rejected_generation_fails_closed(self):
        authority = self.authority()
        authority.rejections.blocked.return_value = True
        with self.assertRaises(H.voice.VoiceDenied):
            authority.check_scope(self.grant())

    async def test_unavailable_rejection_store_fails_closed(self):
        authority = self.authority()
        authority.rejections.blocked.side_effect = H.voice.RejectionStoreUnavailable()
        with self.assertRaises(H.voice.VoiceDenied):
            authority.check_scope(self.grant())

    async def test_permission_version_change_ends_gate(self):
        authority = self.authority()
        authority.request = mock.Mock(return_value=self.grant(version=8))
        gate = H.voice.VoiceGate(authority, "T" * 43, 7, self.grant())
        with self.assertRaises(H.voice.VoiceDenied):
            gate.check()
        self.assertTrue(gate.ended.is_set())

    async def test_ended_gate_makes_no_new_authority_request(self):
        authority = self.authority()
        authority.request = mock.Mock()
        gate = H.voice.VoiceGate(authority, "T" * 43, 7, self.grant())
        gate.ended.set()
        with self.assertRaises(H.voice.VoiceDenied):
            gate.check()
        authority.request.assert_not_called()

    async def test_model_access_uses_each_gates_own_session(self):
        authority = self.authority()
        def request(action, body):
            subject = "a" if body["session"] == "A" * 43 else "b"
            grant = self.grant(subject)
            if action == "model-access":
                grant.update(model="qwen3.8-flash", api_key="synthetic-" + subject,
                             rejection_receipt="synthetic-receipt", authorization_order=4)
            return grant
        authority.request = mock.Mock(side_effect=request)
        a = H.voice.VoiceGate(authority, "A" * 43, 7, self.grant("a"))
        b = H.voice.VoiceGate(authority, "B" * 43, 7, self.grant("b"))
        self.assertEqual(a.model_access()["api_key"], "synthetic-a")
        self.assertEqual(b.model_access()["api_key"], "synthetic-b")
        a.ended.set()
        self.assertEqual(b.model_access()["api_key"], "synthetic-b")
        # The server-side account mapping here is a fake, NOT proven policy.

    async def test_wrong_model_or_missing_key_receipt_ends_gate(self):
        cases = [{"model": "synthetic-wrong-model"}, {"api_key": ""}, {"rejection_receipt": ""}]
        for patch in cases:
            with self.subTest(field=next(iter(patch))):
                authority = self.authority()
                result = dict(self.grant(), model="qwen3.8-flash", api_key="synthetic-key",
                              rejection_receipt="synthetic-receipt", authorization_order=4)
                result.update(patch)
                authority.request = mock.Mock(side_effect=[self.grant(), result])
                gate = H.voice.VoiceGate(authority, "T" * 43, 7, self.grant())
                with self.assertRaises(H.voice.VoiceDenied):
                    gate.model_access()
                self.assertTrue(gate.ended.is_set())

    async def test_guarded_socket_checks_every_send_and_rejects_after_exit(self):
        class Gate:
            def __init__(self):
                import threading
                self.ended = threading.Event()
                self.check_async = mock.AsyncMock()
        gate = Gate()
        socket_fake = types.SimpleNamespace(send=mock.AsyncMock())
        guarded = H.voice.GuardedSocket(socket_fake, gate)
        await guarded.send("synthetic-first")
        await guarded.send(b"synthetic-audio")
        gate.ended.set()
        with self.assertRaises(H.voice.VoiceDenied):
            await guarded.send("synthetic-old-result")
        self.assertEqual(gate.check_async.await_count, 3)
        self.assertEqual(socket_fake.send.await_count, 2)


class ToolTimeoutObservation(AsyncBase):
    async def test_timed_out_tool_keeps_running_but_late_result_is_not_appended(self):
        loop = asyncio.get_running_loop()
        started, release, finished = asyncio.Event(), asyncio.Event(), asyncio.Event()
        observed_futures, reports = [], []
        async def call(*args):
            started.set()
            try:
                await release.wait()
                return H.register.ActionResponse(H.register.Action.RESPONSE, result="synthetic-late")
            finally:
                finished.set()
        conn = types.SimpleNamespace(loop=loop, logger=H.logger,
                                     func_handler=types.SimpleNamespace(handle_llm_function_call=call))
        original_submit = asyncio.run_coroutine_threadsafe
        def submit(coro, target):
            # This wrapper runs on the waiting thread. Synchronize with the
            # actual start event before production begins its short wait.
            future = original_submit(coro, target)
            observed_futures.append(future)
            marker = original_submit(started.wait(), target)
            try:
                marker.result(timeout=2)
            except BaseException:
                marker.cancel()
                raise
            return future
        namespace = H.timeout_seam.__globals__
        with mock.patch.object(asyncio, "run_coroutine_threadsafe", side_effect=submit), \
                mock.patch.dict(namespace, {"enqueue_tool_report": lambda *a, **kw: reports.append((a, kw))}):
            try:
                results = await asyncio.wait_for(asyncio.to_thread(
                    H.timeout_seam, conn,
                    [{"name": TOOL, "id": "synthetic-call", "arguments": "{}"}], 0.05), 2)
                self.assertTrue(started.is_set())
                self.assertFalse(finished.is_set())
                self.assertIs(results[0][0].action, H.register.Action.ERROR)
                self.assertFalse(observed_futures[0].done())
                reports_before = len(reports)
                release.set()
                await asyncio.wait_for(finished.wait(), 2)
                late = await asyncio.wait_for(asyncio.wrap_future(observed_futures[0]), 2)
                self.assertIs(late.action, H.register.Action.RESPONSE)
                self.assertEqual(len(results), 1)
                self.assertIs(results[0][0].action, H.register.Action.ERROR)
                self.assertEqual(len(reports), reports_before)
            finally:
                release.set()
                if observed_futures:
                    await asyncio.wait_for(asyncio.wrap_future(observed_futures[0]), 2)
        # Only these exact submit/wait loops are exercised. This does not prove
        # TTS output guards, same-round concurrency or remote cancellation.


class FixedRegression(AsyncBase):
    """Target safeguards beyond the original diagnostic red cases."""

    async def test_concurrent_initialize_shares_one_worker_and_ready_result(self):
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        try:
            await asyncio.wait_for(asyncio.gather(client.initialize(), client.initialize()), 2)
            self.assertEqual(len(H.sessions), 1)
            self.assertTrue(client.is_connected())
        finally:
            await client.cleanup()

    async def test_cleanup_while_discovery_pending_releases_initialize(self):
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        started = asyncio.Event()
        original = H.session_factory
        def pending(**kwargs):
            session = original(**kwargs)
            async def discover():
                started.set()
                await asyncio.Event().wait()
            session.list_tools = discover
            return session
        with mock.patch.object(H.client, "ClientSession", side_effect=pending), \
                mock.patch.multiple(H.client, _WORKER_GRACE_SECONDS=0.05, _WORKER_CANCEL_WAIT_SECONDS=0.05):
            task = asyncio.create_task(client.initialize())
            try:
                await asyncio.wait_for(started.wait(), 2)
                await asyncio.wait_for(client.cleanup(), 2)
                with self.assertRaises(RuntimeError):
                    await asyncio.wait_for(task, 2)
                self.assertFalse(client.is_connected())
                self.assertIsNone(client.session)
                self.assertEqual(client.get_available_tools(), [])
            finally:
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
                await client.cleanup()

    async def test_initialize_after_cleanup_builds_fresh_session(self):
        client = await self.initialized()
        first = client.session
        await client.cleanup()
        await client.initialize()
        self.assertIsNot(client.session, first)
        self.assertTrue(client.is_connected())

    async def test_cancelled_initializer_does_not_publish_partial_tools(self):
        started = asyncio.Event()
        original = H.session_factory
        def pending(**kwargs):
            session = original(**kwargs)
            async def discover():
                started.set()
                await asyncio.Event().wait()
            session.list_tools = discover
            return session
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        with mock.patch.object(H.client, "ClientSession", side_effect=pending), \
                mock.patch.multiple(H.client, _WORKER_GRACE_SECONDS=0.05, _WORKER_CANCEL_WAIT_SECONDS=0.05):
            task = asyncio.create_task(client.initialize())
            await asyncio.wait_for(started.wait(), 2)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            await asyncio.wait_for(client.cleanup(), 2)
        self.assertEqual(client.tools_dict, {})
        self.assertIsNone(client.session)

    async def test_same_tool_in_another_server_does_not_overwrite_registration(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        definition = {"type": "function", "function": {"name": TOOL, "parameters": SCHEMA}}
        first = SyntheticClient(initialize=mock.AsyncMock(), cleanup=mock.AsyncMock(),
                                      get_available_tools=lambda: [definition])
        second = SyntheticClient(initialize=mock.AsyncMock(), cleanup=mock.AsyncMock(),
                                       get_available_tools=lambda: [definition])
        with mock.patch.object(H.manager, "ServerMCPClient", side_effect=[first, second]):
            await manager._init_server("synthetic-first", {})
            await manager._init_server("synthetic-second", {})
        self.assertEqual(list(manager.clients), ["synthetic-first"])
        self.assertEqual(len(manager.tools), 1)
        second.cleanup.assert_awaited_once()
        # Registration of the conflicting service was rejected explicitly;
        # this is not a claim that both services were accepted unambiguously.

    async def test_successful_cleanup_removes_client_and_tool_registry(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        client = types.SimpleNamespace(cleanup=mock.AsyncMock())
        manager.clients["synthetic"] = client
        manager.tools = [{"function": {"name": TOOL}}]
        await manager.cleanup_all()
        self.assertEqual(manager.clients, {})
        self.assertEqual(manager.tools, [])
        client.cleanup.assert_awaited_once()

    async def test_upstream_value_error_is_execution_error_not_tool_not_found(self):
        manager, client = self.manager_with(ValueError(SYNTHETIC_SECRET))
        with self.assertRaises(H.manager.MCPToolExecutionError) as caught:
            await manager.execute_tool(TOOL, {})
        self.assertNotIn(SYNTHETIC_SECRET, str(caught.exception))
        self.assertEqual(client.call_tool.await_count, 1)
        response = await self.executor_with(error=caught.exception).execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR)

    async def test_malformed_protocol_result_and_render_error_are_safe(self):
        class BrokenRender:
            content = [{"type": "text", "text": "synthetic"}]
            isError = False
            def __str__(self):
                raise RuntimeError(SYNTHETIC_SECRET)
        variants = [None, {}, [], types.SimpleNamespace(content=[], isError=True),
                    types.SimpleNamespace(content=[{"type": "text"}], isError="false"),
                    types.SimpleNamespace(content="bad", isError=False),
                    types.SimpleNamespace(content=[{}], isError=False), BrokenRender()]
        for index, result in enumerate(variants):
            with self.subTest(variant=index):
                response = await self.executor_with(result=result).execute(None, TOOL, {})
                self.assertIs(response.action, H.register.Action.ERROR)
                self.assertIsNone(response.result)
                self.assertNotIn(SYNTHETIC_SECRET, response.response)

    async def test_structured_only_protocol_result_can_continue_without_business_verdict(self):
        for names in ({"isError": False, "structuredContent": {"synthetic": "data"}},
                      {"is_error": False, "structured_content": {"synthetic": "data"}}):
            result = types.SimpleNamespace(content=[], **names)
            response = await self.executor_with(result=result).execute(None, TOOL, {})
            self.assertIs(response.action, H.register.Action.REQLLM)
            self.assertEqual(response.result, str(result))

    async def test_snake_case_mcp_error_is_rejected(self):
        result = types.SimpleNamespace(content=[{"type": "text", "text": SYNTHETIC_SECRET}],
                                       is_error=True)
        response = await self.executor_with(result=result).execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR)
        self.assertIsNone(response.result)
        self.assertNotIn(SYNTHETIC_SECRET, response.response)

    async def test_conflicting_error_aliases_are_rejected(self):
        result = types.SimpleNamespace(content=[{"type": "text"}], isError=False, is_error=True)
        response = await self.executor_with(result=result).execute(None, TOOL, {})
        self.assertIs(response.action, H.register.Action.ERROR)

    async def test_manager_cleanup_during_initialize_blocks_late_registration(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        started, release = asyncio.Event(), asyncio.Event()
        async def initialize(**kwargs):
            started.set()
            await release.wait()
        fake = SyntheticClient(initialize=mock.AsyncMock(side_effect=initialize), cleanup=mock.AsyncMock(),
                               get_available_tools=lambda: [{"function": {"name": TOOL}}])
        with mock.patch.object(H.manager, "ServerMCPClient", return_value=fake):
            task = asyncio.create_task(manager._init_server("synthetic-server", {}))
            await asyncio.wait_for(started.wait(), 2)
            await asyncio.wait_for(manager.cleanup_all(), 2)
            release.set()
            await asyncio.wait_for(task, 2)
        self.assertEqual(manager.clients, {})
        self.assertEqual(manager.tools, [])
        self.assertEqual(manager._pending_clients, set())
        self.assertGreaterEqual(fake.cleanup.await_count, 1)

    async def test_cancelled_manager_cleanup_keeps_unfinished_clients_for_retry(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        started = asyncio.Event()
        async def slow_cleanup():
            started.set()
            await asyncio.Event().wait()
        first = SyntheticClient(cleanup=mock.AsyncMock(side_effect=slow_cleanup), get_available_tools=lambda: [])
        second = SyntheticClient(cleanup=mock.AsyncMock(), get_available_tools=lambda: [])
        manager.clients.update(first=first, second=second)
        task = asyncio.create_task(manager.cleanup_all())
        await asyncio.wait_for(started.wait(), 2)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertEqual(set(manager.clients), {"first", "second"})
        second.cleanup.assert_not_awaited()
        first.cleanup = mock.AsyncMock()
        await manager.cleanup_all()
        self.assertEqual(manager.clients, {})
        second.cleanup.assert_awaited_once()

    async def test_closed_manager_cannot_submit_even_when_cleanup_reference_remains(self):
        manager, client = self.manager_with([H.result()])
        manager._closed = True
        with self.assertRaises(H.manager.MCPToolExecutionError):
            await manager.execute_tool(TOOL, {})
        client.call_tool.assert_not_awaited()

    async def _retained_worker_cleanup(self, caller_cancelled):
        entered, release = asyncio.Event(), asyncio.Event()
        cancellations = []

        @asynccontextmanager
        async def delayed_transport(**kwargs):
            try:
                yield object(), object()
            finally:
                entered.set()
                while not release.is_set():
                    try:
                        await release.wait()
                    except asyncio.CancelledError:
                        cancellations.append(True)

        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        worker = cleanup = None
        with mock.patch.object(H.client, "sse_client", delayed_transport), \
                mock.patch.multiple(H.client, _WORKER_GRACE_SECONDS=0.02,
                                    _WORKER_CANCEL_WAIT_SECONDS=0.02):
            try:
                await client.initialize()
                worker, attempt = client._worker_task, client._attempt
                manager.clients["synthetic-review"] = client
                cleanup = asyncio.create_task(manager.cleanup_all())
                await asyncio.wait_for(entered.wait(), 2)
                if caller_cancelled:
                    cleanup.cancel()
                    with self.assertRaises(asyncio.CancelledError):
                        await cleanup
                else:
                    await asyncio.wait_for(cleanup, 2)
                await asyncio.sleep(0)
                self.assertFalse(worker.done())
                self.assertTrue(cancellations)
                self.assertIs(client._worker_task, worker)
                self.assertIs(client._attempt, attempt)
                self.assertIn("synthetic-review", manager.clients)
                self.assertFalse(client.is_connected())
                self.assertIsNone(client.session)
                self.assertEqual(client.get_available_tools(), [])
                with self.assertRaises(RuntimeError):
                    await client.call_tool(TOOL, {})
                with self.assertRaises(RuntimeError):
                    await asyncio.wait_for(client.initialize(), 2)
                self.assertEqual(len(H.sessions), 1)
                with self.assertRaises(H.manager.MCPToolExecutionError):
                    await manager.execute_tool(TOOL, {})
                # A second incomplete cleanup must not discard the same worker.
                await asyncio.wait_for(manager.cleanup_all(), 2)
                self.assertIs(client._worker_task, worker)
                self.assertIn("synthetic-review", manager.clients)
                release.set()
                await asyncio.wait_for(asyncio.gather(worker, return_exceptions=True), 2)
                await asyncio.wait_for(manager.cleanup_all(), 2)
                self.assertEqual(manager.clients, {})
                self.assertEqual(manager.tools, [])
                self.assertIsNone(client._worker_task)
                self.assertIsNone(client._attempt)
                await client.initialize()
                self.assertTrue(client.is_connected())
                self.assertIsNot(client._worker_task, worker)
                await client.cleanup()
            finally:
                release.set()
                if cleanup is not None and not cleanup.done():
                    cleanup.cancel()
                    await asyncio.gather(cleanup, return_exceptions=True)
                if worker is not None:
                    await asyncio.wait_for(asyncio.gather(worker, return_exceptions=True), 2)
                await client.cleanup()
                await manager.cleanup_all()

    async def test_real_client_pending_worker_reference_survives_cleanup_deadline(self):
        await self._retained_worker_cleanup(False)

    async def test_real_client_pending_worker_reference_survives_caller_cancel(self):
        await self._retained_worker_cleanup(True)

    async def test_successful_pending_cleanup_removes_manager_reference(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        client = SyntheticClient(cleanup=mock.AsyncMock(), get_available_tools=lambda: [])
        manager._pending_clients.add(client)
        await manager.cleanup_all()
        self.assertEqual(manager._pending_clients, set())
        client.cleanup.assert_awaited_once()

    async def test_failed_pending_cleanup_retains_reference_until_retry(self):
        manager = H.manager.ServerMCPManager(types.SimpleNamespace())
        client = SyntheticClient(cleanup=mock.AsyncMock(side_effect=RuntimeError(SYNTHETIC_SECRET)),
                               get_available_tools=lambda: [])
        manager._pending_clients.add(client)
        await manager.cleanup_all()
        self.assertIn(client, manager._pending_clients)
        self.assertFalse(any(SYNTHETIC_SECRET in entry for entry in H.logger.messages))
        client.cleanup = mock.AsyncMock()
        await manager.cleanup_all()
        self.assertEqual(manager._pending_clients, set())
        client.cleanup.assert_awaited_once()

    async def test_initialize_converts_timedelta_to_sdk_seconds(self):
        client = H.client.ServerMCPClient({"url": "https://synthetic.invalid/sse"})
        try:
            await client.initialize(read_timeout_seconds=timedelta(seconds=2))
            self.assertEqual(H.sessions[0].options["read_timeout_seconds"], 2.0)
            self.assertIsInstance(H.sessions[0].options["read_timeout_seconds"], float)
        finally:
            await client.cleanup()

    async def test_sdk_numeric_timeout_dispatch_uses_real_memory_streams_when_installed(self):
        if H.sdk == "stub":
            client = await self.initialized()
            await client.call_tool(TOOL, {}, read_timeout_seconds=timedelta(seconds=0.02))
            self.assertEqual(H.sessions[0].calls[0][1]["read_timeout_seconds"], 0.02)
            return
        import anyio
        read_sender, read_receiver = anyio.create_memory_object_stream(2)
        write_sender, write_receiver = anyio.create_memory_object_stream(2)
        session = H.sdk_session_class(read_receiver, write_sender, read_timeout_seconds=0.02)
        leaves = []
        def collect(error):
            children = getattr(error, "exceptions", None)
            if children:
                for child in children:
                    collect(child)
            else:
                leaves.append(error)
        try:
            async with session:
                try:
                    await session.initialize()
                except Exception as error:
                    collect(error)
        except Exception as error:
            collect(error)
        finally:
            await read_sender.aclose()
            await write_receiver.aclose()
        self.assertTrue(leaves)
        self.assertFalse(any(isinstance(error, TypeError) for error in leaves))
        # MCP 2.3.0 surfaces a read timeout as MCPError(code=-32001,
        # "Request ... timed out"), not the builtin TimeoutError. Accept only
        # those two explicit timeout shapes, so a numeric-accepting-but-hanging
        # session or an unrelated protocol failure still fails this requirement.
        self.assertTrue(all(
            isinstance(error, TimeoutError) or (
                type(error).__name__ == "MCPError"
                and getattr(getattr(error, "error", error), "code", None) == -32001
                and getattr(getattr(error, "error", error), "message", str(error))
                    == "Request 'initialize' timed out"
            ) for error in leaves),
            [(type(error).__name__, getattr(getattr(error, "error", error), "code", None))
             for error in leaves])

    async def test_no_unfinished_worker_after_normal_cleanup(self):
        client = await self.initialized()
        worker = client._worker_task
        await client.cleanup()
        self.assertTrue(worker.done())
        self.assertIsNone(client._worker_task)
        self.assertEqual(client.tools_dict, {})
        self.assertEqual(client.name_mapping, {})


class Recorder(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.cases = []

    def addSuccess(self, test):
        super().addSuccess(test)
        self.cases.append({"id": test.id(), "status": "pass"})

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.cases.append({"id": test.id(), "status": "fail"})

    def addError(self, test, err):
        super().addError(test, err)
        self.cases.append({"id": test.id(), "status": "error"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", choices=("stub", "installed"), required=True)
    parser.add_argument("--group", choices=("existing", "requirements", "extended", "all"), default="all")
    parser.add_argument("--output", type=Path, required=True, help="New directory only")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False, mode=0o700)
    global H
    text = io.StringIO()
    import contextlib
    # Windows implements socketpair with a loopback socket internally. Permit
    # only that standard-library helper while it runs, not arbitrary loopback
    # or external connections made by code under test.
    import threading
    local = threading.local()
    original_connect = socket.socket.connect
    original_pair = socket.socketpair
    ACCESS_ATTEMPTS.clear()

    def guarded_connect(sock, address):
        if getattr(local, "socketpair", False):
            return original_connect(sock, address)
        return deny_access("socket_connect")

    def denied(kind):
        return lambda *args, **kwargs: deny_access(kind)

    def internal_pair(*args, **kwargs):
        prior = getattr(local, "socketpair", False)
        local.socketpair = True
        try:
            return original_pair(*args, **kwargs)
        finally:
            local.socketpair = prior

    import os
    import subprocess
    # SDK imports may use Popen[...] in runtime annotations. Retain the class
    # shape while refusing every instantiation, rather than replacing it with
    # an unsubscriptable function before the real SDK is imported.
    class BlockedProcess(subprocess.Popen):
        def __init__(self, *args, **kwargs):
            deny_access("subprocess")

    with tempfile.TemporaryDirectory(prefix="issue12-offline-") as temporary, \
            mock.patch.object(socket.socket, "connect", guarded_connect), \
            mock.patch.object(socket.socket, "connect_ex", denied("socket_connect_ex")), \
            mock.patch.object(socket.socket, "sendto", denied("socket_sendto")), \
            mock.patch.object(socket, "socketpair", internal_pair), \
            mock.patch.object(socket, "create_connection", denied("create_connection")), \
            mock.patch.object(socket, "getaddrinfo", denied("dns_lookup")), \
            mock.patch.object(subprocess, "Popen", BlockedProcess), \
            mock.patch.object(os, "system", denied("os_system")), \
            mock.patch.object(os, "popen", denied("os_popen")), \
            contextlib.redirect_stdout(text), contextlib.redirect_stderr(text):
        try:
            H = Harness(args.sdk, temporary)
            suite = unittest.TestSuite()
            groups = {
                "existing": (ExistingBehavior,),
                "requirements": (RequiredBehavior, ExtendedRequirements),
                "extended": (ExtendedBehavior, ExtendedRequirements, LocalVoiceGuard, ToolTimeoutObservation),
                "all": (ExistingBehavior, RequiredBehavior, ExtendedBehavior,
                        ExtendedRequirements, LocalVoiceGuard, ToolTimeoutObservation, FixedRegression),
            }
            for group in groups[args.group]:
                suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(group))
            result = unittest.TextTestRunner(stream=text, verbosity=2, resultclass=Recorder).run(suite)
            report = {
                "schema": "issue12-offline-diagnostics-v1",
                "sdk": args.sdk, "group": args.group, "python": sys.version.split()[0],
                "installed_metadata": H.installed_metadata,
                "network": "synthetic sessions/transports; Python connection/DNS/UDP/process guards (not an OS sandbox)",
                "guard_denials": dict(ACCESS_ATTEMPTS),
                "request_count_basis": "synthetic injected IO, not upstream telemetry",
                "real_mcp_requests": 0, "real_model_requests": 0,
                "real_schema_verified": False, "account_authorization_verified": False,
                "remote_cancellation_verified": False,
                "connection_chat_timeout_seam_passed": any(
                    c["status"] == "pass" and ".ToolTimeoutObservation." in c["id"]
                    for c in result.cases),
                "voice_guard_synthetic_authority_passed": (
                    sum(".LocalVoiceGuard." in c["id"] for c in result.cases) ==
                    unittest.defaultTestLoader.loadTestsFromTestCase(LocalVoiceGuard).countTestCases()
                    and all(c["status"] == "pass" for c in result.cases
                            if ".LocalVoiceGuard." in c["id"])), 
                "run": result.testsRun, "pass": sum(c["status"] == "pass" for c in result.cases),
                "fail": len(result.failures), "error": len(result.errors), "skip": len(result.skipped),
                "cases": result.cases,
            }
        except Exception as exc:
            report = {"schema": "issue12-offline-diagnostics-v1", "sdk": args.sdk,
                      "setup_error": type(exc).__name__, "real_mcp_requests": 0,
                      "real_model_requests": 0}
            text.write("SETUP_FAILED: " + type(exc).__name__ + "\n")
        finally:
            if H is not None:
                H.close()
    # The marker exists only in controlled memory; never publish it in logs.
    log = text.getvalue().replace(SYNTHETIC_SECRET, "<synthetic-private>")
    report["source_sha256"] = {str(p.relative_to(ROOT)).replace("\\", "/"):
                               hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in [Path(__file__).resolve(), *sorted(MCP_PATH.glob("*.py")),
                                         VOICE / "plugins_func/register.py",
                                         *sorted((VOICE / "core/providers/tools/base").glob("*.py")),
                                         VOICE / "core/utils/util.py", VOICE / "core/connection.py",
                                         VOICE / "core/product_voice.py", VOICE / "core/product_rejections.py"]}
    (args.output / "tests.txt").write_text(log, encoding="utf-8")
    (args.output / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(log, end="")
    print(json.dumps({k: report.get(k) for k in ("sdk", "python", "run", "pass", "fail", "error", "skip", "setup_error")}))
    return 2 if "setup_error" in report else (0 if not (report["fail"] or report["error"]) else 1)


if __name__ == "__main__":
    raise SystemExit(main())
