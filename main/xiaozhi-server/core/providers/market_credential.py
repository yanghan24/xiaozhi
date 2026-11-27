"""One current AI Market credential shared by the providers of an authenticated session."""

import asyncio
import contextlib
import json
import re

import httpx
from websockets.exceptions import InvalidStatus, WebSocketException

from config.manage_api_client import MarketCredentialError, get_market_credential


def market_model_failure(status, body):
    if status in (400, 402, 403) and isinstance(body, (bytes, bytearray, str)) and len(body) <= 4096:
        try:
            body = json.loads(body)
        except ValueError:
            body = None
    if status in (400, 402, 403) and isinstance(body, dict):
        error = body.get("error")
        detail = error if isinstance(error, dict) else body
        if (detail.get("code") in ("INSUFFICIENT_POINTS", "insufficient_points")
                or detail.get("message") in ("积分不足", "积分不足，请充值后重试")):
            return MarketCredentialError("MARKET_INSUFFICIENT_POINTS")
    if status == 403:
        return MarketCredentialError("MARKET_MODEL_FORBIDDEN")
    if status == 429:
        return MarketCredentialError("MARKET_RATE_LIMITED")
    if status >= 500:
        return MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE")
    return None


class MarketSessionCredential:
    def __init__(self, device_id, session, loop, data):
        self._device_id = device_id
        self._session = session
        self._loop = loop
        self._lock = asyncio.Lock()
        self._value = self._read(data)

    @staticmethod
    def _read(data):
        generation, key = data.get("generation"), data.get("api_key")
        if not isinstance(generation, str) or not re.fullmatch(r"[A-Za-z0-9_-]{43}", generation):
            raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE")
        if not isinstance(key, str) or not key:
            raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE")
        return generation, key

    @property
    def value(self):
        if self._value is None:
            raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE")
        return self._value

    def invalidate(self):
        self._value = None

    async def _update(self, *, rejected=None, expected=None):
        async with self._lock:
            current = self.value
            if (rejected is not None and current[0] != rejected) or current[0] == expected:
                return current
            try:
                data = await get_market_credential(self._device_id, self._session, rejected)
            except (httpx.HTTPError, ValueError):
                raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE") from None
            if self._value is None:
                raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE")
            updated = self._read(data)
            if updated[0] == rejected:
                raise MarketCredentialError("MARKET_MODEL_KEY_REJECTED")
            self._value = updated
            return updated

    async def synchronize(self, expected):
        return await self._update(expected=expected)

    async def open_websocket(self, connect):
        generation, key = self.value
        try:
            return await self._connect(connect, key)
        except InvalidStatus as failure:
            if failure.response.status_code != 401:
                raise
        if asyncio.get_running_loop() is self._loop:
            _, key = await self._update(rejected=generation)
        else:
            recovery = asyncio.run_coroutine_threadsafe(self._update(rejected=generation), self._loop)
            _, key = await asyncio.wrap_future(recovery)
        try:
            return await self._connect(connect, key)
        except InvalidStatus as failure:
            if failure.response.status_code == 401:
                raise MarketCredentialError("MARKET_MODEL_KEY_REJECTED") from None
            raise

    @staticmethod
    async def _connect(connect, key):
        try:
            return await connect(key)
        except InvalidStatus as failure:
            rejection = market_model_failure(failure.response.status_code, failure.response.body)
            if rejection:
                raise rejection from None
            raise
        except (OSError, asyncio.TimeoutError, WebSocketException):
            raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE") from None

    def recover_for_thread(self, rejected):
        try:
            current_loop = asyncio.get_running_loop()
        except RuntimeError:
            current_loop = None
        if current_loop is self._loop:
            raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE")
        recovery = asyncio.run_coroutine_threadsafe(self._update(rejected=rejected), self._loop)
        try:
            return recovery.result(timeout=26)
        except TimeoutError:
            recovery.cancel()
            raise MarketCredentialError("MARKET_CREDENTIAL_UNAVAILABLE") from None


async def notify_market_credential_error(conn, error):
    message = MarketCredentialError.messages.get(str(error))
    if not message or getattr(conn, "_market_credential_notified", False):
        return
    conn._market_credential_notified = True
    with contextlib.suppress(Exception):
        await conn.websocket.send(json.dumps({
            "type": "alert", "status": "语音服务", "message": message, "emotion": "neutral",
            "presentation": "notification",
        }, ensure_ascii=False))
