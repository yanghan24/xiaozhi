#!/usr/bin/env python3
"""Initialize only the isolated, empty Xiaozhi development voice configuration."""
import os
from pathlib import Path
import pymysql
import redis
import requests
import yaml

expected = 'jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?'
if not os.environ.get('SPRING_DATASOURCE_DRUID_URL', '').startswith(expected):
    raise SystemExit('Refusing initialization outside the isolated development database')
destination = Path('/workspace/main/xiaozhi-server/data/.config.yaml')
if destination.exists():
    raise SystemExit('Voice configuration already exists; preserve its settings')
response = requests.get('http://127.0.0.1:8002/xiaozhi/user/pub-config', timeout=5)
response.raise_for_status()
connection = pymysql.connect(host='172.16.18.104', port=13306, user='xiaozhi_dev',
    password=os.environ['SPRING_DATASOURCE_DRUID_PASSWORD'], database='xiaozhi_dev')
try:
    with connection.cursor() as cursor:
        cursor.execute("SELECT COUNT(*) FROM sys_user")
        if cursor.fetchone()[0]:
            raise SystemExit('Initial bootstrap requires a new development database without users')
        cursor.execute("SELECT param_value FROM sys_params WHERE param_code='server.secret'")
        secret = cursor.fetchone()[0]
        if not secret or secret == 'null':
            raise SystemExit('Management API has not initialized its service secret')
        cursor.execute("UPDATE ai_agent_template SET asr_model_id='ASR_OpenaiASR', "
                       "mem_model_id='Memory_nomem', intent_model_id='Intent_nointent' "
                       "ORDER BY sort LIMIT 1")
        cursor.execute("UPDATE sys_params SET param_value='true' WHERE param_code='server.auth.enabled'")
    connection.commit()
finally:
    connection.close()
cache = redis.Redis(host='172.16.18.104', port=16380,
    password=os.environ['SPRING_DATA_REDIS_PASSWORD'], db=0)
cache.delete('server:config')
cache.hdel('sys:params', 'server.auth.enabled')
configuration = {
    'server': {'ip': '0.0.0.0', 'port': 8000, 'http_port': 8003,
               'vision_explain': 'http://127.0.0.1:18003/mcp/vision/explain'},
    'manager-api': {'url': 'http://127.0.0.1:8002/xiaozhi', 'secret': secret},
    'prompt_template': 'agent-base-prompt.txt',
}
descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(descriptor, 'w') as stream:
    yaml.safe_dump(configuration, stream, allow_unicode=True)
print('Initialized isolated voice configuration; provider credentials remain unconfigured')
