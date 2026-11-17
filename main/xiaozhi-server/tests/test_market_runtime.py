from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest
from core.providers.llm.openai.openai import LLMProvider
from core.api.vision_handler import VisionHandler
from core.auth import AuthenticationError


def test_original_openai_provider_scopes_json_and_retry_settings_to_market(monkeypatch):
    constructed, requests = [], []
    def client(**kwargs):
        constructed.append(kwargs)
        def create(**body):
            requests.append(body)
            return (value for value in [])
        return SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=create)))
    monkeypatch.setattr("core.providers.llm.openai.openai.openai.OpenAI", client)
    public = {"model_name": "compatible", "base_url": "https://aimarket.hncig.cn/v1",
              "response_format": {"type": "json_object"}, "thinking": "default"}
    for source, key in [("aimarket", "a-key"), ("aimarket", "b-key"), ("system", "system-key")]:
        provider = LLMProvider(dict(public, source=source, api_key=key))
        list(provider.response("fixture", [{"role": "user", "content": "test"}]))
    assert [c["api_key"] for c in constructed] == ["a-key", "b-key", "system-key"]
    assert [c.get("max_retries") for c in constructed] == [0, 0, None]
    assert requests[0]["response_format"] == {"type": "json_object"}
    assert "response_format" not in requests[2]
    assert "api_key" not in public


@pytest.mark.asyncio
async def test_vision_web_test_header_cannot_bypass_device_identity(monkeypatch):
    open_identity = AsyncMock(side_effect=AuthenticationError("Device login required"))
    monkeypatch.setattr("core.api.vision_handler.DeviceSession.open", open_identity)
    get_config = AsyncMock()
    monkeypatch.setattr("core.api.vision_handler.get_private_config_from_api", get_config)
    monkeypatch.setattr("core.api.base_handler.setup_logging", lambda *a, **k: Mock())
    handler = VisionHandler({"speaker_identity_enabled": True, "server": {"auth_key": "fixture"}})
    handler._add_cors_headers = lambda response: None
    request = SimpleNamespace(headers={"Client-Id": "web_test_client", "Device-Id": "other-device"})
    response = await handler.handle_post(request)
    assert response.status == 401
    open_identity.assert_awaited_once()
    get_config.assert_not_awaited()
