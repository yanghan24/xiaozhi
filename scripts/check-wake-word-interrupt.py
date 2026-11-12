"""Offline WebSocket regression using real routing, providers and audio queues.

Run in the voice dependency image on Ubuntu. Only model HTTP and Edge's
external transport are controlled; no production authority or credentials.
"""
import asyncio
import json
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import edge_tts
import opuslib_next
import websockets

sys.path.insert(0, '/app')
from config.config_loader import read_config
from core.websocket_server import WebSocketServer

release_old = threading.Event()
requests = []
model_bodies = []
asr_requests = []
old_finished = threading.Event()


class ModelHTTP(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        if self.path == '/asr':
            self.rfile.read(int(self.headers['Content-Length']))
            asr_requests.append(self.path)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({'text': '普通插话不应进入问答'}).encode())
            return
        body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        model_bodies.append(body)
        question = body['messages'][-1]['content']
        requests.append(question)
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.end_headers()

        def chunk(text):
            payload = {'choices': [{'delta': {'content': text}}]}
            self.wfile.write(('data: ' + json.dumps(payload) + '\n\n').encode())
            self.wfile.flush()

        try:
            chunk('这是旧回答。' if question == '开始长回答' else '这是新回答。')
            if question == '开始长回答':
                assert release_old.wait(20), 'Test did not release the old model stream'
                chunk('旧回答不应再播放。')
                old_finished.set()
            self.wfile.write(b'data: [DONE]\n\n')
        except (BrokenPipeError, ConnectionResetError):
            pass


mp3 = subprocess.run([
    'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'anullsrc=r=24000:cl=mono',
    '-t', '0.2', '-f', 'mp3', 'pipe:1',
], check=True, stdout=subprocess.PIPE).stdout


class Edge:
    def __init__(self, text, **kwargs):
        self.text = text

    async def stream(self):
        yield {'type': 'audio', 'data': mp3}


edge_tts.Communicate = Edge


async def send(socket, **message):
    await socket.send(json.dumps(message, ensure_ascii=False))


async def until(socket, predicate):
    messages = []
    async def receive():
        while True:
            data = await socket.recv()
            message = data if isinstance(data, bytes) else json.loads(data)
            messages.append(message)
            if predicate(message):
                return messages
    return await asyncio.wait_for(receive(), 15)


async def barrier(socket):
    # Numeric echo is handled by the actual protocol processor, after prior
    # messages. It provides ordering without inspecting connection internals.
    await socket.send('12345')
    return await until(socket, lambda m: m == 12345)


