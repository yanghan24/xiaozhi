#!/usr/bin/env python3
"""Only the restored agent-management APIs/UI, using disposable development fixtures."""
import json
import os
from pathlib import Path
import secrets
import subprocess
import uuid
import base64
import time
import yaml
import pymysql
import requests
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

assert os.environ.get('SPRING_DATASOURCE_DRUID_URL','').startswith('jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?')
base='http://127.0.0.1:8002/xiaozhi'
db=pymysql.connect(host='172.16.18.104',port=13306,user='xiaozhi_dev',password=os.environ['SPRING_DATASOURCE_DRUID_PASSWORD'],database='xiaozhi_dev',autocommit=True)
prefix='agent-ui-'+uuid.uuid4().hex[:12]
users=[8*10**17+secrets.randbelow(10**15) for _ in range(2)]
tokens=[secrets.token_hex(32) for _ in users]
agents=[]
mcp_id=None
device=prefix+'-voice'
customer=str(uuid.uuid4())
checks=[]
identity=Path('/opt/xiaozhi-dev/run/agent-ui-fixture.json')

def sql(query,args=()):
    with db.cursor() as cursor:
        cursor.execute(query,args)
        return cursor.fetchall()

def request(method,path,body=None,operator=0,success=True):
    r=requests.request(method,base+path,headers={'Authorization':'Bearer '+tokens[operator]},json=body,timeout=15)
    data=r.json()
    if success:
        assert r.status_code==200 and data.get('code',0)==0, 'Agent API failed at '+path
    else:
        return data
    return data.get('data') if 'data' in data else data

original=sql('SELECT configured_agent_id,default_agent_id FROM speaker_runtime WHERE id=1')[0]
try:
    for user,token in zip(users,tokens):
        sql('INSERT INTO sys_user(id,username,password,super_admin,status,create_date) VALUES (%s,%s,%s,1,1,NOW())',(user,prefix+str(user),'unusable-fixture-password'))
        sql('INSERT INTO sys_user_token(id,user_id,token,expire_date,update_date,create_date) VALUES (%s,%s,%s,DATE_ADD(NOW(),INTERVAL 30 MINUTE),NOW(),NOW())',(user,user,token))
    templates=request('GET','/agent/template')
    assert {'湾湾小何','星际游子','英语老师','好奇男孩','汪汪队长'} <= {t['agentName'] for t in templates}
    for operator in range(2):
        agents.append(request('POST','/agent',{'agentName':prefix+'-'+str(operator)},operator=operator))
    assert set(agents) <= {a['id'] for a in request('GET','/agent/list')}
    draft=request('GET','/agent/'+agents[0])
    draft['systemPrompt']='我是{{assistant_name}}，仅用于管理界面定向检查。'
    draft['ttsRate']=12;draft['ttsVolume']=55;draft['ttsPitch']=3
    draft['intentModelId']='Intent_nointent'
    draft['tagNames']=[prefix]
    payload={key:draft.get(key) for key in ('agentName','systemPrompt','asrModelId','vadModelId','llmModelId','slmModelId','vllmModelId','ttsModelId','ttsVoiceId','ttsLanguage','ttsRate','ttsVolume','ttsPitch','memModelId','intentModelId','summaryMemory','contextProviders','correctWordFileIds','tagNames')}
    request('PUT','/agent/'+agents[0],payload)
    saved=request('GET','/agent/'+agents[0]);assert saved['ttsRate']==12 and saved['ttsVolume']==55
    snapshots=request('GET','/agent/'+agents[0]+'/snapshots?page=1&limit=10')
    assert snapshots['total']>=2
    status=request('GET','/admin/speaker/agent-default')
    request('POST','/admin/speaker/agent-default/'+agents[0],{'expected_revision':status['revision']})
    rejected=request('DELETE','/agent/'+agents[0],success=False)
    assert '当前默认智能体不能删除' in json.dumps(rejected,ensure_ascii=False)
    assert request('GET','/admin/speaker/agent-default')['agent_id']==agents[0]
    # A disposable voice context verifies the selected full agent's runtime
    # projection. No socket audio, OAuth, model or synthesis request is made.
    settings=dict(line.split('=',1) for line in Path('/opt/xiaozhi-dev/speaker/speaker.properties').read_text().splitlines() if line and not line.startswith('#'))
    def encrypted(value):
        iv=secrets.token_bytes(12)
        key=base64.b64decode(Path(settings['speaker.encryption-key-file']).read_text().strip())
        aad=(settings['speaker.issuer']+'\n'+settings['speaker.client-id']).encode()
        return base64.b64encode(iv).decode()+'.'+base64.b64encode(AESGCM(key).encrypt(iv,value.encode(),aad)).decode()
    registered=request('POST','/admin/speaker/devices',{'device_id':device,'name':'智能体衔接检查临时上下文','board_type':'protocol-fixture'})
    now=int(time.time()*1000)
    sql('INSERT INTO speaker_customer(id,issuer,subject,created_at_ms) VALUES (%s,%s,%s,%s)',(customer,settings['speaker.issuer'],prefix,now))
    sql("INSERT INTO speaker_grant(customer_id,client_id,access_cipher,access_expires_at_ms,token_updated_at_ms,credential_cipher,credential_status,credential_base,generation) VALUES (%s,%s,%s,%s,%s,%s,'active',%s,%s)",(customer,settings['speaker.client-id'],encrypted('synthetic-access'),now+86400000,now,None,'http://127.0.0.1:9',secrets.token_urlsafe(32)))
    sql('UPDATE speaker_device_state SET customer_id=%s,use_enabled=TRUE WHERE device_id=%s',(customer,device))
    state=requests.get(base+'/speaker/v1/state',headers={'Device-Id':device,'Authorization':'Bearer '+registered['credential']},timeout=5).json()
    assert state['use_status']=='ready'
    service_secret=yaml.safe_load(Path('/workspace/main/xiaozhi-server/data/.config.yaml').read_text())['manager-api']['secret']
    private_headers={'Authorization':'Bearer '+service_secret}
    def private(path,body):
        r=requests.post(base+'/config/'+path,headers=private_headers,json=body,timeout=5)
        assert r.status_code==200 and r.json()['code']==0, 'Original private configuration request failed'
        return r.json()['data']
    opened=private('device-session/open',{'device_id':device,'token':state['voice']['token']})
    def runtime():
        return private('agent-models',{'macAddress':device,'clientId':device,'selectedModule':{}})
    first=runtime()
    assert first['TTS'][first['selected_module']['TTS']]['ttsRate']==12
    assert first['memory_scope'] and first['summaryMemory']==''
    assert 'mcp_servers' in first and 'agent' not in first
    payload['ttsRate']=24
    request('PUT','/agent/'+agents[0],payload)
    changed=runtime()
    assert changed['TTS'][changed['selected_module']['TTS']]['ttsRate']==24
    assert changed['configuration_revision']!=first['configuration_revision']
    assert private('device-session/check',{'session':opened['session']})['allowed'] is True
    checks.append('full original providers/voice parameters change without customer resource Key')
    checks.extend(['five original templates available','administrators share complete agent list','full configuration/voice parameters persist','version history preserved','default selection and default deletion protection'])
    mcp_id=request('POST','/admin/speaker/mcp',{'name':prefix+'-mcp','url':'https://fixture.example/mcp','transport':'streamable-http','headers':None,'clear_credentials':False,'enabled':False,'version':0})['id']
    request('POST','/admin/speaker/agent-mcp/'+agents[0],[mcp_id])
    public=requests.get(base+'/user/pub-config',timeout=5).json()['data']
    identity.write_text(json.dumps({'token':tokens[0],'user':{'id':str(users[0]),'username':'界面检查管理员','superAdmin':1},'public':public,'agent':agents[0],'name':draft['agentName'],'mcpName':prefix+'-mcp'}));identity.chmod(0o600)
    subprocess.run(['node','main/manager-web/scripts/check-agent-ui.cjs'],check=True)
    checks.append('browser card/editor/template/model and voice selectors/advanced settings/history')
