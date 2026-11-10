"""HTTP/WebSocket integration: only external market and Edge transport are controlled."""
import asyncio,io,json,os,sys,threading,time,urllib.request,urllib.error,wave,subprocess
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
sys.path.insert(0,'/app')
from config.settings import check_config_file
check_config_file()
from config.config_loader import load_config
from core.websocket_server import WebSocketServer
from core.product_asr import TranscriptionUnavailable
import websockets,opuslib_next,edge_tts
BASE='http://127.0.0.1:8002/xiaozhi/'
SERVICE='isolated-voice-test-service-secret-123456'
asr_urls=[];models=[];synthesis=[];asr_started=threading.Event()
scenario='success'
class Market(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def reply(self,body):
        self.send_response(200);self.end_headers();self.wfile.write(json.dumps(body,ensure_ascii=False).encode())
    def do_POST(self):
        assert self.headers.get('Authorization')=='Bearer test-business-key'
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        if self.path=='/v1/services/audio/asr/transcription':
            assert body['model']=='qwen-audio-3.0-asr-flash-filetrans'
            assert self.headers['X-DashScope-Async']=='enable'
            url=body['input']['file_urls'][0]
            with urllib.request.urlopen(url,timeout=5) as r:
                assert r.headers['Content-Type']=='audio/wav'
                with wave.open(io.BytesIO(r.read())) as wav:
                    assert wav.getframerate()==16000 and wav.getnchannels()==1 and wav.getnframes()>1000
                    if not asr_urls:assert wav.getnframes()==19200,'listen.stop lost queued audio'
            asr_urls.append(url);asr_started.set()
            self.reply({'output':{'task_id':'task-'+str(len(asr_urls)),'task_status':'PENDING'}})
        elif self.path=='/v1/chat/completions':
            models.append(body)
            self.send_response(200);self.send_header('Content-Type','text/event-stream');self.end_headers()
            self.wfile.write(b'data: {"choices":[{"delta":{"content":"OK."}}]}\n\ndata: [DONE]\n\n')
        else:self.send_error(404)
    def do_GET(self):
        if self.path.startswith('/v1/tasks/'):
            assert self.headers.get('Authorization')=='Bearer test-business-key'
            output={'task_status':'RUNNING'} if scenario=='pending' else {'task_status':'SUCCEEDED','results':[{'subtask_status':'FAILED' if scenario=='failed' else 'SUCCEEDED','transcription_url':'http://127.0.0.1:18085/result'}]}
            self.reply({'output':output})
        elif self.path=='/result':
            assert self.headers.get('Authorization') is None
            if scenario=='redirect':
                self.send_response(302);self.send_header('Location','http://127.0.0.1:18085/forbidden');self.end_headers()
            elif scenario=='slow':
                self.send_response(200);self.end_headers()
                try:
                    for _ in range(30):
                        self.wfile.write(b' ');self.wfile.flush();time.sleep(.3)
                except (BrokenPipeError,ConnectionResetError):pass
            else:self.reply({'transcripts':[] if scenario=='empty' else [{'channel_id':0,'text':'音频识别完成，请回答。'}]})
        else:self.send_error(404)
mp3=subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','anullsrc=r=24000:cl=mono','-t','2','-f','mp3','pipe:1'],check=True,stdout=subprocess.PIPE).stdout
class Edge:
    def __init__(self,text,**kwargs):synthesis.append(text)
    async def stream(self):yield {'type':'audio','data':mp3}
edge_tts.Communicate=Edge

def sign_out(d):
    req=urllib.request.Request(BASE+'customer/v1/device-sessions/'+d['device_session_id']+'/sign-out',data=b'{}',headers={'Cookie':d['cookie'],'Origin':'https://bridge.hncig.cn:18443','X-CSRF-Token':d['csrf'],'Idempotency-Key':'asr-stop','Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=5) as r:assert json.load(r)['code']=='OK'
def unavailable(url):
    try:urllib.request.urlopen(url,timeout=3)
    except urllib.error.HTTPError as e:return e.code==404
    return False
async def main():
    global scenario
    market=ThreadingHTTPServer(('127.0.0.1',18085),Market)
    threading.Thread(target=market.serve_forever,daemon=True).start()
    d=json.loads(Path(os.environ['FIXTURE_FILE']).read_text())['devices'][0]
    headers={'Device-Id':d['device_id'],'Client-Id':d['device_id'],'Authorization':'Bearer '+d['voice_token']}
    config=await load_config()
    config['server']['auth_key']='legacy'
    config['product_voice_required']=True
    config['product-voice']={'authority_url':BASE,'service_key':SERVICE,'asr_audio_base_url':'http://127.0.0.1:18086'}
    config['prompt']='Reply briefly.'
    config['selected_module']['ASR']='TestRemoteASR'
    config['ASR']={'TestRemoteASR':{'type':'openai','api_key':'synthetic','base_url':'http://127.0.0.1:18085/asr','model_name':'test','output_dir':'tmp/'}}
    server=WebSocketServer(config)
    async with websockets.serve(server._handle_connection,'127.0.0.1',18086,process_request=server._http_response) as ws:
        uri='ws://127.0.0.1:18086/xiaozhi/v1/'
        async with websockets.connect(uri,additional_headers=headers) as socket:
            await socket.send(json.dumps({'type':'hello','version':1,'audio_params':{'format':'opus','sample_rate':24000,'channels':1,'frame_duration':60}}))
            hello=json.loads(await asyncio.wait_for(socket.recv(),15))
            def ready():
                req=urllib.request.Request(uri.replace('ws:','http:')+'ready/'+hello['session_id'],headers=headers)
                with urllib.request.urlopen(req,timeout=3) as r:return json.load(r)
            for _ in range(150):
                status=await asyncio.to_thread(ready)
                if status['status']!='pending':break
                await asyncio.sleep(.2)
            assert status['status']=='ready',status
            encoder=opuslib_next.Encoder(16000,1,opuslib_next.APPLICATION_AUDIO)
            async def send_audio(target=socket,mode='manual'):
                await target.send(json.dumps({'type':'listen','state':'start','mode':mode}))
                for _ in range(20):
                    await target.send(encoder.encode(b'\0'*1920,960))
                await target.send(json.dumps({'type':'listen','state':'stop'}))
            await send_audio()
            heard=False
            for _ in range(40):
                message=await asyncio.wait_for(socket.recv(),12)
                if isinstance(message,bytes):heard=True
                elif json.loads(message).get('state')=='stop':break
            assert heard and len(asr_urls)==1 and len(models)==1,'No ASR-to-audio round trip'
            assert models[0]['messages'][-1]['content']=='音频识别完成，请回答。'
            assert await asyncio.to_thread(unavailable,asr_urls[0]),'Completed audio URL still readable'
            handler=server.sessions[hello['session_id']]
            provider=handler.asr
            for scenario in ('failed','empty','redirect','slow','pending'):
                provider.TIMEOUT=.3 if scenario=='pending' else 10
                before=len(asr_urls)
                try:await provider.speech_to_text([b'\0'*38400],'regression')
                except TranscriptionUnavailable:pass
                else:raise AssertionError('ASR failure accepted: '+scenario)
                assert len(asr_urls)==before+1,'ASR submission retried'
                assert await asyncio.to_thread(unavailable,asr_urls[-1])
                assert len(models)==1,'Failed ASR called model'
            provider.TIMEOUT=60
            store=server.product_audio
            store.TTL=.05
            token,url=store.publish(b'\0'*38400,handler.product_voice_gate)
            await asyncio.sleep(.1)
            assert await asyncio.to_thread(unavailable,url),'Expired audio readable'
            store.TTL=90
            try:store.publish(b'\0'*(store.MAX_PCM+2),handler.product_voice_gate)
            except TranscriptionUnavailable:pass
            else:raise AssertionError('Oversized audio accepted')
            scenario='success'
            await send_audio()
            while not isinstance(await asyncio.wait_for(socket.recv(),5),bytes):pass
            await send_audio(mode='auto')
            deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                response=await asyncio.wait_for(socket.recv(),8)
                if len(models)==3 and isinstance(response,str) and json.loads(response).get('state')=='stop':break
            assert len(models)==3,'Playback interruption cancelled the new ASR utterance'
            assert models[-1]['messages'][-1]['content']=='音频识别完成，请回答。'
            baseline_models,baseline_synthesis=len(models),len(synthesis)
            scenario='pending'
            async with websockets.connect(uri,additional_headers=headers) as other:
                await other.send(json.dumps({'type':'hello','version':1,'audio_params':{'format':'opus','sample_rate':24000,'channels':1,'frame_duration':60}}))
                other_hello=json.loads(await asyncio.wait_for(other.recv(),15))
                def other_ready():
                    req=urllib.request.Request(uri.replace('ws:','http:')+'ready/'+other_hello['session_id'],headers=headers)
                    with urllib.request.urlopen(req,timeout=3) as r:return json.load(r)['status']
                for _ in range(150):
                    if await asyncio.to_thread(other_ready)=='ready':break
                    await asyncio.sleep(.1)
                asr_started.clear();await send_audio(other)
                assert await asyncio.to_thread(asr_started.wait,10)
                disconnected_url=asr_urls[-1]
                for _ in range(110):
                    await other.send(encoder.encode(b'\0'*1920,960))
                    await asyncio.sleep(.06)
                assert await asyncio.to_thread(other_ready)=='ready','Continuous input disconnected pending ASR'
                await other.send(json.dumps({'type':'abort'}))
                while True:
                    response=await asyncio.wait_for(other.recv(),3)
                    if isinstance(response,str) and json.loads(response).get('state')=='stop':break
                assert await asyncio.to_thread(unavailable,disconnected_url),'Aborted audio readable'
                asr_started.clear();await send_audio(other)
                assert await asyncio.to_thread(asr_started.wait,10)
                disconnected_url=asr_urls[-1]
                await other.close()
                await asyncio.sleep(.3)
                assert await asyncio.to_thread(unavailable,disconnected_url),'Disconnected audio readable'
            asr_started.clear();await send_audio()
            assert await asyncio.to_thread(asr_started.wait,10)
            await asyncio.to_thread(sign_out,d)
            try:
                while True:
                    message=await asyncio.wait_for(socket.recv(),4)
                    assert not isinstance(message,bytes),'Late audio after signout'
            except websockets.exceptions.ConnectionClosed:pass
            assert await asyncio.to_thread(unavailable,asr_urls[-1]),'Revoked audio readable'
            await asyncio.sleep(1.2)
            assert len(models)==baseline_models and len(synthesis)==baseline_synthesis,'Pending ASR escaped revocation'
    market.shutdown()
    print(json.dumps({'result':'pass','checks':['device Opus -> asynchronous ASR -> model -> TTS audio','temporary WAV fetched with correct format','completed and revoked URLs unavailable','pending ASR cannot call model/TTS after signout']}))
asyncio.run(main())
