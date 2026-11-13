"""Public audio output boundary; permission and device transport are external fakes."""
import asyncio
import json
import threading
from contextlib import suppress
from types import SimpleNamespace
import unittest

from config.logger import setup_logging
from core.handle.sendAudioHandle import sendAudioMessage, send_tts_message, send_stt_message
from core.product_voice import GuardedSocket, VoiceDenied
from core.providers.tts.dto.dto import SentenceType


class Permission:
    def __init__(self):
        self.ended = threading.Event()
        self.entered = asyncio.Event()
        self.release = asyncio.Event()
        self.release.set()

    async def check_async(self):
        self.entered.set()
        await self.release.wait()
        if self.ended.is_set():
            raise VoiceDenied()


class Device:
    def __init__(self):
        self.frames = []
        self.after_send = None
        self.received = asyncio.Event()

    async def send(self, data):
        self.frames.append(data)
        self.received.set()
        if self.after_send is not None:
            await self.after_send(data)


class AudioOutputTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.permission = Permission()
        self.device = Device()
        self.conn = SimpleNamespace(
            sentence_id="turn-a", session_id="session", client_abort=False,
            product_voice_gate=self.permission, config={}, calling=False,
            close_after_chat=False, client_aec=False, conn_from_mqtt_gateway=False,
            logger=setup_logging(), tts=SimpleNamespace(tts_audio_first_sentence=True),
            websocket=GuardedSocket(self.device, self.permission),
            clearSpeakStatus=lambda: None,
        )
        self.conn.product_turn_is_current = lambda turn: (
            turn == self.conn.sentence_id and not self.conn.client_abort
            and not self.permission.ended.is_set()
        )

        def guard(turn):
            if not self.conn.product_turn_is_current(turn):
                raise VoiceDenied()

        self.conn.product_turn_guard = guard

    async def asyncTearDown(self):
        controller = getattr(self.conn, "audio_rate_controller", None)
        if controller is not None:
            controller.stop_sending()
            if controller.pending_send_task is not None:
                with suppress(asyncio.CancelledError):
                    await controller.pending_send_task

    async def test_new_turn_drops_status_waiting_for_permission(self):
        self.permission.release.clear()
        output = asyncio.create_task(sendAudioMessage(
            self.conn, SentenceType.FIRST, [b"old-audio"], "old answer", "turn-a"
        ))
        await asyncio.wait_for(self.permission.entered.wait(), 1)
        self.conn.sentence_id = "turn-b"
        self.permission.release.set()
        with suppress(VoiceDenied):
            await asyncio.wait_for(output, 1)
        self.assertEqual([], self.device.frames)
        self.assertFalse(self.permission.ended.is_set())

    async def test_new_turn_drops_audio_waiting_for_permission(self):
        self.permission.release.clear()
        output = asyncio.create_task(sendAudioMessage(
            self.conn, SentenceType.MIDDLE, b"old-audio", None, "turn-a"
        ))
        await asyncio.wait_for(self.permission.entered.wait(), 1)
        self.conn.sentence_id = "turn-b"
        self.permission.release.set()
        with suppress(VoiceDenied):
            await asyncio.wait_for(output, 1)
        self.assertEqual([], self.device.frames)
        self.assertFalse(self.permission.ended.is_set())

    async def test_old_audio_is_not_relabelled_after_sentence_start(self):
        async def start_new_turn(data):
            self.conn.sentence_id = "turn-b"

        self.device.after_send = start_new_turn
        with suppress(VoiceDenied):
            await sendAudioMessage(
                self.conn, SentenceType.FIRST, [b"old-audio"], "old answer", "turn-a"
            )
        self.assertEqual(["sentence_start"], [json.loads(frame)["state"]
            if isinstance(frame, str) else "audio" for frame in self.device.frames])

    async def test_old_stop_does_not_clear_new_turn_speaking_state(self):
        self.conn.sentence_id = "turn-b"
        self.conn.client_is_speaking = True
        self.conn.clearSpeakStatus = lambda: setattr(self.conn, "client_is_speaking", False)
        with suppress(VoiceDenied):
            await send_tts_message(self.conn, "stop", sentence_id="turn-a")
        self.assertTrue(self.conn.client_is_speaking)
        self.assertEqual([], self.device.frames)

    async def test_new_turn_drops_stt_waiting_for_permission(self):
        self.permission.release.clear()
        output = asyncio.create_task(send_stt_message(self.conn, "old question"))
        await asyncio.wait_for(self.permission.entered.wait(), 1)
        self.conn.sentence_id = "turn-b"
        self.permission.release.set()
        with suppress(VoiceDenied):
            await asyncio.wait_for(output, 1)
        self.assertEqual([], self.device.frames)

    async def test_new_turn_drops_gateway_audio_waiting_for_permission(self):
        self.conn.conn_from_mqtt_gateway = True
        self.permission.release.clear()
        output = asyncio.create_task(sendAudioMessage(
            self.conn, SentenceType.MIDDLE, b"old-audio", None, "turn-a"
        ))
        await asyncio.wait_for(self.permission.entered.wait(), 1)
        self.conn.sentence_id = "turn-b"
        self.permission.release.set()
        with suppress(VoiceDenied):
            await asyncio.wait_for(output, 1)
        self.assertEqual([], self.device.frames)

    async def test_old_stop_finishes_when_queued_audio_loses_its_turn(self):
        self.conn.config["tts_audio_send_delay"] = 1
        self.permission.release.clear()
        await sendAudioMessage(
            self.conn, SentenceType.MIDDLE, [b"old-audio"], None, "turn-a"
        )
        await asyncio.wait_for(self.permission.entered.wait(), 1)
        stop = asyncio.create_task(send_tts_message(self.conn, "stop"))
        await asyncio.sleep(0)
        self.conn.sentence_id = "turn-b"
        self.permission.release.set()
        with suppress(VoiceDenied):
            await asyncio.wait_for(stop, 0.5)
        self.assertEqual([], self.device.frames)

    async def test_new_turn_can_send_after_old_queued_audio_is_discarded(self):
        self.conn.config["tts_audio_send_delay"] = 1
        self.permission.release.clear()
        await sendAudioMessage(
            self.conn, SentenceType.MIDDLE, [b"old-one", b"old-two"], None, "turn-a"
        )
        await asyncio.wait_for(self.permission.entered.wait(), 1)
        self.conn.sentence_id = "turn-b"
        self.permission.release.set()
        await sendAudioMessage(
            self.conn, SentenceType.MIDDLE, [b"new-audio"], None, "turn-b"
        )
        await asyncio.wait_for(self.device.received.wait(), 1)
        self.assertEqual([b"new-audio"], self.device.frames)
        self.assertFalse(self.permission.ended.is_set())

    async def test_queued_sentence_start_keeps_its_original_turn(self):
        self.conn.config["tts_audio_send_delay"] = 1

        async def start_new_turn(data):
            self.conn.sentence_id = "turn-b"

        self.device.after_send = start_new_turn
        await sendAudioMessage(
            self.conn, SentenceType.MIDDLE, [b"last-old-audio"], None, "turn-a"
        )
        await sendAudioMessage(
            self.conn, SentenceType.FIRST, [], "old queued sentence", "turn-a"
        )
        await asyncio.wait_for(self.conn.audio_rate_controller.queue_empty_event.wait(), 1)
        self.assertEqual([b"last-old-audio"], self.device.frames)

    async def test_current_turn_delivers_text_audio_and_stop_in_order(self):
        self.conn.client_is_speaking = False
        self.conn.clearSpeakStatus = lambda: setattr(self.conn, "client_is_speaking", False)
        await send_stt_message(self.conn, "question")
        await sendAudioMessage(
            self.conn, SentenceType.FIRST, [b"one", b"two", b"three"], "answer", "turn-a"
        )
        await sendAudioMessage(self.conn, SentenceType.LAST, [], None, "turn-a")
        self.assertEqual(["stt", "start", "sentence_start", b"one", b"two", b"three", "stop"],
            [json.loads(frame).get("state", json.loads(frame)["type"])
             if isinstance(frame, str) else frame for frame in self.device.frames])
        self.assertFalse(self.conn.client_is_speaking)

    async def test_legacy_socket_does_not_need_product_turn_callbacks(self):
        self.conn.product_voice_gate = None
        del self.conn.product_turn_is_current
        del self.conn.product_turn_guard
        self.conn.websocket = self.device
        await sendAudioMessage(
            self.conn, SentenceType.FIRST, [b"legacy-audio"], "legacy answer", "turn-a"
        )
        await sendAudioMessage(self.conn, SentenceType.LAST, [], None, "turn-a")
        self.assertEqual(["sentence_start", b"legacy-audio", "stop"],
            [json.loads(frame)["state"] if isinstance(frame, str) else frame
             for frame in self.device.frames])

    async def test_product_output_logs_metadata_without_answer_content(self):
        content = "privateanswercanary"
        messages = []
        sink = self.conn.logger.add(lambda record: messages.append(record.record["message"]))
        try:
            await send_stt_message(self.conn, content)
            await sendAudioMessage(
                self.conn, SentenceType.FIRST, [b"audio"], content, "turn-a"
            )
            await sendAudioMessage(self.conn, SentenceType.LAST, [], None, "turn-a")
        finally:
            self.conn.logger.remove(sink)
        self.assertTrue(any(content in frame for frame in self.device.frames
                            if isinstance(frame, str)))
        self.assertIn(b"audio", self.device.frames)
        self.assertEqual("stop", json.loads(self.device.frames[-1])["state"])
        self.assertNotIn(content, "\n".join(messages))
        self.assertTrue(any("turn-a" in message and "sent frames 1" in message
                            for message in messages))

    async def test_stt_does_not_start_speaking_for_a_newer_turn(self):
        self.conn.client_is_speaking = False

        async def start_new_turn(data):
            self.conn.sentence_id = "turn-b"

        self.device.after_send = start_new_turn
        await send_stt_message(self.conn, "old question")
        self.assertEqual(["stt"], [json.loads(frame)["type"] for frame in self.device.frames])
        self.assertFalse(self.conn.client_is_speaking)


if __name__ == "__main__":
    unittest.main()
