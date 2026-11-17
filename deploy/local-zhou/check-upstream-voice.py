#!/usr/bin/env python3
"""Bounded original-runtime integration with local ASR/LLM/TTS protocol fixtures.

Real API/configuration/ownership and WebSocket/Opus/provider adapters, synthetic
accounts and credentials. No physical device or external AI service calls.
"""
import asyncio
import base64
import io
import json
import os
from pathlib import Path
import secrets
import socket
import sys
import threading
import time
import uuid
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch

import numpy as np
import opuslib_next
import pymysql
import requests
import redis
import websockets
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

assert Path('/workspace/.devcontainer/supervisord.conf').exists()
assert os.environ['SPRING_DATASOURCE_DRUID_URL'].startswith('jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?')
sys.path.insert(0, '/workspace/main/xiaozhi-server')
os.chdir('/workspace/main/xiaozhi-server')
from config.settings import load_config
from core.websocket_server import WebSocketServer
from core.utils.prompt_manager import PromptManager

api = 'http://127.0.0.1:8002/xiaozhi'
db = pymysql.connect(host='172.16.18.104', port=13306, user='xiaozhi_dev', password=os.environ['SPRING_DATASOURCE_DRUID_PASSWORD'], database='xiaozhi_dev', autocommit=True)
prefix = 'restore-' + uuid.uuid4().hex[:10]
user = 8*10**17 + secrets.randbelow(10**15)
admin = secrets.token_hex(32)
device = prefix
customer = str(uuid.uuid4())
agents, models = [], []
events = []
model_requests = []
tts_started = threading.Event()
model_release = threading.Event()
sample = io.BytesIO()
with wave.open(sample, 'wb') as audio:
    audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(24000)
    audio.writeframes((np.sin(np.arange(7200)*2*np.pi*440/24000)*3000).astype('<i2').tobytes())
sound = sample.getvalue()

def sql(query, values=()):
    with db.cursor() as cursor:
        cursor.execute(query, values)
        return cursor.fetchall()

def management(method, path, body=None):
    response = requests.request(method, api+path, json=body, headers={'Authorization': 'Bearer '+admin}, timeout=10)
    result = response.json()
    assert response.status_code == 200 and result.get('code', 0) == 0, 'Management request failed: '+path
    return result.get('data', result)

