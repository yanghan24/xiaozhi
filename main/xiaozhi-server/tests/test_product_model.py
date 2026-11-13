"""Product model behavior across real local HTTP, without external credentials."""
import asyncio
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
import threading
import unittest

from core.product_voice import MarketModel, ModelUnavailable, VoiceAuthority, VoiceDenied


TOOLS = [{"type": "function", "function": {
    "name": "ask_knowledge", "description": "Search the approved knowledge base",
    "parameters": {"type": "object", "properties": {"question": {"type": "string"}},
                   "required": ["question"], "additionalProperties": False}}}]


def stream(*deltas, done=True):
    lines = [b'data: ' + json.dumps({"choices": [{"delta": delta}]}).encode() + b'\n\n'
             for delta in deltas]
    if done:
        lines.append(b'data: [DONE]\n\n')
    return b''.join(lines)


class HTTPFixture:
    def __init__(self):
        self.allowed = True
        self.key = 'synthetic-owner-key'
        self.requests = []
        self.status = 200
        self.output = stream({"content": "Ordinary answer."})
        self.before_model_reply = lambda: None
        self.on_model_access = lambda: None
        self.rejection_received = threading.Event()
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def send_json(self, body):
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(json.dumps(body).encode())

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                if self.path == '/v1/chat/completions':
                    fixture.requests.append({'body': body, 'authorization': self.headers.get('Authorization')})
                    fixture.before_model_reply()
                    self.send_response(fixture.status)
                    self.send_header('Content-Type', 'text/event-stream')
                    self.end_headers()
                    self.wfile.write(fixture.output)
                    return
                if self.headers.get('X-Voice-Service-Key') != 'synthetic-voice-service-secret-123456':
                    self.send_error(403)
                    return
                action = self.path.rsplit('/', 1)[-1]
                grant = {'session': 's' * 43, 'permission_version': 7,
                         'authorization_scope': 'g' * 43, 'authorization_subject': 'a' * 64,
                         'permission_authorization_order': 2}
                if action in ('open', 'check'):
                    self.send_json({**grant, 'allowed': fixture.allowed})
                elif action == 'model-access':
                    fixture.on_model_access()
                    self.send_json({**grant, 'model': 'qwen3.8-flash', 'api_key': fixture.key,
                                    'rejection_receipt': 'synthetic-receipt', 'authorization_order': 2,
                                    'base_url': fixture.base + '/v1'})
                elif action == 'key-rejected':
                    fixture.rejection_received.set()
                    self.send_json({'recorded': True})
                elif action == 'close':
                    self.send_json({})
                else:
                    self.send_error(404)

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.base = 'http://127.0.0.1:' + str(self.server.server_port)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


class ProductModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Keep outbox directories alive until process exit: its daemon has no stop API.
        cls.directory = tempfile.mkdtemp(prefix='product-model-test-')

    def setUp(self):
        self.market = HTTPFixture()
        self.addCleanup(self.market.close)
        self.authority = VoiceAuthority(self.market.base + '/authority',
            'synthetic-voice-service-secret-123456',
            str(Path(self.directory) / self.id().rsplit('.', 1)[-1]))
        self.gate = asyncio.run(self.authority.open(
            {'Device-Id': 'test-device', 'Authorization': 'Bearer ' + 't' * 43}, '/xiaozhi/v1/'))
        self.model = MarketModel(self.gate)

    def test_model_can_choose_knowledge_tool_from_fragmented_stream(self):
        self.market.output = stream(
            {'tool_calls': [{'index': 0, 'id': 'call-1', 'type': 'function',
                             'function': {'name': 'ask_knowledge', 'arguments': '{"question":'}}]},
            {'tool_calls': [{'index': 0, 'function': {'arguments': '"public question"}'}}]})
        messages = [{'role': 'user', 'content': 'A public knowledge question'}]
        chunks = list(self.model.response_with_functions('chat', messages, functions=TOOLS))
        self.assertEqual(len(chunks), 2)
        self.assertEqual(chunks[0][1][0].function.name, 'ask_knowledge')
        self.assertEqual(chunks[0][1][0].id, 'call-1')
        self.assertEqual(chunks[1][1][0].index, 0)
        self.assertEqual(''.join(chunk[1][0].function.arguments for chunk in chunks),
                         '{"question":"public question"}')
        self.assertEqual(self.market.requests, [{'authorization': 'Bearer synthetic-owner-key',
            'body': {'model': 'qwen3.8-flash', 'messages': messages, 'stream': True, 'tools': TOOLS}}])

    def test_stopped_turn_during_authorization_never_submits_model_request(self):
        stopped = threading.Event()
        self.market.on_model_access = stopped.set

        def current_turn():
            if stopped.is_set():
                raise VoiceDenied()

        for method in (self.model.response, self.model.response_with_functions):
            with self.subTest(method=method.__name__):
                stopped.clear()
                with self.assertRaises(VoiceDenied):
                    list(method('chat', [{'role': 'user', 'content': 'hello'}],
                                functions=TOOLS, turn_guard=current_turn))
        self.assertEqual(self.market.requests, [])

    def test_permission_revoked_during_authorization_blocks_model_submission(self):
        self.market.on_model_access = lambda: setattr(self.market, 'allowed', False)
        with self.assertRaises(VoiceDenied):
            list(self.model.response_with_functions('chat', [{'role': 'user', 'content': 'hello'}],
                                                    functions=TOOLS))
        self.assertEqual(self.market.requests, [])

    def test_malformed_model_deltas_fail_safely_without_replay(self):
        for delta in [
            {'content': {'private': 'upstream-secret'}},
            {'tool_calls': [{'index': -1, 'function': {'arguments': 'upstream-secret'}}]},
            {'tool_calls': [{'index': True, 'function': {'arguments': 'upstream-secret'}}]},
        ]:
            with self.subTest(delta=delta):
                self.market.output = stream(delta)
                with self.assertRaises(ModelUnavailable) as failure:
                    list(self.model.response_with_functions('chat', [], functions=TOOLS))
                self.assertNotIn('upstream-secret', str(failure.exception))
        self.assertEqual(len(self.market.requests), 3)

    def test_ordinary_answer_and_tool_continuation_use_current_owner_access(self):
        self.market.output = stream({'reasoning_content': 'private reasoning'},
                                    {'content': 'Ordinary answer.'})
        self.assertEqual(list(self.model.response('chat', [{'role': 'user', 'content': 'hello'}])),
                         ['Ordinary answer.'])
        self.market.key = 'synthetic-refreshed-owner-key'
        messages = [
            {'role': 'user', 'content': 'A public knowledge question'},
            {'role': 'assistant', 'tool_calls': [{'id': 'call-1', 'type': 'function',
                'function': {'name': 'ask_knowledge', 'arguments': '{"question":"public question"}'}}]},
            {'role': 'tool', 'tool_call_id': 'call-1', 'content': 'Relevant reference passage'},
        ]
        self.market.output = stream({'content': 'Answer from the reference.'})
        self.assertEqual(list(self.model.response_with_functions('chat', messages, functions=TOOLS)),
                         [('Answer from the reference.', None)])
        self.assertEqual([r['authorization'] for r in self.market.requests],
                         ['Bearer synthetic-owner-key', 'Bearer synthetic-refreshed-owner-key'])
        self.assertEqual(self.market.requests[1]['body']['messages'], messages)

    def test_rejected_key_blocks_peer_and_is_not_replayed(self):
        peer = asyncio.run(self.authority.open(
            {'Device-Id': 'test-device', 'Authorization': 'Bearer ' + 't' * 43}, '/xiaozhi/v1/'))
        self.market.status = 401
        self.market.output = b'{"error":"upstream-secret"}'
        with self.assertRaises(VoiceDenied):
            list(self.model.response_with_functions('chat', [], functions=TOOLS))
        with self.assertRaises(VoiceDenied):
            peer.check()
        with self.assertRaises(VoiceDenied):
            list(self.model.response('chat', []))
        self.assertTrue(self.market.rejection_received.wait(2))
        self.assertEqual(len(self.market.requests), 1)

    def test_resource_and_stream_failures_preserve_grant_without_replay(self):
        cases = [
            (400, b'{"error":"upstream-secret credit failure"}'),
            (503, b'{"error":"upstream-secret transport failure"}'),
            (200, b'data: upstream-secret invalid JSON\n\n'),
            (200, stream({'content': 'Partial answer'}, done=False)),
            (200, b'data: {"error":{"message":"upstream-secret"}}\n\n'),
        ]
        for index, (status, output) in enumerate(cases, 1):
            with self.subTest(status=status, case=index):
                self.market.status, self.market.output = status, output
                with self.assertRaises(ModelUnavailable) as failure:
                    list(self.model.response_with_functions('chat', [], functions=TOOLS))
                self.assertNotIn('upstream-secret', str(failure.exception))
                self.gate.check()
                self.assertEqual(len(self.market.requests), index)

    def test_late_response_cannot_escape_turn_stop_or_permission_change(self):
        stopped = threading.Event()
        self.market.before_model_reply = stopped.set

        def current_turn():
            if stopped.is_set():
                raise VoiceDenied()

        with self.assertRaises(VoiceDenied):
            list(self.model.response('chat', [], turn_guard=current_turn))
        self.gate.check()  # A local turn ending does not revoke the device's grant.
        self.market.before_model_reply = lambda: setattr(self.market, 'allowed', False)
        with self.assertRaises(VoiceDenied):
            list(self.model.response_with_functions('chat', [], functions=TOOLS))
        self.assertEqual(len(self.market.requests), 2)


if __name__ == '__main__':
    unittest.main()