async def main():
    config = read_config('/app/config.yaml')
    config.update({
        'read_config_from_api': False, 'product_voice_required': False,
        'product-voice': {}, 'interrupt_mode': 'wake_word',
        'enable_greeting': True, 'enable_wakeup_words_response_cache': False,
        'prompt': 'Reply briefly.', 'context_providers': [],
    })
    config['server']['auth_key'] = 'offline-test'
    config['server']['auth'] = {'enabled': False}
    config['selected_module'].update({
        'ASR': 'TestASR', 'LLM': 'TestLLM', 'TTS': 'EdgeTTS',
        'Intent': 'nointent', 'Memory': 'nomem',
    })
    config['ASR']['TestASR'] = {
        'type': 'openai', 'api_key': 'synthetic', 'output_dir': 'tmp/',
        'base_url': 'http://127.0.0.1:18085/asr', 'model_name': 'test',
    }
    config['LLM']['TestLLM'] = {
        'type': 'openai', 'api_key': 'synthetic',
        'base_url': 'http://127.0.0.1:18085/v1', 'model_name': 'test',
    }
    model = ThreadingHTTPServer(('127.0.0.1', 18085), ModelHTTP)
    threading.Thread(target=model.serve_forever, daemon=True).start()
    server = WebSocketServer(config)
    try:
        async with websockets.serve(server._handle_connection, '127.0.0.1', 0) as ws:
            uri = 'ws://127.0.0.1:' + str(ws.sockets[0].getsockname()[1]) + '/xiaozhi/v1/'
            async with websockets.connect(uri, additional_headers={'Device-Id': 'offline-test'}) as socket:
                await send(socket, type='hello', features={'aec': True}, audio_params={
                    'format': 'opus', 'sample_rate': 24000, 'channels': 1, 'frame_duration': 60,
                })
                await until(socket, lambda m: isinstance(m, dict) and m.get('type') == 'hello')
                await send(socket, type='listen', state='detect', text='开始长回答')
                await until(socket, lambda m: isinstance(m, bytes))
                await send(socket, type='listen', state='detect', text='旁边的人在说话')
                observed = await barrier(socket)
                assert not any(isinstance(m, dict) and m.get('type') in ('tts', 'stt') for m in observed), observed
                assert requests == ['开始长回答'], requests
                print('PASS: ordinary detect does not stop or replace the current answer', flush=True)

                # Manual capture makes every PCM frame eligible for ASR without
                # depending on synthetic speech meeting Silero's threshold.
                await send(socket, type='listen', state='start', mode='manual')
                encoder = opuslib_next.Encoder(16000, 1, opuslib_next.APPLICATION_AUDIO)
                packet = encoder.encode(bytes(1920), 960)
                for _ in range(20):
                    await socket.send(packet)
                await asyncio.sleep(.3)
                await send(socket, type='listen', state='stop')
                observed = await barrier(socket)
                assert asr_requests == [], asr_requests
                assert not any(isinstance(m, dict) and m.get('type') in ('tts', 'stt') for m in observed), observed
                await send(socket, type='listen', state='start', mode='realtime')
                print('PASS: playback PCM never reaches the external ASR', flush=True)

                # A different configured wake word must not interrupt either.
                await send(socket, type='listen', state='detect', text='小爱同学')
                observed = await barrier(socket)
                assert not any(isinstance(m, dict) and m.get('type') in ('tts', 'stt') for m in observed), observed

                # Current firmware emits abort followed by listen.start, without
                # listen.detect when it wakes during playback.
                await send(socket, type='abort', reason='wake_word_detected')
                observed = await until(socket, lambda m: isinstance(m, dict) and m.get('state') == 'stop')
                await send(socket, type='listen', state='start', mode='auto')
                observed += await barrier(socket)
                assert not any(isinstance(m, dict) and m.get('state') == 'start' for m in observed), observed
                assert requests == ['开始长回答'], requests
                print('PASS: firmware wake abort stops without a greeting', flush=True)

                await send(socket, type='listen', state='detect', text='新问题')
                observed = await until(socket, lambda m: isinstance(m, dict) and m.get('state') == 'stop')
                assert any(isinstance(m, bytes) for m in observed), observed
                release_old.set()
                assert await asyncio.to_thread(old_finished.wait, 3)
                # Let the delayed old HTTP response reach real LLM/TTS workers.
                await asyncio.sleep(.5)
                observed = await barrier(socket)
                assert not any(isinstance(m, bytes) or (isinstance(m, dict) and m.get('type') in ('stt', 'tts')) for m in observed), observed
                assert requests == ['开始长回答', '新问题'], requests
                print('PASS: old answer never resumes after the new answer', flush=True)

                await send(socket, type='listen', state='detect', text='检查上下文')
                await until(socket, lambda m: isinstance(m, dict) and m.get('state') == 'stop')
                assert not any('旧回答不应再播放' in str(m.get('content', '')) for m in model_bodies[-1]['messages']), model_bodies[-1]
                print('PASS: cancelled model output does not reappear in conversation history', flush=True)

                # Protocols which send detect directly must work too.
                release_old.clear()
                old_finished.clear()
                await send(socket, type='listen', state='detect', text='开始长回答')
                await until(socket, lambda m: isinstance(m, bytes))
                await send(socket, type='listen', state='detect', text='你好，小智！')
                await until(socket, lambda m: isinstance(m, dict) and m.get('state') == 'stop')
                observed = await barrier(socket)
                assert not any(isinstance(m, dict) and m.get('state') == 'start' for m in observed), observed
                assert requests == ['开始长回答', '新问题', '检查上下文', '开始长回答'], requests
                release_old.set()
                print('PASS: exact wake detect cancels without greeting; normal post-answer questions work', flush=True)
    finally:
        release_old.set()
        model.shutdown()


asyncio.run(main())
