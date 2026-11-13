"""Product tool behavior through the handler; only MCP SDK/transport is doubled.

Run with unittest on the isolated Ubuntu backend image and synthetic config.
"""
import asyncio
import copy
import unittest
from contextlib import asynccontextmanager
from types import SimpleNamespace
from unittest.mock import patch

from mcp.types import CallToolResult, Tool
from loguru import logger

from core.product_voice import VoiceGate, VoiceDenied
from core.providers.tools.unified_tool_handler import UnifiedToolHandler
from plugins_func.register import Action


SUBJECT = "a" * 64
QUESTION = "What is the support period?"


class Authority:
    """External backend permission service boundary."""
    def __init__(self):
        self.allowed = True
        self.subject = SUBJECT

    def check_scope(self, grant):
        if not self.allowed:
            raise VoiceDenied()

    def request(self, action, body):
        return {"allowed": self.allowed, "permission_version": 7,
                "authorization_subject": self.subject}


class MCPService:
    def __init__(self):
        self.connections = []
        self.calls = []
        self.result = CallToolResult(content=[], structuredContent={
            "status": "ok", "generated": False,
            "chunks": [{"content": "Support lasts one year."}]})
        self.error = None
        self.init_error = None
        self.before_result = None
        self.knowledge_name = "ask_knowledge"

    @asynccontextmanager
    async def transport(self, url, **kwargs):
        self.connections.append(url)
        yield object(), object()

    def session(self, **kwargs):
        service = self

        class Session:
            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                return False

            async def initialize(self):
                if service.init_error:
                    raise service.init_error

            async def list_tools(self):
                return SimpleNamespace(tools=[
                    Tool(name=service.knowledge_name, description="remote untrusted description",
                         inputSchema={"type": "object"}),
                    Tool(name="unapproved_tool", inputSchema={"type": "object"}),
                ])

            async def call_tool(self, name, arguments, **kwargs):
                service.calls.append((name, arguments))
                if service.before_result:
                    await service.before_result()
                if service.error:
                    raise service.error
                return service.result

        return Session()


class ProductKnowledgeTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.service = MCPService()
        self.authority = Authority()
        gate = VoiceGate(self.authority, "test-session", 7,
                         self.authority.request("open", {}))
        self.conn = SimpleNamespace(
            device_id="test-device-b", product_voice_gate=gate,
            current_turn="turn-1", config={"product-voice": {"knowledge": {
                "device_id": "test-device-b", "authorization_subject": SUBJECT,
                "server": {"url": "https://approved.example.invalid/sse"}}}})
        self.conn.product_turn_is_current = lambda turn: turn == self.conn.current_turn
        self.patches = [
            patch("core.providers.tools.server_mcp.mcp_client.sse_client", self.service.transport),
            patch("core.providers.tools.server_mcp.mcp_client.ClientSession", self.service.session),
        ]
        for item in self.patches:
            item.start()
        self.handler = None
        self.logs = []
        self.log_sink = logger.add(self.logs.append, format="{message}")

    async def asyncTearDown(self):
        if self.handler:
            await self.handler.cleanup()
        for item in reversed(self.patches):
            item.stop()
        logger.remove(self.log_sink)

    async def start(self):
        self.handler = UnifiedToolHandler(self.conn)
        self.conn.func_handler = self.handler
        await self.handler._initialize()
        return self.handler

    async def test_only_approved_service_and_tool_are_exposed(self):
        handler = await self.start()
        self.assertEqual(self.service.connections, ["https://approved.example.invalid/sse"])
        functions = handler.get_functions()
        self.assertEqual([entry["function"]["name"] for entry in functions], ["ask_knowledge"])
        self.assertNotIn("remote untrusted description", str(functions))
        self.assertTrue(handler.has_tool("ask_knowledge"))
        self.assertFalse(handler.has_tool("unapproved_tool"))

    async def test_chunks_continue_model_without_claiming_grounded_answer(self):
        handler = await self.start()
        response = await handler.handle_llm_function_call(self.conn, {
            "name": "ask_knowledge", "arguments": {"question": QUESTION}}, turn_id="turn-1")
        self.assertEqual(self.service.calls, [("ask_knowledge", {"question": QUESTION})])
        self.assertEqual(response.action, Action.REQLLM)
        self.assertIn("Support lasts one year.", response.result)
        self.assertIsNone(response.response)

    async def test_rejects_other_names_and_noncontract_arguments_without_remote_call(self):
        handler = await self.start()
        for name, arguments in [
            ("unapproved_tool", {"question": QUESTION}),
            ("mcp_ask_knowledge", {"question": QUESTION}),
            ("ask_knowledge", {"question": ""}),
            ("ask_knowledge", {"question": " "}),
            ("ask_knowledge", {"question": "x" * 301}),
            ("ask_knowledge", {"question": 1}),
            ("ask_knowledge", {"question": QUESTION, "account": "other"}),
            ("ask_knowledge", {}), ("ask_knowledge", []),
            ("ask_knowledge", '{"question":"unterminated'),
        ]:
            with self.subTest(name=name, arguments=arguments):
                response = await handler.handle_llm_function_call(
                    self.conn, {"name": name, "arguments": arguments}, turn_id="turn-1")
                self.assertEqual(response.action, Action.ERROR)
        self.assertEqual(self.service.calls, [])

    async def test_device_registration_cannot_expand_product_tool_scope(self):
        handler = await self.start()
        await handler.register_iot_tools([{
            "name": "unapproved-device", "description": "PRIVATE_DESCRIPTION",
            "properties": [], "methods": []}])
        handler.upload_functions_desc()
        self.assertEqual(handler.current_support_functions(), ["ask_knowledge"])
        self.assertEqual(handler.get_tool_statistics(), {"server_mcp": 1})
        self.assertNotIn("PRIVATE_", "".join(self.logs))

    async def test_invalidated_turn_or_scope_cannot_execute(self):
        handler = await self.start()
        for reason in ["turn", "missing-turn", "device", "subject", "permission"]:
            with self.subTest(reason=reason):
                self.conn.product_voice_gate = VoiceGate(self.authority, "test-session", 7,
                    {"permission_version": 7, "authorization_subject": SUBJECT})
                self.conn.current_turn = "turn-2" if reason == "turn" else "turn-1"
                self.conn.device_id = "other-device" if reason == "device" else "test-device-b"
                self.authority.subject = "b" * 64 if reason == "subject" else SUBJECT
                self.authority.allowed = reason != "permission"
                response = await handler.handle_llm_function_call(self.conn, {
                    "name": "ask_knowledge", "arguments": {"question": QUESTION}},
                    turn_id=None if reason == "missing-turn" else "turn-1")
                self.assertEqual(response.action, Action.NONE)
        self.assertEqual(self.service.calls, [])

    async def test_late_result_after_stop_scope_change_or_revoke_is_discarded(self):
        handler = await self.start()
        for reason in ["turn", "device", "subject", "permission", "cleanup"]:
            with self.subTest(reason=reason):
                self.conn.current_turn = "turn-1"
                self.conn.device_id = "test-device-b"
                self.authority.allowed = True
                self.authority.subject = SUBJECT
                self.conn.product_voice_gate = VoiceGate(self.authority, "test-session", 7,
                    self.authority.request("open", {}))

                async def change_during_query():
                    if reason == "turn":
                        self.conn.current_turn = "turn-2"
                    elif reason == "device":
                        self.conn.device_id = "other-device"
                    elif reason == "subject":
                        self.authority.subject = "b" * 64
                    elif reason == "permission":
                        self.authority.allowed = False
                    else:
                        await handler.cleanup()

                self.service.before_result = change_during_query
                response = await handler.handle_llm_function_call(self.conn, {
                    "name": "ask_knowledge", "arguments": {"question": QUESTION}}, turn_id="turn-1")
                self.assertEqual(response.action, Action.NONE)
                self.assertIsNone(response.result)
                self.assertIsNone(response.response)
        self.assertEqual(len(self.service.calls), 5)

    async def test_unapproved_or_unready_knowledge_degrades_without_global_fallback(self):
        configuration = copy.deepcopy(self.conn.config)
        for reason in ["missing", "device", "subject", "failure"]:
            with self.subTest(reason=reason):
                self.conn.config = copy.deepcopy(configuration)
                scope = self.conn.config["product-voice"]["knowledge"]
                self.service.connections.clear()
                if reason == "missing":
                    self.conn.config["product-voice"].pop("knowledge")
                elif reason == "device":
                    scope["device_id"] = "other-device"
                elif reason == "subject":
                    scope["authorization_subject"] = "b" * 64
                else:
                    self.service.init_error = RuntimeError("PRIVATE_INITIALIZATION_DETAIL")
                handler = await self.start()
                self.assertEqual(handler.get_functions(), [])
                self.assertEqual(handler.knowledge_status, "unavailable")
                self.assertIn("知识库暂时不可用", handler.knowledge_prompt)
                self.assertNotIn("PRIVATE_INITIALIZATION_DETAIL", handler.knowledge_prompt)
                self.assertEqual(self.service.connections,
                    ["https://approved.example.invalid/sse"] if reason == "failure" else [])
                await handler.cleanup()

    async def test_remote_failure_is_fixed_safe_feedback_without_retry(self):
        handler = await self.start()
        self.service.error = RuntimeError("PRIVATE_EXCEPTION_DETAIL")
        response = await handler.handle_llm_function_call(self.conn, {
            "name": "ask_knowledge", "arguments": {"question": "PRIVATE_QUESTION"}}, turn_id="turn-1")
        self.assertEqual(response.action, Action.ERROR)
        self.assertEqual(response.response, "知识库查询失败，请稍后再试。")
        self.assertIsNone(response.result)
        self.assertEqual(len(self.service.calls), 1)
        self.assertNotIn("PRIVATE_", "".join(self.logs))

    async def test_closed_handler_never_initializes_or_executes_again(self):
        self.handler = UnifiedToolHandler(self.conn)
        await self.handler.cleanup()
        await self.handler._initialize()
        self.assertEqual(self.service.connections, [])
        self.assertEqual(self.handler.get_functions(), [])
        response = await self.handler.handle_llm_function_call(self.conn, {
            "name": "ask_knowledge", "arguments": {"question": QUESTION}}, turn_id="turn-1")
        self.assertEqual(response.action, Action.NONE)
        self.assertEqual(self.service.calls, [])

    async def test_sanitized_alias_cannot_impersonate_the_approved_tool(self):
        self.service.knowledge_name = "ask.knowledge"
        handler = await self.start()
        self.assertEqual(handler.get_functions(), [])
        self.assertEqual(handler.knowledge_status, "unavailable")
        response = await handler.handle_llm_function_call(self.conn, {
            "name": "ask_knowledge", "arguments": {"question": QUESTION}}, turn_id="turn-1")
        self.assertEqual(response.action, Action.ERROR)
        self.assertEqual(self.service.calls, [])


if __name__ == "__main__":
    unittest.main()
