import asyncio
import json
import re

from core.handle.textMessageHandler import TextMessageHandler
from core.handle.textMessageType import TextMessageType


async def send_reminder_audio(conn, turn_id, title):
    from core.handle.sendAudioHandle import sendAudio, send_tts_message
    from core.utils.util import audio_bytes_to_data_stream

    # Generate the complete short clip before sending any frames. A partial
    # synthesis must not acquire a successful end marker.
    conn.tts.current_sentence_id = turn_id
    audio = await asyncio.wait_for(conn.tts.text_to_speak("日程提醒，" + title, None), 30)
    frames = []
    await asyncio.to_thread(audio_bytes_to_data_stream, audio, conn.tts.audio_file_type,
                            True, frames.append, conn.sample_rate)
    if not frames:
        raise ValueError("Empty reminder audio")
    await send_tts_message(conn, "start", sentence_id=turn_id)
    await sendAudio(conn, frames, sentence_id=turn_id)
    await asyncio.wait_for(send_tts_message(conn, "stop", sentence_id=turn_id),
                           timeout=30 + len(frames) * .15)
    flow = conn.audio_flow_control
    if flow["sentence_id"] != turn_id or flow["packet_count"] != len(frames):
        raise ValueError("Incomplete reminder audio")
    return len(frames)


class ReminderTextMessageHandler(TextMessageHandler):
    @property
    def message_type(self):
        return TextMessageType.REMINDER

    async def handle(self, conn, message):
        gate = getattr(conn, "product_voice_gate", None)
        if gate is None or set(message) != {"type", "id", "state"}:
            return
        key = message["id"]
        if not isinstance(key, str) or not re.fullmatch(r"[a-f0-9]{32}", key):
            return
        action = message["state"]
        if action not in ("claim", "played"):
            return
        chat = conn.product_conversation
        if action == "played" and not getattr(conn, "reminder_playback_stopped", False):
            return
        if action == "played" and getattr(conn, "reminder_playback", None) != (key, conn.sentence_id):
            return
        await gate.check_async()
        if action == "claim" and conn.sentence_id is not None:
            return
        try:
            result = await asyncio.to_thread(gate.authority.request, "reminder",
                {"session": gate.session, "command": {"id": key, "action": action}})
            if action == "played":
                return
            if result.get("status") != "claimed":
                await conn.websocket.send(json.dumps({"type": "reminder", "state": "cancel", "id": key}))
                return
            turn_id = chat.begin()
            conn.reminder_playback = (key, turn_id)
            if not await asyncio.to_thread(chat._wait_until_ready, turn_id):
                return
            await conn.websocket.send(json.dumps({"type": "reminder", "state": "start", "id": key}),
                                      before_send=lambda: chat.guard(turn_id))
            frames = await send_reminder_audio(conn, turn_id, result["title"])
            await conn.websocket.send(json.dumps({"type": "reminder", "state": "end",
                "id": key, "frames": frames}), before_send=lambda: chat.guard(turn_id))
            conn.reminder_playback_stopped = True
        except Exception:
            # A lost claim or an interrupted transmission remains unknown.
            await conn.websocket.close(code=1011, reason="Reminder unavailable")