class Provider(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_POST(self):
        if self.headers.get('Transfer-Encoding', '').lower() == 'chunked':
            parts = []
            while True:
                size = int(self.rfile.readline().split(b';', 1)[0], 16)
                if not size:
                    self.rfile.readline(); break
                parts.append(self.rfile.read(size)); self.rfile.read(2)
            body = b''.join(parts)
        else:
            body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if self.path == '/asr':
            events.append(('asr_done', time.monotonic()))
            data = json.dumps({'text': '第一轮问题' if not model_requests else '第二轮问题'}).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json'); self.end_headers(); self.wfile.write(data)
        elif self.path == '/tts':
            events.append(('tts_request', time.monotonic()))
            tts_started.set()
            self.send_response(200); self.send_header('Content-Type', 'audio/wav'); self.send_header('Content-Length', str(len(sound))); self.end_headers(); self.wfile.write(sound)
        elif self.path == '/v1/chat/completions':
            request = json.loads(body)
            if not request.get('stream'):
                self.send_response(200); self.send_header('Content-Type', 'application/json'); self.end_headers()
                self.wfile.write(json.dumps({'id': 'fixture', 'object': 'chat.completion', 'created': 1, 'model': 'fixture', 'choices': [{'index': 0, 'finish_reason': 'stop', 'message': {'role': 'assistant', 'content': '协议验证'}}]}).encode()); return
            model_requests.append(request)
            events.append(('llm_first_text', time.monotonic()))
            self.send_response(200); self.send_header('Content-Type', 'text/event-stream'); self.end_headers()
            def chunk(text, finish=None):
                value = {'id': 'fixture', 'object': 'chat.completion.chunk', 'created': 1, 'model': 'fixture', 'choices': [{'index': 0, 'delta': {'content': text}, 'finish_reason': finish}]}
                self.wfile.write(('data: '+json.dumps(value, ensure_ascii=False)+'\n\n').encode()); self.wfile.flush()
            chunk('这是第一句话，可以立即开始播放。')
            assert tts_started.wait(8), 'Original TTS did not start while the model stream was open'
            assert model_release.wait(12), 'Fixture release not received'
            chunk('这是当前角色回答的结尾。')
            chunk('', 'stop'); self.wfile.write(b'data: [DONE]\n\n'); self.wfile.flush()
            events.append(('llm_done', time.monotonic()))
        else:
            self.send_error(404)

provider = ThreadingHTTPServer(('127.0.0.1', 0), Provider)
provider.daemon_threads = True
threading.Thread(target=provider.serve_forever, daemon=True).start()
origin = 'http://127.0.0.1:'+str(provider.server_port)
settings = dict(line.split('=', 1) for line in Path('/opt/xiaozhi-dev/speaker/speaker.properties').read_text().splitlines() if line and not line.startswith('#'))
original = sql('SELECT configured_agent_id,default_agent_id FROM speaker_runtime WHERE id=1')[0]

def encrypted(text):
    iv = secrets.token_bytes(12)
    key = base64.b64decode(Path(settings['speaker.encryption-key-file']).read_text().strip())
    aad = (settings['speaker.issuer']+'\n'+settings['speaker.client-id']).encode()
    return base64.b64encode(iv).decode()+'.'+base64.b64encode(AESGCM(key).encrypt(iv, text.encode(), aad)).decode()

def select(agent):
    current = management('GET', '/admin/speaker/agent-default')
    management('POST', '/admin/speaker/agent-default/'+agent, {'expected_revision': current['revision']})

async def dialogue(ticket):
    config = await load_config()
    config['server']['auth_key'] = 'synthetic-local-fixture'
    server = WebSocketServer(config)
    with patch.object(PromptManager, 'update_context_info', lambda *args: None), patch.object(PromptManager, 'build_enhanced_prompt', lambda self, prompt, *args, **kwargs: prompt):
        async with websockets.serve(server._handle_connection, '127.0.0.1', 0, process_request=server._http_response) as listener:
            url = 'ws://127.0.0.1:'+str(listener.sockets[0].getsockname()[1])+'/xiaozhi/v1/'
            headers = {'Device-Id': device, 'Client-Id': device, 'Authorization': 'Bearer '+ticket}
            async with websockets.connect(url, additional_headers=headers) as ws:
                await ws.send(json.dumps({'type': 'hello', 'version': 1, 'transport': 'websocket', 'features': {'emoji': False}, 'audio_params': {'format': 'opus', 'sample_rate': 16000, 'channels': 1, 'frame_duration': 60}}))
                hello = json.loads(await asyncio.wait_for(ws.recv(), 10)); assert hello['type'] == 'hello'
                encoder = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_AUDIO)
                pcm = (np.sin(np.arange(960)*2*np.pi*220/16000)*3000).astype('<i2').tobytes()
                for turn in range(2):
                    tts_started.clear(); model_release.clear()
                    await ws.send(json.dumps({'type': 'listen', 'state': 'start', 'mode': 'manual'}))
                    for _ in range(18):
                        await ws.send(encoder.encode(pcm, 960))
                        await asyncio.sleep(.01)
                    await asyncio.sleep(.15)
                    events.append(('listen_stop', time.monotonic()))
                    await ws.send(json.dumps({'type': 'listen', 'state': 'stop'}))
                    binary = False
                    while True:
                        packet = await asyncio.wait_for(ws.recv(), 20)
                        if isinstance(packet, bytes):
                            assert packet
                            if not binary:
                                events.append(('first_audio', time.monotonic()))
                                if turn == 0:
                                    await asyncio.to_thread(select, agents[1])
                            binary = True
                            model_release.set()
                        else:
                            message = json.loads(packet)
                            if message.get('type') == 'tts' and message.get('state') == 'stop': break
                    assert binary and len(model_requests) == turn+1
                assert 'role-A-fixture' in json.dumps(model_requests[0]['messages'])
                assert 'role-B-fixture' in json.dumps(model_requests[1]['messages'])
                assert 'role-A-fixture' not in json.dumps(model_requests[1]['messages'])
                assert '第一轮问题' not in json.dumps(model_requests[1]['messages'], ensure_ascii=False)
                tts_started.clear(); model_release.clear()
                await ws.send(json.dumps({'type': 'listen', 'state': 'detect', 'text': '请开始打断验证'}))
                while not isinstance(await asyncio.wait_for(ws.recv(), 15), bytes): pass
                await ws.send(json.dumps({'type': 'abort', 'reason': 'wake_word_detected'}))
                while True:
                    packet = await asyncio.wait_for(ws.recv(), 5)
                    if isinstance(packet, str):
                        message = json.loads(packet)
                        if message.get('type') == 'tts' and message.get('state') == 'stop': break
                events.append(('abort_stop', time.monotonic())); model_release.set()
                response = await asyncio.to_thread(requests.post, api+'/speaker/v1/actions/unbind', json={'expected_permission_version': 1, 'request_key': prefix+'-unbind'}, headers={'Authorization': 'Bearer '+credential, 'Device-Id': device}, timeout=5)
                assert response.status_code == 200
                await asyncio.wait_for(ws.wait_closed(), 6)
            async with websockets.connect(url, additional_headers=headers) as old:
                assert await asyncio.wait_for(old.recv(), 4) == '认证失败'
            # Give original queued history/title workers a bounded chance to finish.
            await asyncio.sleep(1)

