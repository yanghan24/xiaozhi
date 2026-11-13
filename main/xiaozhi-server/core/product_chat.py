"""One authorized product turn: model decision, optional knowledge, spoken answer.

Each worker owns its dialogue snapshot and turn ID. Only a current turn can commit
history or queue speech; a late remote result never becomes the next turn's input.
"""
import asyncio
import copy
import hashlib
import threading
import uuid
from concurrent.futures import TimeoutError

from core.product_voice import VoiceDenied, ModelUnavailable
from core.providers.tts.dto.dto import ContentType, SentenceType, TTSMessageDTO
from core.utils.dialogue import Message
from plugins_func.register import Action


class ProductConversation:
    def __init__(self, conn):
        self.conn = conn
        self.lock = threading.RLock()
        self.pending = set()

    def current(self, turn_id):
        conn = self.conn
        return (turn_id is not None and turn_id == conn.sentence_id
                and not conn.client_abort and not conn.stop_event.is_set()
                and not conn.product_voice_gate.ended.is_set())

    def guard(self, turn_id):
        if not self.current(turn_id):
            raise VoiceDenied()

    def invalidate(self):
        with self.lock:
            self.conn.client_abort = True
            self.conn.sentence_id = None
            for future in self.pending:
                future.cancel()
            self.pending.clear()

    def begin(self):
        with self.lock:
            self.invalidate()
            self.conn.clear_queues()
            self.conn.sentence_id = uuid.uuid4().hex
            self.conn.client_abort = False
            return self.conn.sentence_id

    def _speech(self, turn_id, kind, text=None):
        with self.lock:
            self.guard(turn_id)
            self.conn.tts.tts_text_queue.put(TTSMessageDTO(
                sentence_id=turn_id, sentence_type=kind,
                content_type=ContentType.TEXT if text else ContentType.ACTION,
                content_detail=text))

    def _tool(self, turn_id, call):
        conn = self.conn
        with self.lock:
            self.guard(turn_id)
            future = asyncio.run_coroutine_threadsafe(
                conn.func_handler.handle_llm_function_call(conn, call, turn_id=turn_id), conn.loop)
            self.pending.add(future)
        try:
            result = future.result(timeout=30)
            self.guard(turn_id)
            return result
        except TimeoutError:
            future.cancel()  # Remote outcome is unknown: never replay the call.
            raise ModelUnavailable('Knowledge request timed out') from None
        finally:
            with self.lock:
                self.pending.discard(future)

    def chat(self, query, turn_id):
        conn = self.conn
        answer = []
        history = []
        try:
            self.guard(turn_id)
            conn.product_voice_gate.check()
            with self.lock:
                self.guard(turn_id)
                conn.dialogue.put(Message(role='user', content=query))
                messages = copy.deepcopy(conn.dialogue.get_llm_dialogue())
            functions = conn.func_handler.get_functions()
            prompt = conn.func_handler.knowledge_prompt
            if messages and messages[0]['role'] == 'system':
                messages[0]['content'] += '\n' + prompt
            else:
                messages.insert(0, {'role': 'system', 'content': prompt})
            self._speech(turn_id, SentenceType.FIRST)
            conn.logger.info('Product turn {} session {} input_sha256 {}',
                             turn_id, conn.session_id, hashlib.sha256(query.encode()).hexdigest())
            conn.logger.info('Product turn {} model decision started', turn_id)
            guard = lambda: self.guard(turn_id)
            calls = []
            # Buffer the decision text until we know whether knowledge is required.
            if functions:
                stream = conn.llm.response_with_functions(
                    conn.session_id, messages, functions=functions, turn_guard=guard)
                for content, fragments in stream:
                    guard()
                    if content:
                        answer.append(content)
                    if fragments:
                        conn._merge_tool_calls(calls, fragments)
            else:
                for content in conn.llm.response(conn.session_id, messages, turn_guard=guard):
                    guard()
                    answer.append(content)
            guard()
            if calls:
                # The minimum contract permits exactly one ask_knowledge per turn.
                if len(calls) != 1 or calls[0]['name'] != 'ask_knowledge' or not calls[0]['id']:
                    raise ModelUnavailable('Invalid knowledge decision')
                call = calls[0]
                conn.logger.info('Product turn {} knowledge started', turn_id)
                result = self._tool(turn_id, call)
                answer = []
                conn.logger.info('Product turn {} knowledge completed: {}', turn_id, result.action.name)
                conn.logger.info('Product turn {} knowledge result_sha256 {}', turn_id,
                                 hashlib.sha256((result.result or '').encode()).hexdigest())
                if result.action == Action.NONE:
                    return
                if result.action == Action.REQLLM:
                    tool_call = {'id': call['id'], 'type': 'function', 'function': {
                        'name': call['name'], 'arguments': call['arguments']}}
                    history = [Message(role='assistant', tool_calls=[tool_call]),
                               Message(role='tool', tool_call_id=call['id'], content=result.result)]
                    messages.extend([{'role': 'assistant', 'tool_calls': [tool_call]},
                                     {'role': 'tool', 'tool_call_id': call['id'], 'content': result.result}])
                    conn.logger.info('Product turn {} grounded continuation started', turn_id)
                    for content in conn.llm.response(conn.session_id, messages, turn_guard=guard):
                        guard()
                        answer.append(content)
                        self._speech(turn_id, SentenceType.MIDDLE, content)
                else:
                    answer = [result.response or result.result or '知识库暂时不可用，请稍后再试。']
                    self._speech(turn_id, SentenceType.MIDDLE, answer[0])
            else:
                self._speech(turn_id, SentenceType.MIDDLE, ''.join(answer))
            if not ''.join(answer).strip():
                raise ModelUnavailable('Empty model answer')
            with self.lock:
                self.guard(turn_id)
                for message in history:
                    conn.dialogue.put(message)
                conn.dialogue.put(Message(role='assistant', content=''.join(answer)))
                conn.tts.store_tts_text(turn_id, ''.join(answer))
            self._speech(turn_id, SentenceType.LAST)
        except VoiceDenied:
            return  # Local turn invalidation does not revoke an otherwise valid gate.
        except Exception:
            if self.current(turn_id):
                conn.logger.warning('Product turn {} failed; remote request will not be replayed', turn_id)
                try:
                    self._speech(turn_id, SentenceType.MIDDLE, '这次回答暂时无法完成，请稍后再试。')
                    self._speech(turn_id, SentenceType.LAST)
                except VoiceDenied:
                    pass
