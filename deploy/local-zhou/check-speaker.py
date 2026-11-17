#!/usr/bin/env python3
"""Scoped development HTTP/voice check. Synthetic identities only; no AI Market/model calls.

Creates its own short-lived admin/customer/device fixtures, then removes them.
Restarts only this container's API to verify persisted ownership/permission state.
"""
import asyncio
import base64
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time
import uuid

import pymysql
import requests
import websockets
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

assert os.environ.get('SPRING_DATASOURCE_DRUID_URL', '').startswith('jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?'), 'Isolated development database required'
assert Path('/workspace/.devcontainer/supervisord.conf').exists(), 'Run inside xiaozhi-dev'
sys.path.insert(0, '/workspace/main/xiaozhi-server')
from core.product_voice import VoiceAuthority, VoiceDenied

settings = dict(line.split('=', 1) for line in Path('/opt/xiaozhi-dev/speaker/speaker.properties').read_text().splitlines() if line and not line.startswith('#'))
assert settings['speaker.enabled'] == 'true'
base = 'http://127.0.0.1:8002/xiaozhi'
connection = pymysql.connect(host='172.16.18.104', port=13306, user='xiaozhi_dev', password=os.environ['SPRING_DATASOURCE_DRUID_PASSWORD'], database='xiaozhi_dev', autocommit=True)
prefix = 'speaker-http-' + uuid.uuid4().hex[:16]
device = prefix
customer = str(uuid.uuid4())
users = [secrets.randbelow(10**15) + 8 * 10**17 for _ in range(2)]
tokens = [secrets.token_hex(32) for _ in users]
roles = []
original_default = None
default_snapshot_read = False
checks = []


def sql(statement, values=()):
    with connection.cursor() as cursor:
        cursor.execute(statement, values)
        return cursor.fetchall()


def request(route, body=None, token=tokens[0], device_id=None):
    headers = {'Authorization': 'Bearer ' + token} if token else {}
    if device_id:
        headers['Device-Id'] = device_id
    response = requests.request('GET' if body is None else 'POST', base + route, json=body, headers=headers, timeout=10)
    return response, response.json()


def ok(route, body=None, **kwargs):
    response, data = request(route, body, **kwargs)
    assert response.status_code == 200 and data.get('code', 0) == 0, 'Unexpected response at ' + route
    return data


def encrypted(value):
    iv = secrets.token_bytes(12)
    key = base64.b64decode(Path(settings['speaker.encryption-key-file']).read_text().strip())
    aad = (settings['speaker.issuer'] + '\n' + settings['speaker.client-id']).encode()
    return base64.b64encode(iv).decode() + '.' + base64.b64encode(AESGCM(key).encrypt(iv, value.encode(), aad)).decode()