finally:
    current=sql('SELECT configured_agent_id FROM speaker_runtime WHERE id=1')[0][0]
    if current in agents:
        sql('UPDATE speaker_runtime SET configured_agent_id=%s,default_agent_id=%s,revision=revision+1 WHERE id=1 AND configured_agent_id=%s',(*original,current))
    for table in ('speaker_conversation','speaker_voice_session','speaker_voice_ticket','speaker_device_operation','speaker_login','speaker_event','speaker_device_state','dc_device_presence'):
        sql('DELETE FROM '+table+' WHERE device_id=%s',(device,))
    sql('DELETE FROM dc_device WHERE device_id=%s',(device,))
    sql('DELETE FROM speaker_grant WHERE customer_id=%s',(customer,))
    sql('DELETE FROM speaker_customer WHERE id=%s',(customer,))
    for agent in agents:
        request('DELETE','/agent/'+agent)
        assert sql('SELECT COUNT(*) FROM speaker_agent_mcp WHERE agent_id=%s',(agent,))[0][0]==0
    if mcp_id:sql('DELETE FROM speaker_mcp WHERE id=%s',(mcp_id,))
    # Tags are otherwise intentionally shared by agents; remove only our test tag.
    sql('DELETE FROM ai_agent_tag WHERE tag_name=%s',(prefix,))
    for user in users:
        sql('DELETE FROM sys_user_token WHERE user_id=%s',(user,))
        sql('DELETE FROM sys_user WHERE id=%s',(user,))
    db.close()
    if identity.exists():identity.unlink()
print(json.dumps({'passed':checks,'fixtures_removed':True,'external_model_audio_requests':False},ensure_ascii=False))
