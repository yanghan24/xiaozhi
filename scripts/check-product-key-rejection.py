"""Real Java authority; substitute only the external market HTTP boundary."""
import asyncio
import json
import os
import sys
import threading
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
sys.path.insert(0, '/app')
from config.settings import check_config_file
check_config_file()
from core.product_voice import VoiceAuthority, VoiceDenied, MarketModel
from core.product_asr import AudioPublication, MarketASR, TranscriptionUnavailable

scenario = 'submit'
class Market(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def reply(self, status, body):
        self.send_response(status); self.end_headers()
        self.wfile.write(json.dumps(body).encode())
    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        if scenario in ('submit', 'model'):
            self.reply(401, {})
        else: self.reply(200, {'output': {'task_id': 'test'}})
    def do_GET(self):
        if scenario == 'poll' or self.path == '/result':
            self.reply(401, {})
        else:
            self.reply(200, {'output': {'task_status': 'SUCCEEDED', 'results': [
                {'subtask_status': 'SUCCEEDED', 'transcription_url': 'http://127.0.0.1:18085/result'}]}})

async def main():
    global scenario
    server = ThreadingHTTPServer(('127.0.0.1', 18085), Market)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    authority = VoiceAuthority('http://127.0.0.1:8002/xiaozhi/', 'isolated-voice-test-service-secret-123456')
    devices = json.loads(Path(os.environ['FIXTURE_FILE']).read_text())['devices']
    for index, scenario in enumerate(('submit', 'poll', 'model')):
        device = devices[index]
        headers = {'Device-Id': device['device_id'], 'Authorization': 'Bearer '+device['voice_token']}
        gate = await authority.open(headers, '/xiaozhi/v1/')
        other = await authority.open(headers, '/xiaozhi/v1/')
        asr = MarketASR(gate, AudioPublication('http://127.0.0.1:18086'))
        if scenario == 'poll':
            scenario = 'result'
            try: await asr.speech_to_text([b'\0'*38400], 'test')
            except TranscriptionUnavailable: pass
            else: raise AssertionError('Unsigned result failure accepted')
            await other.check_async()  # Result capability failure is not a business Key failure.
            scenario = 'poll'
        try:
            if scenario == 'model':
                await asyncio.to_thread(lambda: list(MarketModel(gate).response('test', [{'role':'user','content':'test'}])))
            else: await asr.speech_to_text([b'\0'*38400], 'test')
        except VoiceDenied: pass
        else: raise AssertionError('401 did not propagate: '+scenario)
        try: await other.check_async()
        except VoiceDenied: pass
        else: raise AssertionError('Other connection survived rejected Key')
        assert not asr.publication.files
        await gate.close(); await other.close()
    server.shutdown()
    print('PASS: model/ASR submit/ASR poll 401 revoke other connections; unsigned result 401 preserves grant')

asyncio.run(main())