async def voice_check(ticket):
    authority = VoiceAuthority(base, settings['speaker.voice-service-key'], '/opt/xiaozhi-dev/run/speaker-http-rejections', 2)
    gate = await authority.open({'Device-Id': device, 'Authorization': 'Bearer ' + ticket['token']}, '/xiaozhi/v1/')
    access = await asyncio.to_thread(gate.model_access)
    assert access['api_key'] == prefix + '-synthetic-key' and access['agent']['id'] == roles[0]
    assert access['base_url'] == 'http://127.0.0.1:9/v1'  # Never a real provider.
    async with websockets.connect(ticket['url'], additional_headers={'Device-Id': device, 'Authorization': 'Bearer ' + ticket['token']}, open_timeout=5) as socket:
        await socket.send(json.dumps({'type': 'hello', 'version': 1, 'transport': 'websocket', 'audio_params': {'format': 'opus', 'sample_rate': 16000, 'channels': 1, 'frame_duration': 60}}))
        hello = json.loads(await asyncio.wait_for(socket.recv(), 10))
        assert hello['type'] == 'hello'
        checks.append('authenticated WebSocket hello without audio/provider requests')
        current = ok('/admin/speaker/agents')
        role = next(row for row in current['items'] if row['id'] == roles[0])
        role['prompt'] = '仅限开发协议夹具的第二个角色提示词'
        ok('/admin/speaker/agents/' + roles[0], {field: role[field] for field in ('name', 'prompt', 'llm_model', 'asr_model', 'tts_voice', 'version')})
        await asyncio.to_thread(gate.check)
        next_access = await asyncio.to_thread(gate.model_access)
        assert next_access['agent']['signature'] != access['agent']['signature']
        checks.append('role revision preserves device/session and changes next snapshot')
        result = ok('/speaker/v1/actions/unbind', {'expected_permission_version': 1, 'request_key': prefix + '-unbind'}, token=credential, device_id=device)
        repeated = ok('/speaker/v1/actions/unbind', {'expected_permission_version': 1, 'request_key': prefix + '-unbind'}, token=credential, device_id=device)
        assert result == repeated
        try:
            await asyncio.to_thread(gate.check)
            raise AssertionError('Old voice session survived unbind')
        except VoiceDenied:
            pass
        await asyncio.wait_for(socket.wait_closed(), 10)
        assert socket.close_code == 1008
        checks.append('unbind is idempotent and closes old voice session')
    await gate.close()


try:
    public = requests.get(base + '/user/pub-config', timeout=5).json()
    assert public['data']['speakerMode'] is True
    discovery = requests.post(base + '/ota/', headers={'Device-Id': '00:00:00:00:00:01'}, json={}, timeout=5).json()
    assert discovery['device_control']['version'] == 2 and type(discovery['server_time']['timestamp']) is int
    assert 'activation' not in discovery
    for route in ('/admin/speaker/devices', '/admin/speaker/customers', '/admin/speaker/agents'):
        response, data = request(route, token='')
        assert response.status_code in (401, 403) or data.get('code', 0) != 0
    for index, (user_id, token) in enumerate(zip(users, tokens)):
        sql('INSERT INTO sys_user(id,username,password,super_admin,status,create_date) VALUES (%s,%s,%s,%s,1,NOW())', (user_id, prefix + '-' + str(index), 'unusable-fixture-password', int(index == 0)))
        sql('INSERT INTO sys_user_token(id,user_id,token,expire_date,update_date,create_date) VALUES (%s,%s,%s,DATE_ADD(NOW(),INTERVAL 15 MINUTE),NOW(),NOW())', (user_id, user_id, token))
    response, data = request('/admin/speaker/devices', token=tokens[1])
    assert response.status_code in (401, 403) or data.get('code', 0) != 0
    response = requests.post(base + '/user/register', json={}, timeout=5)
    assert response.status_code == 403
    checks.append('management denies anonymous/ordinary accounts and registration')
    current = ok('/admin/speaker/agents')
    original_default = current['default_agent_id']; default_snapshot_read = True
    assert original_default is None, 'This initial-default check requires the unconfigured development environment'
    registration = ok('/admin/speaker/devices', {'device_id': device, 'name': '开发协议夹具，检查后自动清理', 'board_type': 'p4-fixture'})
    credential = registration['credential']
    response, _ = request('/speaker/v1/state', token=credential, device_id=device + '-other')
    assert response.status_code == 401
    ok('/speaker/v1/heartbeat', {'board_type': 'p4-fixture', 'firmware_version': 'phase1-http-fixture'}, token=credential, device_id=device)
    state = ok('/speaker/v1/state', token=credential, device_id=device)
    assert state['protocol_version'] == 2 and state['binding_state'] == 'unbound' and state['voice'] is None
    now = int(time.time() * 1000)
    sql('INSERT INTO speaker_customer(id,issuer,subject,display_name,created_at_ms) VALUES (%s,%s,%s,%s,%s)', (customer, settings['speaker.issuer'], prefix, '协议夹具客户', now))
    sql("INSERT INTO speaker_grant(customer_id,client_id,access_cipher,access_expires_at_ms,token_updated_at_ms,credential_cipher,credential_status,credential_base,generation) VALUES (%s,%s,%s,%s,%s,%s,'active',%s,%s)", (customer, settings['speaker.client-id'], encrypted('synthetic-access'), now + 86400000, now, encrypted(prefix + '-synthetic-key'), 'http://127.0.0.1:9', secrets.token_urlsafe(32)))
    sql('UPDATE speaker_device_state SET customer_id=%s,use_enabled=TRUE WHERE device_id=%s', (customer, device))
    state = ok('/speaker/v1/state', token=credential, device_id=device)
    assert state['use_status'] == 'default_agent_required' and state['voice'] is None
    checks.append('authenticated heartbeat is independent of binding; explicit initial role required')
    agent = ok('/admin/speaker/agents', {'name': prefix, 'prompt': '仅限开发协议夹具，不能调用真实模型', 'llm_model': 'synthetic-model', 'asr_model': 'rtasr-llm', 'tts_voice': 'zh-CN-XiaoxiaoNeural', 'version': 0})
    roles.append(agent['id'])
    ok('/admin/speaker/agents/' + roles[0] + '/default', {'expected_revision': current['revision']})
    state = ok('/speaker/v1/state', token=credential, device_id=device)
    assert state['use_status'] == 'ready'
    detail = ok('/admin/speaker/customers/' + customer)
    assert len(detail['devices']) == 1 and detail['devices'][0]['online'] is True
    assert 'synthetic-key' not in json.dumps(detail)
    asyncio.run(voice_check(state['voice']))
    assert sql('SELECT credential_status FROM speaker_grant WHERE customer_id=%s', (customer,))[0][0] == 'active'
    subprocess.run(['supervisorctl', '-c', '/workspace/.devcontainer/supervisord.conf', 'restart', 'api'], check=True, stdout=subprocess.DEVNULL)
    for attempt in range(100):
        try:
            if requests.get(base + '/user/pub-config', timeout=1).status_code == 200:
                break
        except requests.RequestException:
            pass
        time.sleep(.2)
    state = ok('/speaker/v1/state', token=credential, device_id=device)
    assert state['binding_state'] == 'unbound' and state['permission_version'] == 2
    ok('/speaker/v1/heartbeat', {'firmware_version': 'phase1-http-fixture'}, token=credential, device_id=device)
    assert ok('/admin/speaker/customers/' + customer)['devices'] == []
    checks.append('API restart preserves unbound state; heartbeat continues and user count is zero')
