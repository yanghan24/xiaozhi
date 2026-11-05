# 规范通与集市语音接口资料

用于[任务 #12](https://github.com/yanghan24/xiaozhi/issues/12)。以下接口信息由用户于 2026-11-05 从集市页面提供，包括会话创建请求、响应示例和 MCP 配置；尚未使用真实 Key 调用验证。通用身份与凭证规则见根目录《元筑AI集市应用接入接口文档》v1.0。页面说明业务参数沿用元筑接口，积分计入 Key 所属账号；文件上传后使用返回的文件 ID，文件接口尚未提供。

## 规范通标识

| 用途 | 值 |
| --- | --- |
| 集市控制台智能体 ID / MCP 路径标识 | `156fd2a5-ade8-4800-b206-21a024d23760` |
| 原生会话 API 的 `app_id` | `e7446555-5f01-4aab-b749-7b2683b7e4e9` |
| 本次提供的资源调用地址根 | `https://aimarket.hncig.cn:4090` |
| 认证 | `Authorization: Bearer YOUR_AIMARKET_KEY`，占位符需替换为有效的集市调用 Key |

上述两个 ID 分别用于各自接口，不能互换。资源地址按本次示例保留 `:4090`；OIDC issuer 及端点仍按身份接入文档使用不带端口的正式地址。

## 创建会话

方法为 `POST`，内容类型为 `application/json`。`app_id` 是必填的字符串字段。

```bash
curl --request POST 'https://aimarket.hncig.cn:4090/api/ai_apaas/v1/app/conversation' \
  --header 'Authorization: Bearer YOUR_AIMARKET_KEY' \
  --header 'Content-Type: application/json' \
  --data '{
    "app_id": "e7446555-5f01-4aab-b749-7b2683b7e4e9"
  }'
```

页面提供的响应示例：

```json
{
  "request_id": "REQUEST_ID",
  "conversation_id": "CONVERSATION_ID"
}
```

示例中的 `request_id` 和 `conversation_id` 均位于 JSON 顶层。前者标识本次请求，后者是新会话的标识；两个大写值都是占位符。此请求未包含用户问题，后续发起对话时如何携带会话标识，仍需对应请求契约。

页面另说明响应字段和事件由元筑返回，具体内容取决于应用，流式响应包含多条 `data` 事件。这段通用说明尚未给出对话增量、完成或错误事件的具体结构，不能据此假定兼容其他模型的流式协议。以上是文档样例，并非真实调用结果。

## MCP 连接配置

```json
{
  "mcpServers": {
    "yuanzhu_agent": {
      "url": "https://aimarket.hncig.cn:4090/api/ai-agents/156fd2a5-ade8-4800-b206-21a024d23760/mcp",
      "headers": {
        "Authorization": "Bearer YOUR_AIMARKET_KEY"
      }
    }
  }
}
```

传输方式依据已有接入文档和控制台通用说明为 Streamable HTTP。该配置给出了连接地址及鉴权；工具名称、输入 schema 和结果结构仍需正式说明或用有效测试 Key 进行 MCP 工具发现。取得两种入口不代表首版同时实现两套调用路径，仍按 #12 选择满足语音需求的一条路径。

## 仍需补齐

- 创建会话的失败响应，以及后续发起对话的完整请求。
- 流式增量正文、结束、错误和引用等事件样例，会话续接/归属校验与取消机制。
- 如选 MCP：工具定义、参数、返回结果及取消语义。
- 集市语音合成模型名称、调用示例、音色、音频格式/采样率和流式能力。
- 私有配置中的有效测试授权，用于实际验证会话创建、问答和合成。

这里的 `YOUR_AIMARKET_KEY` 是占位符；未进行真实会话创建、问答、合成或 MCP 工具发现。本资料补充不代表 #12 已完成，也不替代设备真机验收。
