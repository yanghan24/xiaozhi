import os
import uuid
import asyncio
import edge_tts
from datetime import datetime
from core.providers.tts.base import TTSProviderBase, logger
from core.providers.tts.dto.dto import SentenceType
from core.utils.opus_encoder_utils import OpusEncoderUtils
from core.utils.tts import MarkdownCleaner


class TTSProvider(TTSProviderBase):
    TTS_PARAM_CONFIG = [
        ("ttsVolume", "volume", 0, 100, 50, int),
        ("ttsRate", "speech_rate", -100, 100, 0, int),
        ("ttsPitch", "pitch_rate", -100, 100, 0, int),
    ]

    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        if config.get("private_voice"):
            self.voice = config.get("private_voice")
        else:
            self.voice = config.get("voice")
        self.audio_file_type = config.get("format", "mp3")

        volume = config.get("volume", "50")
        self.volume = int(volume) if volume else 50

        speech_rate = config.get("rate", "0")
        self.speech_rate = int(speech_rate) if speech_rate else 0

        pitch_rate = config.get("pitch", "0")
        self.pitch_rate = int(pitch_rate) if pitch_rate else 0

        # 应用百分比调整
        self._apply_percentage_params(config)

        self.edge_rate = f"{self.speech_rate:+}%"
        self.edge_volume = f"{self.volume:+}%"
        self.edge_pitch = f"{self.pitch_rate:+}Hz"

    def generate_filename(self, extension=".mp3"):
        return os.path.join(
            self.output_file,
            f"tts-{datetime.now().date()}@{uuid.uuid4().hex}{extension}",
        )

    def to_tts_stream(self, text, opus_handler=None):
        if (getattr(self.conn, 'product_voice_gate', None) is None
                or not self.delete_audio_file or self.audio_file_type != 'mp3'
                or self.conn.audio_format != 'opus'):
            return super().to_tts_stream(text, opus_handler)
        turn_id = self.current_sentence_id
        cleaned = MarkdownCleaner.clean_markdown(text)
        if self._correct_words_pattern:
            cleaned = self._correct_words_pattern.sub(
                lambda match: self.correct_words[match.group(0)], cleaned)
        emitted = False
        handler = opus_handler or self.handle_opus

        def emit(packet):
            nonlocal emitted
            if not self.conn.product_turn_is_current(turn_id):
                raise asyncio.CancelledError()
            if not emitted:
                self.tts_audio_queue.put((SentenceType.FIRST, None, text, turn_id))
            handler(packet)
            emitted = True

        # Retrying after any frame was queued can replay speech already heard.
        for attempt in range(2):
            try:
                asyncio.run(self._stream_product_audio(cleaned, turn_id, emit))
                if not emitted:
                    raise RuntimeError('No decoded speech')
                logger.info('Edge speech complete: turn {}', turn_id)
                return
            except asyncio.CancelledError:
                return
            except Exception as error:
                logger.warning('Edge speech unavailable: turn {} type {} attempt {}',
                               turn_id, type(error).__name__, attempt + 1)
                if emitted or not self.conn.product_turn_is_current(turn_id):
                    return

    async def _stream_product_audio(self, text, turn_id, emit):
        if not self.conn.product_turn_is_current(turn_id):
            raise asyncio.CancelledError()
        decoder = await asyncio.create_subprocess_exec(
            'ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error',
            '-probesize', '32', '-analyzeduration', '0', '-f', 'mp3',
            '-i', 'pipe:0', '-f', 's16le', '-ac', '1',
            '-ar', str(self.conn.sample_rate), 'pipe:1',
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL)
        encoder = None
        tasks = []
        try:
            encoder = OpusEncoderUtils(self.conn.sample_rate, 1, 60)

            async def feed():
                # Streaming bypasses text_to_speak's connection wrapper, so keep
                # its authoritative check before every external attempt.
                await self.conn.product_voice_gate.check_async()
                if not self.conn.product_turn_is_current(turn_id):
                    raise asyncio.CancelledError()
                communicate = edge_tts.Communicate(
                    text, voice=self.voice, rate=self.edge_rate,
                    volume=self.edge_volume, pitch=self.edge_pitch)
                stream = communicate.stream()
                try:
                    while True:
                        try:
                            chunk = await asyncio.wait_for(stream.__anext__(), self.tts_timeout)
                        except StopAsyncIteration:
                            break
                        if chunk['type'] == 'audio':
                            decoder.stdin.write(chunk['data'])
                            await asyncio.wait_for(decoder.stdin.drain(), self.tts_timeout)
                finally:
                    await stream.aclose()
                    decoder.stdin.close()

            async def consume():
                remainder = b''
                while True:
                    data = await asyncio.wait_for(decoder.stdout.read(8192), self.tts_timeout)
                    if not data:
                        break
                    data = remainder + data
                    even = len(data) - len(data) % 2
                    if even:
                        encoder.encode_pcm_to_opus_stream(data[:even], False, emit)
                    remainder = data[even:]
                if remainder:
                    raise RuntimeError('Incomplete PCM sample')
                if await asyncio.wait_for(decoder.wait(), 5) != 0:
                    raise RuntimeError('Speech decoder failed')
                encoder.encode_pcm_to_opus_stream(b'', True, emit)

            async def cancelled():
                while self.conn.product_turn_is_current(turn_id):
                    await asyncio.sleep(.05)
                raise asyncio.CancelledError()

            tasks = [asyncio.create_task(feed()), asyncio.create_task(consume())]
            work = asyncio.gather(*tasks)
            watcher = asyncio.create_task(cancelled())
            tasks.extend([work, watcher])
            done, _ = await asyncio.wait([work, watcher], return_when=asyncio.FIRST_COMPLETED)
            if watcher in done:
                await watcher
            await work
        finally:
            for task in tasks:
                task.cancel()
            if decoder.returncode is None:
                try:
                    decoder.kill()
                except ProcessLookupError:
                    pass
            if tasks:
                await asyncio.gather(*tasks, return_exceptions=True)
            # Process exit alone does not finish pipe transports. Drain buffered
            # PCM and await stdin closure before asyncio.run closes this loop.
            try:
                decoder.stdin.close()
                try:
                    await asyncio.wait_for(decoder.stdin.wait_closed(), 5)
                except (BrokenPipeError, ConnectionResetError):
                    pass  # Expected when cancellation kills a decoder still reading.
                await asyncio.wait_for(decoder.communicate(), 5)
            finally:
                if encoder is not None:
                    encoder.close()

    async def text_to_speak(self, text, output_file):
        try:
            communicate = edge_tts.Communicate(
                text,
                voice=self.voice,
                rate=self.edge_rate,
                volume=self.edge_volume,
                pitch=self.edge_pitch,
            )
            if output_file:
                # 确保目录存在并创建空文件
                os.makedirs(os.path.dirname(output_file), exist_ok=True)
                with open(output_file, "wb") as f:
                    pass

                # 流式写入音频数据
                with open(output_file, "ab") as f:  # 改为追加模式避免覆盖
                    async for chunk in communicate.stream():
                        if chunk["type"] == "audio":  # 只处理音频数据块
                            f.write(chunk["data"])
            else:
                # 返回音频二进制数据
                audio_bytes = b""
                async for chunk in communicate.stream():
                    if chunk["type"] == "audio":
                        audio_bytes += chunk["data"]
                return audio_bytes
        except Exception as e:
            error_msg = f"Edge TTS请求失败: {e}"
            raise Exception(error_msg)  # 抛出异常，让调用方捕获
