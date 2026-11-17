"""Default-role turn semantics with controlled model/TTS; no external resources."""
import copy
import json
import queue
import threading
from types import SimpleNamespace
from unittest.mock import Mock, patch

from core.product_chat import ProductConversation
from core.product_voice import MarketModel
from core.utils.dialogue import Dialogue


def role(name):
    return {'signature': name, 'prompt': 'You are ' + name, 'tts_voice': name + '-voice', 'llm_model': name + '-model'}


def test_default_switch_waits_until_next_turn_and_clears_only_dialogue():
    selected = [role('A')]
    gate = SimpleNamespace(check=lambda: None, ended=threading.Event(),
                           model_access=lambda: {'agent': copy.deepcopy(selected[0]), 'model': selected[0]['llm_model'], 'api_key': 'owner-fixture'})
    ready = threading.Event()
    ready.set()
    calls = []

    def respond(session, messages, **kwargs):
        snapshot = kwargs['turn_access']
        calls.append((copy.deepcopy(messages), snapshot))
        selected[0] = role('B')  # Administrator switches while A is still answering.
        yield snapshot['agent']['signature'] + '-answer'

    conn = SimpleNamespace(config={'_speaker_product': True}, product_voice_gate=gate, sentence_id='turn-1',
                           client_abort=False, stop_event=threading.Event(), components_initialized=ready,
                           initialization={'status': 'ready'}, dialogue=Dialogue(), device_id='device-fixture',
                           session_id='session-fixture', logger=Mock(), func_handler=SimpleNamespace(get_functions=lambda: [], knowledge_prompt=''),
                           llm=SimpleNamespace(response=respond), tts=SimpleNamespace(voice='old-voice', tts_text_queue=queue.Queue(), store_tts_text=lambda *args: None))
    conn.change_system_prompt = conn.dialogue.update_system_message
    conversation = ProductConversation(conn)
    conversation.chat('first-question', 'turn-1')
    assert calls[0][1]['model'] == 'A-model'
    assert conn.tts.voice == 'A-voice'
    assert conn.config['_speaker_role_signature'] == 'A'
    assert any(message.content == 'A-answer' for message in conn.dialogue.dialogue)
    conn.sentence_id = 'turn-2'
    conversation.chat('second-question', 'turn-2')
    assert calls[1][1]['model'] == 'B-model'
    assert all(message.get('content') not in ('first-question', 'A-answer') for message in calls[1][0])
    assert conn.tts.voice == 'B-voice'
    assert conn.device_id == 'device-fixture'


def test_model_transport_uses_turn_snapshot_without_refetching_new_default():
    gate = SimpleNamespace(check=lambda: None, model_access=Mock(side_effect=AssertionError('must not refetch mid-turn')))
    sent = []

    class Reply:
        def __enter__(self): return self
        def __exit__(self, *args): pass
        def __iter__(self): return iter([b'data: {"choices":[{"delta":{"content":"answer"}}]}\n', b'data: [DONE]\n'])

    class Opener:
        def open(self, request, **kwargs):
            sent.append(json.loads(request.data))
            return Reply()

    access = {'model': 'selected-model', 'base_url': 'http://127.0.0.1/v1', 'api_key': 'owner-fixture'}
    with patch('urllib.request.build_opener', return_value=Opener()):
        answer = list(MarketModel(gate).response('session-fixture', [], turn_access=access))
    assert answer == ['answer']
    assert sent[0]['model'] == 'selected-model'
    gate.model_access.assert_not_called()