try:
    sql('INSERT INTO sys_user(id,username,password,super_admin,status,create_date) VALUES (%s,%s,%s,1,1,NOW())', (user, prefix, 'unusable-fixture-password'))
    sql('INSERT INTO sys_user_token(id,user_id,token,expire_date,update_date,create_date) VALUES (%s,%s,%s,DATE_ADD(NOW(),INTERVAL 30 MINUTE),NOW(),NOW())', (user, user, admin))
    for kind, value in [('ASR', {'type': 'openai', 'model_name': 'fixture', 'base_url': origin+'/asr', 'output_dir': '/opt/xiaozhi-dev/run/voice-fixture-audio'}), ('LLM', {'type': 'openai', 'model_name': 'fixture', 'base_url': origin+'/v1', 'timeout': 15}), ('TTS', {'type': 'openai', 'api_url': origin+'/tts', 'voice': 'alloy', 'format': 'wav', 'output_dir': '/opt/xiaozhi-dev/run/voice-fixture-audio'})]:
        model = prefix+'-'+kind; models.append(model); value['api_key'] = 'synthetic-provider-key'
        sql('INSERT INTO ai_model_config(id,model_type,model_code,model_name,is_default,is_enabled,config_json,sort) VALUES (%s,%s,%s,%s,0,1,%s,0)', (model, kind, model, model, json.dumps(value)))
    for name in ('role-A-fixture', 'role-B-fixture'):
        agent = management('POST', '/agent', {'agentName': prefix+'-'+name}); agents.append(agent)
        management('PUT', '/agent/'+agent, {'systemPrompt': name, 'vadModelId': 'VAD_SileroVAD', 'asrModelId': models[0], 'llmModelId': models[1], 'slmModelId': models[1], 'ttsModelId': models[2], 'intentModelId': 'Intent_nointent', 'memModelId': 'Memory_mem_report_only', 'chatHistoryConf': 1, 'contextProviders': [], 'functions': []})
    select(agents[0])
    registration = management('POST', '/admin/speaker/devices', {'device_id': device, 'name': '原版协议检查临时音箱', 'board_type': 'protocol-fixture'})
    credential = registration['credential']; now = int(time.time()*1000)
    sql('INSERT INTO speaker_customer(id,issuer,subject,created_at_ms) VALUES (%s,%s,%s,%s)', (customer, settings['speaker.issuer'], prefix, now))
    sql("INSERT INTO speaker_grant(customer_id,client_id,access_cipher,access_expires_at_ms,token_updated_at_ms,credential_status,generation) VALUES (%s,%s,%s,%s,%s,'not_requested',%s)", (customer, settings['speaker.client-id'], encrypted('synthetic-access'), now+3600000, now, secrets.token_urlsafe(32)))
    sql('UPDATE speaker_device_state SET customer_id=%s,use_enabled=TRUE WHERE device_id=%s', (customer, device))
    state = requests.get(api+'/speaker/v1/state', headers={'Authorization': 'Bearer '+credential, 'Device-Id': device}, timeout=5).json()
    assert state['use_status'] == 'ready'
    asyncio.run(dialogue(state['voice']['token']))
    rows = sql('SELECT agent_id,customer_id FROM speaker_conversation WHERE device_id=%s', (device,))
    assert {row[0] for row in rows} == set(agents) and all(row[1] == customer for row in rows)
    history = sql('SELECT DISTINCT agent_id FROM ai_agent_chat_history WHERE mac_address=%s', (device,))
    assert {row[0] for row in history} == set(agents)
    titles = sql('SELECT COUNT(*) FROM ai_agent_chat_title WHERE session_id IN (SELECT id FROM speaker_conversation WHERE device_id=%s)', (device,))[0][0]
    assert titles == 2, 'Original session titles must use the pinned role and local model'
    print(json.dumps({'passed': ['original WS/Opus/ASR/streaming LLM/TTS/audio', 'first audio before model completion', 'default change finishes current answer and clears next-turn context', 'WS abort stops playback', 'original history and titles pinned to customer/device/role', 'unbind closes connection and denies old ticket'], 'events': [(name, round(at-events[0][1], 3)) for name, at in events], 'external_ai_requests': False}, ensure_ascii=False))
