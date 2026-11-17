"""AI Market IAT V2.0 through the original streaming ASR interface."""
import asyncio
import base64
import json
from urllib.parse import urlencode, urlsplit, urlunsplit

import websockets
from config.logger import setup_logging
from core.providers.asr.base import ASRProviderBase
from core.providers.asr.dto.dto import InterfaceType

logger = setup_logging()


class ASRProvider(ASRProviderBase):
    MAX_AUDIO_BYTES = 16000 * 2 * 60
    FRAME_BYTES = 1280  # 40 ms of actual 16 kHz / 16-bit mono PCM

    def __init__(self, config, delete_audio_file=True):
        super().__init__()
        self.interface_type = InterfaceType.STREAM
        self.api_key = config.get("api_key", "")
        self.output_dir = config.get("output_dir", "tmp/")
        self.delete_audio_file = delete_audio_file
        self.model = config.get("model_name", "")
        if self.model != "spark_zh_iat_new":
            raise ValueError("Unsupported AI Market ASR model")
        root = urlsplit(config.get("base_url", "https://aimarket.hncig.cn"))
        known = root.scheme == "https" and root.netloc == "aimarket.hncig.cn"
        fixture = root.scheme == "http" and root.hostname == "127.0.0.1"
        if not (known or fixture) or root.path not in ("", "/") or root.query or root.fragment or root.username:
            raise ValueError("Invalid AI Market ASR endpoint")
        self.ws_url = urlunsplit(("wss" if known else "ws", root.netloc,
                                "/v1/asr/xfyun/iat-v2", urlencode({"model": self.model}), ""))
        self._task = None
        self._conn = None
        self._ws = None
        self._closed = False
        self._ending = False
        self._queue = None
        self._recorded = []
        self._bytes = 0
        self.text = ""
        self.last_error = None
        self.final_timeout = min(15, max(1, float(config.get("final_timeout", 8))))

    async def receive_audio(self, conn, pcm_frame, audio_have_voice):
        if self._closed:
            return
        await super().receive_audio(conn, pcm_frame, audio_have_voice)
        if self._task is None and audio_have_voice:
            if not self.api_key:
                self.last_error = "MARKET_CREDENTIAL_REQUIRED"
                raise ValueError(self.last_error)
            self._conn = conn
            self._queue = asyncio.Queue(maxsize=256)
            self._recorded, self._bytes = [], 0
            self.text, self.last_error, self._ending = "", None, False
            # Includes the current frame exactly once, and VAD's bounded pre-roll.
            for frame in conn.asr_audio:
                self._enqueue(frame)
            self._task = asyncio.create_task(self._recognize(conn))
        elif self._task is not None and not self._ending:
            try:
                self._enqueue(pcm_frame)
            except (ValueError, asyncio.QueueFull):
                self.last_error = "ASR_AUDIO_LIMIT"
                await self.close()
                if hasattr(conn, "close"):
                    await conn.close()
                return
        if self._task is not None and conn.client_voice_stop:
            await self._send_stop_request()

    def _enqueue(self, frame):
        if len(frame) % 2 or self._bytes + len(frame) > self.MAX_AUDIO_BYTES:
            raise ValueError("ASR_AUDIO_LIMIT")
        self._queue.put_nowait(frame)
        self._recorded.append(frame)
        self._bytes += len(frame)

    async def _send_stop_request(self):
        if self._task is not None and not self._ending:
            self._ending = True
            try:
                self._queue.put_nowait(None)
            except asyncio.QueueFull:
                self.last_error = "ASR_BACKPRESSURE"
                await self.close()
                if hasattr(self._conn, "close"):
                    await self._conn.close()

    async def _send(self, ws):
        sequence = 0
        buffer = bytearray()

        async def frame(audio, status):
            nonlocal sequence
            sequence += 1
            body = {"header": {"status": status}, "payload": {"audio": {
                "encoding": "raw", "sample_rate": 16000, "channels": 1, "bit_depth": 16,
                "seq": sequence, "status": status, "audio": base64.b64encode(audio).decode()}}}
            if status == 0:
                body["parameter"] = {"iat": {"domain": "spark_asr_v2", "language": "zh_cn",
                    "accent": "mandarin", "dwa": "wpgs",
                    "result": {"encoding": "utf8", "compress": "raw", "format": "json"}}}
            await ws.send(json.dumps(body, ensure_ascii=False))

        while True:
            part = await self._queue.get()
            if part is None:
                if buffer:
                    await frame(bytes(buffer), 0 if sequence == 0 else 1)
                await frame(b"", 2)
                return
            buffer.extend(part)
            while len(buffer) >= self.FRAME_BYTES:
                chunk = bytes(buffer[:self.FRAME_BYTES])
                del buffer[:self.FRAME_BYTES]
                await frame(chunk, 0 if sequence == 0 else 1)
                await asyncio.sleep(len(chunk) / 32000)

    @staticmethod
    def merge_result(parts, payload):
        if not payload.get("text"):
            return
        data = json.loads(base64.b64decode(payload["text"], validate=True).decode("utf-8"))
        if data.get("ret") not in (None, 0, "0"):
            raise ValueError("ASR_UPSTREAM_REJECTED")
        seq = data.get("sn", payload.get("seq"))
        if not isinstance(seq, int) or seq < 0:
            raise ValueError("ASR_INVALID_RESULT")
        pgs, rg = payload.get("pgs", data.get("pgs")), payload.get("rg", data.get("rg"))
        if pgs == "rpl":
            if not isinstance(rg, list) or len(rg) != 2 or not all(isinstance(n, int) for n in rg):
                raise ValueError("ASR_INVALID_RESULT")
            for old in list(parts):
                if rg[0] <= old <= rg[1]:
                    parts.pop(old)
        parts[seq] = "".join(word.get("w", "") for segment in data.get("ws", [])
                             for word in segment.get("cw", []))

    async def _receive(self, ws):
        parts = {}
        async for message in ws:
            data = json.loads(message)
            header = data.get("header", {})
            if header.get("code") != 0:
                raise ValueError("ASR_UPSTREAM_REJECTED")
            self.merge_result(parts, data.get("payload", {}).get("result", {}))
            if header.get("status") == 2:
                return "".join(parts[n] for n in sorted(parts))
        raise ValueError("ASR_INCOMPLETE_RESULT")

    async def _recognize(self, conn):
        children = []
        success = False
        snapshot = []
        try:
            async with websockets.connect(self.ws_url,
                    additional_headers={"Authorization": "Bearer " + self.api_key},
                    open_timeout=8, close_timeout=2, max_size=1024 * 1024) as ws:
                self._ws = ws
                sender = asyncio.create_task(self._send(ws))
                receiver = asyncio.create_task(self._receive(ws))
                children = [sender, receiver]
                done, _ = await asyncio.wait(children, timeout=80, return_when=asyncio.FIRST_COMPLETED)
                if not done:
                    raise asyncio.TimeoutError()
                if sender in done:
                    await sender
                    self.text = await asyncio.wait_for(receiver, self.final_timeout)
                else:
                    self.text = await receiver
                success = bool(self.text)
                snapshot = self._recorded.copy()
        except asyncio.CancelledError:
            raise
        except Exception:
            # Neither upstream bodies nor websocket headers/URLs belong in logs.
            self.last_error = "ASR_UNAVAILABLE"
            self._closed = True
            logger.warning("AI Market ASR did not complete")
        finally:
            for task in children:
                if not task.done():
                    task.cancel()
            if children:
                await asyncio.gather(*children, return_exceptions=True)
            self._ws = None
            self._recorded = []
            if getattr(conn, "asr", None) is self:
                conn.reset_audio_states()
        if self.last_error and hasattr(conn, "close"):
            await conn.close()
        if success and not self._closed and getattr(conn, "asr", None) is self:
            # Original reporting, voiceprint and startToChat remain responsible.
            await super().handle_voice_stop(conn, snapshot)
        if self._task is asyncio.current_task():
            self._task = None

    async def speech_to_text(self, pcm_data, session_id, artifacts=None):
        result, self.text = self.text, ""
        return result, None

    async def close(self):
        self._closed = True
        current = asyncio.current_task()
        if self._task is not None and self._task is not current:
            self._task.cancel()
            await asyncio.gather(self._task, return_exceptions=True)
        if self._ws is not None:
            await self._ws.close()
        self._task = None
        self._recorded = []
