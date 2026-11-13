"""Product TTS preserves speech while excluding its body from diagnostic logs."""
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from loguru import logger

from core.providers.tts.edge import TTSProvider


BODY = "PrivateAnswerSentinel"
ERROR_BODY = "PrivateProviderErrorSentinel"


class ProductTTSLoggingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audio = subprocess.run(
            ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
             "anullsrc=r=24000:cl=mono", "-t", "0.2", "-f", "mp3", "pipe:1"],
            check=True, stdout=subprocess.PIPE,
        ).stdout

    def setUp(self):
        self.messages = []
        sink = logger.add(lambda message: self.messages.append(message.record["message"]))
        self.addCleanup(logger.remove, sink)

    def provider(self, *, product=True):
        provider = TTSProvider({"voice": "zh-CN-XiaoxiaoNeural"}, delete_audio_file=True)
        provider.conn = SimpleNamespace(
            sample_rate=24000, product_voice_gate=object() if product else None,
        )
        provider.current_sentence_id = "logging-test-turn"
        provider.opus_encoder = None
        return provider

    def synthesize(self, provider, *, failure=False):
        audio, requests, frames = self.audio, [], []

        class EdgeBoundary:
            def __init__(self, text, **kwargs):
                requests.append(text)

            async def stream(self):
                if failure:
                    raise RuntimeError(ERROR_BODY + ": " + BODY)
                yield {"type": "audio", "data": audio}

        with patch("edge_tts.Communicate", EdgeBoundary):
            provider.to_tts_stream(BODY, opus_handler=frames.append)
        return requests, frames

    def test_product_success_delivers_audio_without_logging_answer(self):
        requests, frames = self.synthesize(self.provider())
        self.assertEqual([BODY], requests)
        self.assertTrue(frames, "Successful TTS must still emit audio")
        self.assertTrue(self.messages, "Safe operational diagnostics must remain")
        diagnostics = "\n".join(self.messages)
        self.assertNotIn(BODY, diagnostics)
        self.assertIn("logging-test-turn", diagnostics)

    def test_product_failure_does_not_log_answer_or_remote_error_body(self):
        requests, frames = self.synthesize(self.provider(), failure=True)
        self.assertTrue(requests, "The controlled external failure must be exercised")
        self.assertFalse(frames)
        self.assertTrue(self.messages, "Failure diagnostics must remain")
        diagnostics = "\n".join(self.messages)
        self.assertNotIn(BODY, diagnostics)
        self.assertNotIn(ERROR_BODY, diagnostics)
        self.assertIn("logging-test-turn", diagnostics)

    def test_legacy_success_retains_its_existing_diagnostics(self):
        requests, frames = self.synthesize(self.provider(product=False))
        self.assertEqual([BODY], requests)
        self.assertTrue(frames)
        self.assertIn(BODY, "\n".join(self.messages))


if __name__ == "__main__":
    unittest.main()
