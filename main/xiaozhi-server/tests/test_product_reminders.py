import json
import unittest
from datetime import datetime, timedelta
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from core.product_reminders import BEIJING, ReminderConversation, due_time
from core.handle.textHandler.reminderMessageHandler import ReminderTextMessageHandler


class ReminderConversationTest(unittest.TestCase):
    def setUp(self):
        self.now = datetime(2026, 11, 30, 18, 30, tzinfo=BEIJING)
        self.chat = ReminderConversation()
        self.request = Mock(return_value={"status": "saved"})
        self.guard = Mock()

    def say(self, text):
        return self.chat.reply(text, self.request, self.guard, self.now)

    def test_create_requires_separate_confirmation_and_uses_beijing(self):
        self.assertIn("12月1日15点30分", self.say("明天下午三点半提醒我开会。"))
        self.request.assert_not_called()

        self.assertEqual("日程已保存。", self.say("确认。"))
        command = self.request.call_args.args[0]
        self.assertEqual(("create", "开会", 1796110200000),
                         (command["action"], command["title"], command["due_at_ms"]))
        self.assertIsNone(self.say("确认"))
        self.request.assert_called_once()

    def test_cancel_proposal_and_unrelated_question_invalidate_prior_confirmation(self):
        self.say("十分钟后提醒我喝水")
        self.assertIn("放弃", self.say("算了"))
        self.say("半小时后提醒我喝水")
        self.assertIsNone(self.say("请介绍你自己"))
        self.assertIsNone(self.say("确认"))
        self.request.assert_not_called()

    def test_ambiguous_time_past_time_and_recurrence_do_not_schedule(self):
        for query in ("明天三点提醒我喝水", "今天上午三点提醒我喝水",
                      "每天下午四点提醒我喝水", "明天下午二十五点提醒我吃饭"):
            with self.subTest(query=query):
                self.assertIn("请说清", self.say(query))
                self.assertIsNone(self.say("确认"))
        self.request.assert_not_called()

    def test_explicit_time_and_relative_time_are_exact(self):
        self.assertEqual(self.now + timedelta(minutes=25), due_time("二十五分钟后", self.now))
        self.assertEqual(datetime(2027, 1, 1, 2, 0, tzinfo=BEIJING),
                         due_time("2027-01-01 02:00", self.now))
        self.assertIn("12月8日09点00分，开会", self.say("提醒我下周二上午九点开会"))
        self.assertIn("12月2日18点30分，开会", self.say("提醒我12月2日18:30开会"))

    def test_edits_and_cancellation_are_versioned_and_previewed(self):
        original = {"id": "a"*32, "version": 7, "due_at_ms": 1796100600000, "title": "开会"}
        self.request.side_effect = lambda cmd: ({"items": [original]} if cmd["action"] == "list"
                                              else {"status": "cancelled" if cmd["action"] == "cancel" else "changed"})
        self.assertIn("第1项", self.say("查询日程"))
        self.assertIn("开会", self.say("修改第一项日程，改到后天上午九点"))
        self.assertIn("状态已变化", self.say("确认"))
        update = self.request.call_args.args[0]
        self.assertEqual((original["id"], 7, "update"), (update["id"], update["version"], update["action"]))
        self.assertIn("取消北京时间", self.say("取消第一项日程"))
        self.assertIn("已取消", self.say("确认"))
        self.assertEqual({"action": "cancel", "id": original["id"], "version": 7}, self.request.call_args.args[0])

    def test_interrupted_confirmation_cannot_mutate(self):
        self.say("三分钟后提醒我喝水")
        self.guard.side_effect = RuntimeError("obsolete")
        with self.assertRaises(RuntimeError):
            self.say("确认")
        self.request.assert_not_called()


class ReminderPlaybackTest(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.handler = ReminderTextMessageHandler()
        self.key = "a" * 32
        self.gate = SimpleNamespace(check_async=AsyncMock(), session="test",
                                     authority=SimpleNamespace(request=Mock(return_value={"status": "claimed", "title": "开会"})))
        self.conn = SimpleNamespace(product_voice_gate=self.gate, sentence_id=None,
                                    product_conversation=Mock(), websocket=AsyncMock())
        def begin():
            self.conn.sentence_id = "turn"
            return "turn"
        self.conn.product_conversation.begin.side_effect = begin
        self.conn.product_conversation._wait_until_ready.return_value = True
        self.media = AsyncMock(return_value=3)
        patcher = patch("core.handle.textHandler.reminderMessageHandler.send_reminder_audio", self.media)
        patcher.start()
        self.addCleanup(patcher.stop)

    async def message(self, action, key=None):
        await self.handler.handle(self.conn, {"type": "reminder", "state": action, "id": key or self.key})

    async def test_end_frame_count_is_required_before_device_played_receipt(self):
        await self.message("played")
        self.gate.authority.request.assert_not_called()
        await self.message("claim")
        messages = [json.loads(call.args[0]) for call in self.conn.websocket.send.await_args_list]
        self.assertEqual(["start", "end"], [message["state"] for message in messages])
        self.assertEqual(3, messages[-1]["frames"])
        self.gate.authority.request.assert_called_once()
        await self.message("played", "b" * 32)
        self.gate.authority.request.assert_called_once()
        await self.message("played")
        self.assertEqual("played", self.gate.authority.request.call_args.args[1]["command"]["action"])

    async def test_missing_or_partial_audio_is_never_acknowledged(self):
        self.media.side_effect = ValueError("incomplete")
        await self.message("claim")
        await self.message("played")
        self.conn.websocket.close.assert_awaited_once()
        self.assertEqual("start", json.loads(self.conn.websocket.send.await_args.args[0])["state"])
        self.gate.authority.request.assert_called_once()

    async def test_unavailable_reminder_cancels_without_synthesis(self):
        self.gate.authority.request.return_value = {"status": "unavailable"}
        await self.message("claim")
        self.assertEqual("cancel", json.loads(self.conn.websocket.send.await_args.args[0])["state"])
        self.media.assert_not_awaited()
        await self.message("played")
        self.gate.authority.request.assert_called_once()
