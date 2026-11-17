#!/usr/bin/env python3
"""Check this isolated development environment without calling model providers."""
import asyncio
import json
import os
import pymysql
import redis
import requests
import websockets

if not os.environ.get('SPRING_DATASOURCE_DRUID_URL', '').startswith(
        'jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?'):
    raise SystemExit('Expected the isolated development database')
result = {}
for name, url in {
    'web': 'http://127.0.0.1:8001/',
    'api': 'http://127.0.0.1:8002/xiaozhi/user/pub-config',
    'web_api_proxy': 'http://127.0.0.1:8001/xiaozhi/user/pub-config',
    'voice': 'http://127.0.0.1:8000/',
}.items():
    response = requests.get(url, timeout=15)
    response.raise_for_status()
    if name in {'api', 'web_api_proxy'}:
        assert response.json()['code'] == 0
    if name == 'web':
        assert 'id="app"' in response.text
    result[name] = response.status_code
response = requests.get('http://127.0.0.1:8002/xiaozhi/params/list', timeout=5)
assert response.status_code == 401 or response.json().get('code') == 401
result['anonymous_admin_rejected'] = True
connection = pymysql.connect(host='172.16.18.104', port=13306, user='xiaozhi_dev',
    password=os.environ['SPRING_DATASOURCE_DRUID_PASSWORD'], database='xiaozhi_dev', connect_timeout=5)
with connection.cursor() as cursor:
    cursor.execute('SELECT VERSION(), DATABASE(), CURRENT_USER()')
    result['mysql'] = cursor.fetchone()
    cursor.execute('SELECT COUNT(*) FROM DATABASECHANGELOG')
    result['migrations'] = cursor.fetchone()[0]
connection.close()
cache = redis.Redis(host='172.16.18.104', port=16380,
    password=os.environ['SPRING_DATA_REDIS_PASSWORD'], socket_timeout=5)
assert cache.ping()
result['redis_version'] = cache.info('server')['redis_version']
try:
    redis.Redis(host='172.16.18.104', port=16380, socket_timeout=5).ping()
    raise AssertionError('Redis accepted an unauthenticated request')
except redis.AuthenticationError:
    result['anonymous_redis_rejected'] = True

async def invalid_voice():
    async with websockets.connect('ws://127.0.0.1:8000/xiaozhi/v1/',
            additional_headers={'Device-Id': '02:00:00:00:00:ee', 'Client-Id': 'dev-auth-check',
                                'Authorization': 'Bearer invalid'}, open_timeout=5) as socket:
        reply = await asyncio.wait_for(socket.recv(), timeout=5)
        assert reply == '认证失败'
        await asyncio.wait_for(socket.wait_closed(), timeout=5)
asyncio.run(invalid_voice())
result['invalid_voice_token_rejected'] = True
print(json.dumps(result, ensure_ascii=False, indent=2))