finally:
    model_release.set()
    current = sql('SELECT configured_agent_id FROM speaker_runtime WHERE id=1')[0][0]
    if current in agents: sql('UPDATE speaker_runtime SET configured_agent_id=%s,default_agent_id=%s,revision=revision+1 WHERE id=1', original)
    sql('DELETE FROM ai_agent_chat_title WHERE session_id IN (SELECT id FROM speaker_conversation WHERE device_id=%s)', (device,))
    for table in ('speaker_conversation', 'speaker_voice_session', 'speaker_voice_ticket', 'speaker_device_operation', 'speaker_login', 'speaker_event', 'speaker_device_state', 'dc_device_presence'):
        sql('DELETE FROM '+table+' WHERE device_id=%s', (device,))
    sql('DELETE FROM dc_device WHERE device_id=%s', (device,)); sql('DELETE FROM speaker_grant WHERE customer_id=%s', (customer,)); sql('DELETE FROM speaker_customer WHERE id=%s', (customer,))
    for agent in agents: management('DELETE', '/agent/'+agent)
    for model in models: sql('DELETE FROM ai_model_config WHERE id=%s', (model,))
    assert os.environ['SPRING_DATA_REDIS_HOST'] == '172.16.18.104' and os.environ['SPRING_DATA_REDIS_PORT'] == '16380'
    cache = redis.Redis(host='172.16.18.104', port=16380, password=os.environ['SPRING_DATA_REDIS_PASSWORD'], db=int(os.environ.get('SPRING_DATA_REDIS_DATABASE', 0)))
    if models: cache.delete(*['model:data:'+model for model in models])
    sql('DELETE FROM sys_user_token WHERE user_id=%s', (user,)); sql('DELETE FROM sys_user WHERE id=%s', (user,))
    db.close(); provider.shutdown(); provider.server_close()