finally:
    if default_snapshot_read:
        selected = sql('SELECT default_agent_id FROM speaker_runtime WHERE id=1')[0][0]
        if selected in roles:
            sql('UPDATE speaker_runtime SET default_agent_id=%s,revision=revision+1 WHERE id=1 AND default_agent_id=%s', (original_default, selected))
    for table in ('speaker_voice_session', 'speaker_voice_ticket', 'speaker_device_operation', 'speaker_login', 'speaker_event', 'speaker_device_state', 'dc_device_presence'):
        sql('DELETE FROM ' + table + ' WHERE device_id=%s', (device,))
    sql('DELETE FROM dc_device WHERE device_id=%s', (device,))
    sql('DELETE FROM speaker_grant WHERE customer_id=%s', (customer,))
    sql('DELETE FROM speaker_customer WHERE id=%s', (customer,))
    for role in roles:
        sql('DELETE FROM speaker_agent WHERE id=%s', (role,))
    for user_id in users:
        sql('DELETE FROM sys_user_token WHERE user_id=%s', (user_id,))
        sql('DELETE FROM sys_user WHERE id=%s', (user_id,))
    connection.close()
print(json.dumps({'passed': checks, 'synthetic_fixtures_removed': True, 'external_authorization_or_model_calls': False}, ensure_ascii=False))
