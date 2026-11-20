"""A fixed product wake greeting, sent through the normal permission gate."""
import asyncio
from functools import lru_cache
from pathlib import Path

from core.handle.sendAudioHandle import sendAudioMessage, send_tts_message
from core.product_voice import VoiceDenied
from core.providers.tts.dto.dto import SentenceType
from core.utils.dialogue import Message
from core.utils.util import audio_to_data_stream

WAKE_TEXT = '我在，请说'
# Shipped, fixed Xiaoxiao voice at +50% volume (24 kHz mono PCM). It does
# not change with the connection's configured voice or call an online TTS.
WAKE_AUDIO = Path(__file__).resolve().parent.parent / 'config/assets/product_wakeup.wav'


@lru_cache(maxsize=8)
def _wakeup_frames(sample_rate, audio_format):
    """Cache fixed audio per negotiated output format, never customer speech."""
    frames = []
    audio_to_data_stream(str(WAKE_AUDIO), is_opus=audio_format != 'pcm',
                         callback=frames.append, sample_rate=sample_rate)
    if not frames:
        raise ValueError('Empty product wake audio')
    return tuple(frames)


async def play_product_wakeup(conn, turn_id):
    """Play once for this wake turn; a later turn or revocation stops output."""
    conversation = conn.product_conversation
    try:
        if not await asyncio.to_thread(conversation._wait_until_ready, turn_id):
            return
        conversation.guard(turn_id)
        try:
            frames = await asyncio.to_thread(_wakeup_frames, conn.sample_rate, conn.audio_format)
        except Exception:
            # Keep the session usable if the packaged audio is unavailable.
            conn.logger.warning('Product wake audio unavailable')
            if conversation.current(turn_id):
                await send_tts_message(conn, 'stop', sentence_id=turn_id)
            return
        await sendAudioMessage(conn, SentenceType.FIRST, frames, WAKE_TEXT, turn_id)
        with conversation.lock:
            conversation.guard(turn_id)
            conn.dialogue.put(Message(role='user', content='嘿，你好呀'))
            conn.dialogue.put(Message(role='assistant', content=WAKE_TEXT))
        await sendAudioMessage(conn, SentenceType.LAST, [], None, turn_id)
    except VoiceDenied:
        return
    except Exception:
        # Do not expose exception details, which can include private data.
        conn.logger.warning('Product wake greeting failed')
        if conversation.current(turn_id):
            try:
                await send_tts_message(conn, 'stop', sentence_id=turn_id)
            except Exception:
                pass
