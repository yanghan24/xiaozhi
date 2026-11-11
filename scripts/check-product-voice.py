#!/usr/bin/env python3
"""Real HTTP authority + WebSocket gate regression; fixtures never leave the test host."""
import asyncio
import json
import os
import sys
from pathlib import Path
import urllib.request
import websockets
sys.path.insert(0, '/app')
from core.product_voice import VoiceAuthority, GuardedSocket, VoiceDenied

BASE='http://127.0.0.1:8002/xiaozhi/'
SERVICE='isolated-voice-test-service-secret-123456'

def sign_out(device):
    request=urllib.request.Request(BASE+'customer/v1/device-sessions/'+device['device_session_id']+'/sign-out',data=b'{}',headers={
        'Cookie':device['cookie'],'Origin':'https://bridge.hncig.cn:18443','X-CSRF-Token':device['csrf'],
        'Idempotency-Key':'python-stop','Content-Type':'application/json'})
    with urllib.request.urlopen(request,timeout=5) as response: assert json.load(response)['code']=='OK'

async def main():
    fixtures=json.loads(Path(os.environ['FIXTURE_FILE']).read_text())['devices']
    authority=VoiceAuthority(BASE,SERVICE)
    async def handle(socket):
        try:
            gate=await authority.open(dict(socket.request.headers),socket.request.path)
        except VoiceDenied:
            await socket.close(code=1008);return
        guarded=GuardedSocket(socket,gate)
        try:
            async for message in guarded:
                if message=='late':
                    await asyncio.sleep(.5)
                await guarded.send('accepted:'+message)
        except VoiceDenied:
            # A send may deny first. The watcher must still close an already-ended gate.
            await gate.watch(socket, lambda: None)
        finally: await gate.close()
    async with websockets.serve(handle,'127.0.0.1',0) as server:
        uri='ws://127.0.0.1:'+str(server.sockets[0].getsockname()[1])+'/xiaozhi/v1/'
        def headers(d):return {'Device-Id':d['device_id'],'Authorization':'Bearer '+d['voice_token']}
        async with websockets.connect(uri,additional_headers=headers(fixtures[0])) as a, websockets.connect(uri,additional_headers=headers(fixtures[1])) as b:
            await a.send('hello');assert await a.recv()=='accepted:hello'
            await a.send('late');await asyncio.to_thread(sign_out,fixtures[0])
            try: await asyncio.wait_for(a.recv(),3)
            except websockets.exceptions.ConnectionClosed: pass
            else: raise AssertionError('Late output escaped after sign-out')
            await b.send('still-allowed');assert await b.recv()=='accepted:still-allowed'
        for candidate in [headers(fixtures[0]),{'Device-Id':fixtures[1]['device_id'],'Authorization':'Bearer legacy.signature'},
                          {'Device-Id':fixtures[2]['device_id'],'Authorization':'Bearer '+fixtures[1]['voice_token']}]:
            async with websockets.connect(uri,additional_headers=candidate) as denied:
                try: await asyncio.wait_for(denied.recv(),3)
                except websockets.exceptions.ConnectionClosed: pass
                else: raise AssertionError('Invalid identity accepted')
        async with websockets.connect(uri+'?device-id='+fixtures[1]['device_id']+'&authorization='+fixtures[1]['voice_token']) as denied:
            try: await asyncio.wait_for(denied.recv(),3)
            except websockets.exceptions.ConnectionClosed: pass
            else: raise AssertionError('Query fallback accepted')
    print(json.dumps({'result':'pass','checks':['real authority handshake','late output blocked','other device unaffected','old and cross-device tokens rejected','query fallback rejected']}))

