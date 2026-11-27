from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import httpx
import openai
import pytest
from config.manage_api_client import MarketCredentialError
from core.providers.llm.openai.openai import LLMProvider
from core.providers.vllm.openai import VLLMProvider
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
              "response_format": {"type": "json_object"}, "thinking": "default",
              "reasoning_effort": "none"}
    for source, key in [("aimarket", "a-key"), ("aimarket", "b-key"), ("system", "system-key")]:
        provider = LLMProvider(dict(public, source=source, api_key=key))
        list(provider.response("fixture", [{"role": "user", "content": "test"}]))
    assert [c["api_key"] for c in constructed] == ["a-key", "b-key", "system-key"]
    assert [c.get("max_retries") for c in constructed] == [0, 0, None]
    assert requests[0]["response_format"] == {"type": "json_object"}
    assert "response_format" not in requests[2]
    assert [body.get("extra_body") for body in requests] == [
        {"reasoning_effort": "none"}, {"reasoning_effort": "none"}, None
    ]
    assert "api_key" not in public

    tools = [{"type": "function", "function": {"name": "lookup"}}]
    list(provider.response_with_functions("fixture", [{"role": "user", "content": "test"}], tools))
    assert "extra_body" not in requests[-1]
    market = LLMProvider(dict(public, source="aimarket", api_key="a-key"))
    list(market.response_with_functions("fixture", [{"role": "user", "content": "test"}], tools))
    assert requests[-1]["tools"] == tools
    assert requests[-1]["extra_body"] == {"reasoning_effort": "none"}
    market.response_no_stream("system", "test")
    assert requests[-1]["response_format"] == {"type": "json_object"}
    assert requests[-1]["extra_body"] == {"reasoning_effort": "none"}


def test_market_chat_refreshes_only_before_opening_a_stream(monkeypatch):
    denial = openai.AuthenticationError("rejected", response=httpx.Response(
        401, request=httpx.Request("POST", "https://aimarket.hncig.cn/v1/chat/completions"),
    ), body=None)
    active_stream = Mock()
    credential = Mock(value=("old", "old-fixture"))
    credential.recover_for_thread.return_value = ("new", "new-fixture")
    client = Mock()
    client.with_options.return_value.chat.completions.create.side_effect = [denial, active_stream]
    monkeypatch.setattr("core.providers.llm.openai.openai.openai.OpenAI", Mock(return_value=client))
    provider = LLMProvider({"model_name": "fixture", "source": "aimarket", "api_key": "old-fixture",
                            "market_credential_handle": credential})
    assert provider._create_completion({"stream": True}) is active_stream
    credential.recover_for_thread.assert_called_once_with("old")
    assert [call.kwargs["api_key"] for call in client.with_options.call_args_list] == ["old-fixture", "new-fixture"]
    credential.recover_for_thread.reset_mock()
    client.with_options.return_value.chat.completions.create.side_effect = openai.BadRequestError(
        "rejected", response=httpx.Response(400, request=httpx.Request("POST", "https://aimarket.hncig.cn/v1/chat/completions")),
        body={"message": "积分不足，请充值后重试"},
    )
    with pytest.raises(MarketCredentialError, match="MARKET_INSUFFICIENT_POINTS"):
        provider._create_completion({"stream": True})
    credential.recover_for_thread.assert_not_called()


def test_vision_provider_only_disables_reasoning_for_market(monkeypatch):
    create = Mock(return_value=SimpleNamespace(
        choices=[SimpleNamespace(message=SimpleNamespace(content="fixture"))]
    ))
    client = SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=create)))
    monkeypatch.setattr("core.providers.vllm.openai.openai.OpenAI", Mock(return_value=client))
    config = {"model_name": "fixture", "api_key": "fixture-key", "reasoning_effort": "none"}

    assert VLLMProvider(dict(config, source="aimarket")).response("test", "image") == "fixture"
    assert create.call_args.kwargs["extra_body"] == {"reasoning_effort": "none"}
    assert VLLMProvider(dict(config, source="system")).response("test", "image") == "fixture"
    assert "extra_body" not in create.call_args.kwargs


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
