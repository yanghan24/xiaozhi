import ast,asyncio,json
from pathlib import Path
from types import SimpleNamespace as N
from unittest.mock import AsyncMock,Mock

def load(path,name,ns):
 node=next(n for n in ast.parse(Path(path).read_text()).body if getattr(n,'name',None)==name)
 if isinstance(node,ast.ClassDef):node.bases=[]
 exec(compile(ast.Module(body=[node],type_ignores=[]),path,'exec'),ns)
 return ns[name]
async def main():
 gate=N(check_async=AsyncMock());conn=N(config={'interrupt_mode':'wake_word'},client_is_speaking=True,product_voice_gate=gate)
 f=load('/app/core/handle/receiveAudioHandle.py','handleAudioMessage',{})
 await f(conn,b'ordinary speech');gate.check_async.assert_awaited_once()
 f=load('/app/core/handle/receiveAudioHandle.py','startToChat',{})
 await f(conn,'late ordinary ASR result')
 calls=[]
 async def abort(c):calls.append('abort');c.client_is_speaking=False
 async def chat(c,t):calls.append('chat')
 ns={'Dict':dict,'Any':object,'handleAbortMessage':abort,'startToChat':chat,'time':__import__('time'),'remove_punctuation_and_length':lambda t:(len(t),t),'enqueue_asr_report':lambda *a:None,'send_stt_message':AsyncMock(),'send_tts_message':AsyncMock(),'TextMessageType':N(LISTEN='listen')}
 cls=load('/app/core/handle/textHandler/listenMessageHandler.py','ListenTextMessageHandler',ns)
 for greeting in [False,True]:
  c=N(config={'interrupt_mode':'wake_word','wakeup_words':['你好小智'],'enable_greeting':greeting},client_is_speaking=True,reset_audio_states=Mock())
  calls.clear();await cls().handle(c,{'state':'detect','text':'普通插话'});assert calls==[] and c.client_is_speaking
  await cls().handle(c,{'state':'detect','text':'你好小智'});assert calls[0]=='abort' and not c.client_is_speaking
  assert ('chat' in calls)==greeting
 # Normal idle speech still enters chat.
 c=N(config={'interrupt_mode':'wake_word','wakeup_words':['你好小智']},client_is_speaking=False,reset_audio_states=Mock())
 calls.clear();await cls().handle(c,{'state':'detect','text':'今天星期几'});assert calls==['chat']
 # Existing explicit device abort remains available (wake-word/button/stop).
 ns={'Dict':dict,'Any':object,'handleAbortMessage':abort,'TextMessageType':N(ABORT='abort')}
 cls=load('/app/core/handle/textHandler/abortMessageHandler.py','AbortTextMessageHandler',ns)
 calls.clear();await cls().handle(c,{'type':'abort','reason':'wake_word'});assert calls==['abort']
 print('PASS: ordinary audio and late ASR suppressed; ordinary detect ignored; wake word stops with greeting on/off; idle speech and explicit abort preserved')
asyncio.run(main())