async def production():
    # Load actual production modules; only the external model HTTP service is substituted.
    import threading
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
    from core.product_voice import MarketModel, ModelUnavailable
    fixtures=json.loads(Path(os.environ['FIXTURE_FILE']).read_text())['devices']
    seen=[]
    class ModelHTTP(BaseHTTPRequestHandler):
        def log_message(self,*args): pass
        def do_POST(self):
            body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            assert self.path=='/v1/chat/completions'
            assert body['model']=='qwen3.8-flash'
            assert self.headers.get('Authorization','').startswith('Bearer test-business-key')
            seen.append(body)
            if body['messages'][-1]['content']=='balance':
                self.send_response(400);self.end_headers();self.wfile.write(b'{"error":{"message":"insufficient points"}}');return
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.end_headers()
            self.wfile.write(b'data: {"choices":[{"delta":{"reasoning_content":"hidden"}}]}\n\n')
            self.wfile.write(b'data: {"choices":[{"delta":{"content":"OK."}}]}\n\ndata: [DONE]\n\n')
    model_http=ThreadingHTTPServer(('127.0.0.1',18085),ModelHTTP)
    threading.Thread(target=model_http.serve_forever,daemon=True).start()
    authority=VoiceAuthority(BASE,SERVICE)
    def headers(d):return {'Device-Id':d['device_id'],'Client-Id':d['device_id'],'Authorization':'Bearer '+d['voice_token']}
    gate=await authority.open(headers(fixtures[2]),'/xiaozhi/v1/')
    model=MarketModel(gate)
    def invoke(text):return ''.join(model.response('test',[{'role':'user','content':text}]))
    assert await asyncio.to_thread(invoke,'hello')=='OK.'
    try: await asyncio.to_thread(invoke,'balance')
    except ModelUnavailable: pass
    else: raise AssertionError('Balance failure accepted')
    await gate.check_async()
    assert await asyncio.to_thread(invoke,'hello again')=='OK.'

    from config.config_loader import load_config
    config=await load_config()
    config['server']['auth_key']='isolated-legacy-secret'
    config['server']['auth']={'enabled':False,'allowed_devices':[fixtures[0]['device_id'],fixtures[2]['device_id']]}
    config['product_voice_required']=True
    config['product-voice']={'authority_url':BASE,'service_key':SERVICE,'asr_audio_base_url':'http://127.0.0.1:18086'}
    config['prompt']='Reply briefly.'
    config['context_providers']=[]
    config['selected_module']['ASR']='TestRemoteASR'
    config['ASR']={'TestRemoteASR':{'type':'openai','api_key':'synthetic','base_url':'http://127.0.0.1:18085/asr','model_name':'test-asr','output_dir':'tmp/'}}
    from core.websocket_server import WebSocketServer
    # Substitute only Edge's external transport; use real TTS adapter, queues and codecs.
    import edge_tts
    import subprocess
    mp3=subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','anullsrc=r=24000:cl=mono','-t','0.2','-f','mp3','pipe:1'],check=True,stdout=subprocess.PIPE).stdout
    synthesis=[]
    synthesis_started=threading.Event()
    class ControlledEdge:
        def __init__(self,text,**kwargs):
            assert kwargs['voice']=='zh-CN-XiaoxiaoNeural'
            synthesis.append(text)
        async def stream(self):
            if len(synthesis)>1:
                synthesis_started.set()
                await asyncio.sleep(.8)
            yield {'type':'audio','data':mp3}
    edge_tts.Communicate=ControlledEdge
    server=WebSocketServer(config)
    async with websockets.serve(server._handle_connection,'127.0.0.1',0,process_request=server._http_response) as ws:
        uri='ws://127.0.0.1:'+str(ws.sockets[0].getsockname()[1])+'/xiaozhi/v1/'
        async with websockets.connect(uri,additional_headers=headers(fixtures[2])) as a, websockets.connect(uri,additional_headers=headers(fixtures[2])) as second:
            for socket in (a,second):
                await socket.send(json.dumps({'type':'hello','version':1,'transport':'websocket','audio_params':{'format':'opus','sample_rate':24000,'channels':1,'frame_duration':60}}))
                hello=json.loads(await asyncio.wait_for(socket.recv(),15));assert hello['type']=='hello'
            # Wait for the actual TTS/ASR initialization, observable through readiness HTTP.
            def readiness():
                request=urllib.request.Request(uri.replace('ws:','http:')+'ready/'+hello['session_id'],headers=headers(fixtures[2]))
                with urllib.request.urlopen(request,timeout=3) as response:return json.load(response)
            for attempt in range(150):
                ready=await asyncio.to_thread(readiness)
                if ready['status']!='pending':break
                await asyncio.sleep(.2)
            assert ready['status']=='ready',ready
            await a.send(json.dumps({'type':'listen','state':'detect','text':'A short answer please'}))
            heard_audio=False
            for attempt in range(40):
                message=await asyncio.wait_for(a.recv(),15)
                if isinstance(message,bytes):heard_audio=True
                elif json.loads(message).get('state')=='stop':break
            assert heard_audio,'Direct model answer did not reach real TTS/audio output'
            await a.send(json.dumps({'type':'listen','state':'detect','text':'Another answer please'}))
            assert await asyncio.to_thread(synthesis_started.wait,10),'Second TTS call did not start'
            await asyncio.to_thread(sign_out,fixtures[2])
            for socket in (a,second):
                try:
                    while True:
                        message=await asyncio.wait_for(socket.recv(),4)
                        assert not isinstance(message,bytes),'Late TTS audio escaped sign-out'
                except websockets.exceptions.ConnectionClosed:pass
            await asyncio.sleep(1)
            assert len(synthesis)==2,'New TTS call started after revocation'
        for candidate in [headers(fixtures[2]),{'Device-Id':fixtures[2]['device_id'],'Authorization':'Bearer old.signature'}]:
            async with websockets.connect(uri,additional_headers=candidate) as denied:
                try: await asyncio.wait_for(denied.recv(),3)
                except websockets.exceptions.ConnectionClosed:pass
                else:raise AssertionError('Production legacy bypass')
        async with websockets.connect(uri+'?device-id='+fixtures[2]['device_id']) as denied:
            try:await asyncio.wait_for(denied.recv(),3)
            except websockets.exceptions.ConnectionClosed:pass
            else:raise AssertionError('Production query bypass')
    count=len(seen)
    try:await asyncio.to_thread(invoke,'after signout')
    except VoiceDenied:pass
    else:raise AssertionError('Model call after signout')
    assert len(seen)==count
    await gate.close()
    model_http.shutdown()
    print(json.dumps({'result':'pass','checks':['actual WebSocketServer and ConnectionHandler','model response through Edge adapter to binary audio','late external TTS output blocked','multiple connections closed','legacy auth disabled and whitelist cannot bypass','direct qwen SSE content only','balance failure preserves grant','signout blocks new model requests']}))

asyncio.run(main())
from config.settings import check_config_file
check_config_file()
asyncio.run(production())
