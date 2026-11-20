"""Exercise the Edge provider with real MP3/Opus and controlled network chunks."""
import asyncio
import gc
import subprocess
import threading
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import opuslib_next
from core.product_chat import ProductConversation
from core.product_voice import VoiceGate
from core.providers.tts.edge import TTSProvider
from core.utils.opus_encoder_utils import OpusEncoderUtils
from core.utils.util import audio_bytes_to_data_stream


class EdgeStreamingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mp3 = subprocess.run([
            'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
            'sine=frequency=440:sample_rate=24000', '-t', '4', '-ac', '1',
            '-b:a', '96k', '-f', 'mp3', 'pipe:1'], check=True, stdout=subprocess.PIPE).stdout

    def provider(self):
        class Authority:
            allowed = True
            before_check = None

            def check_scope(self, grant):
                pass

            def request(self, action, payload):
                if self.before_check:
                    self.before_check()
                return {'allowed': self.allowed, 'permission_version': 1}

        conn = SimpleNamespace(sentence_id='a' * 32, client_abort=False,
                               stop_event=threading.Event(), sample_rate=24000,
                               audio_format='opus',
                               product_voice_gate=VoiceGate(Authority(), 'test-session', 1, {}))
        conn.product_conversation = ProductConversation(conn)
        conn.product_turn_is_current = conn.product_conversation.current
        provider = TTSProvider({'voice': 'zh-CN-XiaoxiaoNeural'}, delete_audio_file=True)
        provider.conn = conn
        provider.current_sentence_id = conn.sentence_id
        provider.opus_encoder = OpusEncoderUtils(24000, 1, 60)
        return provider

    def test_authority_revocation_blocks_external_synthesis(self):
        provider = self.provider()
        gate = provider.conn.product_voice_gate
        gate.authority.allowed = False
        self.assertFalse(gate.ended.is_set(), 'Periodic watcher has not observed revocation yet')
        packets = []
        with patch('core.providers.tts.edge.edge_tts.Communicate',
                   side_effect=RuntimeError('Unexpected external synthesis')) as network:
            provider.to_tts_stream('固定测试文本。', packets.append)
        network.assert_not_called()
        self.assertEqual([], packets)
        self.assertTrue(gate.ended.is_set())

    def test_turn_cancelled_during_authority_check_does_not_start_synthesis(self):
        provider = self.provider()
        checked = threading.Event()

        def cancel():
            provider.conn.client_abort = True
            checked.set()

        provider.conn.product_voice_gate.authority.before_check = cancel
        packets = []
        with patch('core.providers.tts.edge.edge_tts.Communicate',
                   side_effect=RuntimeError('Unexpected external synthesis')) as network:
            provider.to_tts_stream('固定测试文本。', packets.append)
        self.assertTrue(checked.is_set())
        network.assert_not_called()
        self.assertEqual([], packets)

    def test_audio_is_decodable_before_network_finishes(self):
        provider = self.provider()
        release = threading.Event()
        prefix_delivered = threading.Event()
        first_audio = threading.Event()
        packets = []
        errors = []
        mp3 = self.mp3

        class Network:
            def __init__(self, text, **kwargs):
                pass

            async def stream(self):
                # Split across MP3 frames; none of these chunks is a file.
                for offset in range(0, 18000, 997):
                    yield {'type': 'audio', 'data': mp3[offset:min(offset + 997, 18000)]}
                prefix_delivered.set()
                while not release.is_set():
                    await asyncio.sleep(.01)
                yield {'type': 'audio', 'data': mp3[18000:]}

        def receive(packet):
            packets.append(packet)
            first_audio.set()

        def produce():
            try:
                provider.to_tts_stream('固定测试文本。', opus_handler=receive)
            except BaseException as error:
                errors.append(type(error).__name__)

        with patch('core.providers.tts.edge.edge_tts.Communicate', Network):
            worker = threading.Thread(target=produce, daemon=True)
            worker.start()
            try:
                self.assertTrue(prefix_delivered.wait(3), 'Network fixture never reached its barrier')
                self.assertTrue(first_audio.wait(2), 'First Opus frame waited for all MP3 bytes')
                decoder = opuslib_next.Decoder(24000, 1)
                self.assertTrue(decoder.decode(packets[0], 1440))
            finally:
                release.set()
                worker.join(5)
        self.assertFalse(worker.is_alive(), 'Synthesis leaked a worker')
        self.assertEqual([], errors)
        decoder = opuslib_next.Decoder(24000, 1)
        pcm = b''.join(decoder.decode(packet, 1440) for packet in packets)
        self.assertGreaterEqual(len(pcm), 4 * 24000 * 2)
        self.assertLess(len(pcm), int(4.3 * 24000 * 2))
        reference = []
        encoder = OpusEncoderUtils(24000, 1, 60)
        try:
            audio_bytes_to_data_stream(mp3, 'mp3', True, reference.append, 24000, encoder)
        finally:
            encoder.close()
        self.assertEqual(reference, packets, 'Streaming changed, reordered or duplicated decoded speech')

    def test_failure_after_first_audio_does_not_replay(self):
        provider = self.provider()
        heard = threading.Event()
        calls = []
        mp3 = self.mp3

        class Network:
            def __init__(self, text, **kwargs):
                calls.append(text)

            async def stream(self):
                yield {'type': 'audio', 'data': mp3[:18000]}
                while not heard.is_set():
                    await asyncio.sleep(.01)
                raise ConnectionError('synthetic transport failure')

        with patch('core.providers.tts.edge.edge_tts.Communicate', Network):
            provider.to_tts_stream('固定测试文本。', opus_handler=lambda _: heard.set())
        self.assertTrue(heard.is_set())
        self.assertEqual(1, len(calls), 'Partially played speech was requested again')

    def test_cancel_during_network_stall_stops_and_next_turn_is_clean(self):
        provider = self.provider()
        heard = threading.Event()
        stream_closed = threading.Event()
        old_packets = []
        mp3 = self.mp3

        class StalledNetwork:
            def __init__(self, text, **kwargs):
                pass

            async def stream(self):
                try:
                    yield {'type': 'audio', 'data': mp3[:18000]}
                    await asyncio.Event().wait()
                finally:
                    stream_closed.set()

        def receive(packet):
            old_packets.append(packet)
            heard.set()

        with patch('core.providers.tts.edge.edge_tts.Communicate', StalledNetwork):
            worker = threading.Thread(target=provider.to_tts_stream,
                                      args=('固定测试文本。', receive), daemon=True)
            worker.start()
            self.assertTrue(heard.wait(3))
            provider.conn.client_abort = True
            worker.join(2)
        self.assertFalse(worker.is_alive(), 'Cancelled TTS stayed blocked on the network')
        self.assertTrue(stream_closed.is_set())
        count_after_cancel = len(old_packets)

        class CompleteNetwork:
            def __init__(self, text, **kwargs):
                pass

            async def stream(self):
                yield {'type': 'audio', 'data': mp3}

        provider.conn.client_abort = False
        provider.current_sentence_id = provider.conn.sentence_id = 'b' * 32
        new_packets = []
        with patch('core.providers.tts.edge.edge_tts.Communicate', CompleteNetwork):
            provider.to_tts_stream('下一轮测试。', new_packets.append)
        reference = []
        encoder = OpusEncoderUtils(24000, 1, 60)
        try:
            audio_bytes_to_data_stream(mp3, 'mp3', True, reference.append, 24000, encoder)
        finally:
            encoder.close()
        self.assertEqual(reference, new_packets)
        self.assertEqual(count_after_cancel, len(old_packets))

    def test_timeout_before_audio_has_bounded_retries(self):
        provider = self.provider()
        provider.tts_timeout = .05
        calls = []
        closed = []
        packets = []

        class Network:
            def __init__(self, text, **kwargs):
                calls.append(text)

            async def stream(self):
                try:
                    await asyncio.Event().wait()
                    yield {'type': 'audio', 'data': b''}
                finally:
                    closed.append(True)

        with patch('core.providers.tts.edge.edge_tts.Communicate', Network):
            provider.to_tts_stream('固定测试文本。', packets.append)
        self.assertEqual([], packets)
        self.assertEqual(2, len(calls))
        self.assertEqual(2, len(closed))

    def test_cancel_with_buffered_audio_closes_process_pipes(self):
        provider = self.provider()
        mp3 = subprocess.run([
            'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
            'sine=frequency=440:sample_rate=24000', '-t', '60', '-ac', '1',
            '-b:a', '96k', '-f', 'mp3', 'pipe:1'], check=True, stdout=subprocess.PIPE).stdout
        heard = []
        finalizer_errors = []

        class Network:
            def __init__(self, text, **kwargs):
                pass

            async def stream(self):
                yield {'type': 'audio', 'data': mp3}
                await asyncio.Event().wait()

        def cancel(packet):
            heard.append(packet)
            provider.conn.client_abort = True

        gc.collect()
        with patch('core.providers.tts.edge.edge_tts.Communicate', Network), \
                patch('sys.unraisablehook', finalizer_errors.append), \
                self.assertNoLogs('asyncio', level='ERROR'):
            provider.to_tts_stream('固定测试文本。', cancel)
            gc.collect()
        self.assertTrue(heard)
        self.assertEqual([], finalizer_errors, 'Decoder pipes outlived the synthesis event loop')


if __name__ == '__main__':
    unittest.main()
