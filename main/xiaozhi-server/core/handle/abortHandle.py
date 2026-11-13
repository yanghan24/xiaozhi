import json
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from core.connection import ConnectionHandler
TAG = __name__


async def handleAbortMessage(conn: "ConnectionHandler"):
    conn.logger.bind(tag=TAG).info("Abort message received")
    # 设置成打断状态，会自动打断llm、tts任务
    conn.close_after_chat = False
    conn.client_abort = True
    if getattr(conn, 'product_voice_gate', None) is not None:
        conn.product_conversation.invalidate()
    if getattr(conn, 'product_voice_gate', None) is not None and conn.asr is not None:
        conn.asr.cancel()
    conn.clear_queues()
    # 打断客户端说话状态
    stop = json.dumps({"type": "tts", "state": "stop", "session_id": conn.session_id})
    if getattr(conn, 'product_voice_gate', None) is not None:
        def still_aborted():
            if not conn.client_abort or conn.sentence_id is not None:
                from core.product_voice import VoiceDenied
                raise VoiceDenied()
        await conn.websocket.send(stop, before_send=still_aborted)
    else:
        await conn.websocket.send(stop)
    conn.clearSpeakStatus()
    conn.logger.bind(tag=TAG).info("Abort message received-end")
