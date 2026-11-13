"""Real authority and sockets; inject failures only at the HTTP delivery boundary."""
import asyncio
import json
import os
import subprocess
import sys
import threading
import time
import re
import uuid
import httpx
import sqlite3
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, '/app')
from config.settings import check_config_file
check_config_file()
from core.product_voice import VoiceAuthority, VoiceDenied, MarketModel, GuardedSocket
from core.product_asr import MarketASR, AudioPublication
from core.product_rejections import RejectionStoreUnavailable
import websockets

BASE = 'http://127.0.0.1:8002/xiaozhi/'
PROXY = 'http://127.0.0.1:18087/xiaozhi/'
SERVICE = 'isolated-voice-test-service-secret-123456'
FAULT = os.environ.get('REPORT_FAULT', '503')
scenario = 'submit'
recover = False
reports = 0
calls = 0

class Market(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def reply(self, status, body):
        self.send_response(status); self.end_headers(); self.wfile.write(json.dumps(body).encode())
    def do_POST(self):
        global calls
        calls += 1
        self.rfile.read(int(self.headers['Content-Length']))
        self.reply(401, {})
    def do_GET(self):
        global calls
        calls += 1
        self.reply(401, {})

class Proxy(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_POST(self):
        global reports
        raw = self.rfile.read(int(self.headers['Content-Length']))
        rejected = self.path.endswith('/key-rejected')
        broken = rejected and not recover
        if rejected: reports += 1
        if broken and FAULT != 'lost_ack':
            if FAULT == 'timeout': time.sleep(3)
            self.send_response(503); self.end_headers(); return
        req = urllib.request.Request('http://127.0.0.1:8002'+self.path, data=raw,
            headers={'Content-Type':'application/json', 'X-Voice-Service-Key':SERVICE})
        try:
            with urllib.request.urlopen(req, timeout=5) as result:
                status, body = result.status, result.read()
        except urllib.error.HTTPError as error:
            status, body = error.code, error.read()
        try:
            self.send_response(503 if broken else status); self.end_headers(); self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError): pass

def direct(action, body):
    req = urllib.request.Request(BASE+'internal/device-voice/v1/'+action, data=json.dumps(body).encode(),
        headers={'Content-Type':'application/json','X-Voice-Service-Key':SERVICE})
    try:
        with urllib.request.urlopen(req, timeout=5) as result: return result.status, json.load(result)
    except urllib.error.HTTPError as error: return error.code, None

def headers(device):
    return {'Device-Id':device['device_id'],'Authorization':'Bearer '+device['voice_token']}

def login(device, confirm=False):
    device_headers={'Device-Id':device['device_id'],'Authorization':'Bearer '+device['credential'],'X-Forwarded-Proto':'https','Idempotency-Key':str(uuid.uuid4())}
    with httpx.Client(timeout=10,trust_env=False) as client:
        state=client.get(BASE+'device-control/v1/state',headers=device_headers).json()['data']
        response=client.post(BASE+'device-control/v1/requests',headers=device_headers,json={'intent':'account' if state['use_permission']=='allowed' else 'auto','expected_permission_version':state['permission_version']})
        assert response.status_code==200, (response.status_code,response.json().get('code'))
        request=response.json()['data']['binding_request']
        handle=request['verification_uri'].rsplit('/',1)[-1]
        response=client.get(BASE+'bind/'+handle)
        page=response.text
        csrf=re.search('name="csrf" value="([^"]+)"',page)[1]
        browser_headers={'Origin':'https://bridge.hncig.cn:18443','X-CSRF-Token':csrf,'Cookie':response.headers['Set-Cookie'].split(';')[0]}
        flow=client.post(BASE+'customer/v1/binding-flows',headers=browser_headers,json={'request_handle':handle}).json()['data']
        auth_page=client.get(flow['authorization_url']).text
        ticket=re.search('name="ticket" value="([^"]+)"',auth_page)[1]
        market_origin=flow['authorization_url'].split('/auth')[0]
        approved=client.get(market_origin+'/approve',params={'ticket':ticket,'decision':'allow','test_subject':device['subject']})
        callback=approved.headers['Location'].replace('https://bridge.hncig.cn:18443/xiaozhi/',BASE)
        assert client.get(callback,headers=browser_headers).status_code in (302,303)
        if confirm:
            browser_headers['Idempotency-Key']=str(uuid.uuid4())
            response=client.post(BASE+'customer/v1/binding-flows/'+flow['flow_id']+'/confirm',headers=browser_headers,json={})
            assert response.status_code==200, 'Explicit resume failed'
            return client.get(BASE+'device-control/v1/state',headers=device_headers).json()['data']['voice']['token']

async def probe_restart(device):
    # A fresh interpreter has no quarantine memory from the rejecting process.
    authority = VoiceAuthority(PROXY, SERVICE)
    try: await authority.open(headers(device), '/xiaozhi/v1/')
    except VoiceDenied: return
    raise AssertionError('Restart lost the quarantine')

async def main():
    global scenario, recover
    fixture = json.loads(Path(os.environ['FIXTURE_FILE']).read_text())
    fixtures, peers = fixture['devices'], fixture['peers']
    if len(sys.argv)>1:
        if sys.argv[1]=='storage-fault':
            try: VoiceAuthority(PROXY,SERVICE)
            except RejectionStoreUnavailable: return
            raise AssertionError('Restart erased storage fault')
        await probe_restart(fixtures[int(sys.argv[1])]); return
    market, proxy = ThreadingHTTPServer(('127.0.0.1',18085), Market), ThreadingHTTPServer(('127.0.0.1',18087), Proxy)
    for server in (market,proxy): threading.Thread(target=server.serve_forever,daemon=True).start()
    authority = VoiceAuthority(PROXY, SERVICE)
    # Independent authority objects still share the durable quarantine.
    peer_authority = VoiceAuthority(PROXY, SERVICE)
    try: VoiceAuthority(BASE, SERVICE)
    except RejectionStoreUnavailable: pass
    else: raise AssertionError('Authority URL change silently created empty rejection state')
    for index, scenario in enumerate(('handshake','second-handshake','model')):
        recover = False
        device = fixtures[index]
        gate = await authority.open(headers(device), '/xiaozhi/v1/')
        if index==0:
            first_access=await asyncio.to_thread(gate.model_access)
            first_gate=gate
        peer = await peer_authority.open(headers(peers[index]), '/xiaozhi/v1/')
        before, calls_before = reports, calls
        asr = MarketASR(gate, AudioPublication('http://127.0.0.1:18086'))
        try:
            if scenario == 'model':
                await asyncio.to_thread(lambda: list(MarketModel(gate).response('test',[{'role':'user','content':'test'}])))
            else: await asr.speech_to_text([b'\0'*38400], 'test')
        except VoiceDenied: pass
        else: raise AssertionError('401 accepted')
        async def output(socket):
            try: await GuardedSocket(socket,peer).send('must-not-arrive')
            except VoiceDenied: await socket.close(code=1008)
        async with websockets.serve(output,'127.0.0.1',0) as ws:
            async with websockets.connect('ws://127.0.0.1:'+str(ws.sockets[0].getsockname()[1])) as receiver:
                try: await asyncio.wait_for(receiver.recv(),3)
                except websockets.exceptions.ConnectionClosed: pass
                else: raise AssertionError('Peer output escaped quarantine')
        assert not asr.publication.files
        await gate.close(); await peer.close()
        # No live originating session is needed for durable retry.
        result = await asyncio.to_thread(subprocess.run,[sys.executable,__file__,str(index)],capture_output=True,timeout=15)
        assert result.returncode==0, 'Fresh process accepted quarantined grant'
        if index==0:
            await asyncio.to_thread(login,device)
            try: await peer_authority.open(headers(peers[index]),'/xiaozhi/v1/')
            except VoiceDenied: pass
            else: raise AssertionError('Relogin bypassed pending rejection for another device')
        if index<2:
            healthy = await peer_authority.open(headers(fixtures[index+1]),'/xiaozhi/v1/')
            await healthy.check_async(); await healthy.close()
        deadline=time.monotonic()+8
        while reports==before and time.monotonic()<deadline: await asyncio.sleep(.05)
        assert reports>before, 'No delivery attempt'
        # Query authority directly: restoration must eventually commit shared revocation.
        recover=True
        deadline=time.monotonic()+40
        while time.monotonic()<deadline:
            status,opened=await asyncio.to_thread(direct,'open',{'device_id':device['device_id'],'token':device['voice_token']})
            if status==403: break
            assert status==200
            await asyncio.to_thread(direct,'close',{'session':opened['session']})
            await asyncio.sleep(.2)
        assert status==403, 'Recovery did not deliver durable rejection'
        if index==0:
            new_token=await asyncio.to_thread(login,device,True)
            restored={**device,'voice_token':new_token}
            restored_gate=await authority.open(headers(restored),'/xiaozhi/v1/')
            await restored_gate.check_async(); await restored_gate.close()
            status,_=await asyncio.to_thread(direct,'open',{'device_id':peers[index]['device_id'],'token':peers[index]['voice_token']})
            assert status==403, 'Explicit target resume restored its peer'
        assert calls==calls_before+1, 'Delivery retry repeated a paid resource request'
    # Inject storage saturation, not an application collaborator replacement.
    state_file=next(Path(os.environ.get('PRODUCT_VOICE_STATE_DIR','data/product-voice')).glob('*.sqlite3'))
    deadline=time.monotonic()+40
    while time.monotonic()<deadline:
        with sqlite3.connect(state_file,timeout=5) as db:
            pending=db.execute('SELECT COUNT(*) FROM rejections WHERE pending=1').fetchone()[0]
        if pending==0: break
        await asyncio.sleep(.1)
    assert pending==0,'Acknowledgement loss did not recover to confirmed delivery'
    with sqlite3.connect(state_file,timeout=5) as db:
        db.execute('BEGIN IMMEDIATE')
        pending=db.execute('SELECT COUNT(*) FROM rejections WHERE pending=1').fetchone()[0]
        for i in range(1024-pending):
            db.execute('INSERT INTO rejections VALUES (?,?,1,0,?,NULL,?,0)',('padding-'+str(i),'fixture-receipt',time.time()+86400,'0'*64))
    try: first_gate.key_rejected(first_access)
    except VoiceDenied: pass
    healthy=await authority.open(headers(restored),'/xiaozhi/v1/')
    await healthy.check_async()  # A duplicate at capacity must not disable unrelated/new grants.
    try: await asyncio.to_thread(lambda: list(MarketModel(healthy).response('test',[{'role':'user','content':'capacity'}])))
    except VoiceDenied: pass
    else: raise AssertionError('New rejection at capacity was allowed')
    result=await asyncio.to_thread(subprocess.run,[sys.executable,__file__,'storage-fault'],capture_output=True,timeout=15)
    assert result.returncode==0,'Capacity failure was forgotten after restart'
    for server in (market,proxy): server.shutdown()
    print(json.dumps({'result':'pass','fault':FAULT,'checks':['peer output blocked before delivery','fresh process stays quarantined','unrelated account allowed','closed source session does not lose delivery','relogin cannot bypass old permission quarantine','explicit resume only restores target','recovery commits rejection','no repeated resource request','duplicate at capacity is harmless','capacity fault survives restart']}))

asyncio.run(main())
