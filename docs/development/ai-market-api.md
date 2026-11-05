# 规范通与集市语音接口资料

用于[任务 #12](https://github.com/yanghan24/xiaozhi/issues/12)。以下两段接口信息由用户于 2026-11-05 从集市提供，已拆分为独立的会话创建示例和 MCP 配置；尚未使用真实 Key 调用验证。通用身份与凭证规则见根目录《元筑AI集市应用接入接口文档》v1.0。

## 规范通标识

| 用途 | 值 |
| --- | --- |
| 集市控制台智能体 ID / MCP 路径标识 | `156fd2a5-ade8-4800-b206-21a024d23760` |
| 原生会话 API 的 `app_id` | `e7446555-5f01-4aab-b749-7b2683b7e4e9` |
| 本次提供的资源调用地址根 | `https://aimarket.hncig.cn:4090` |
| 认证 | `Authorization: Bearer YOUR_AIMARKET_KEY`，占位符需替换为有效的集市调用 Key |

上述两个 ID 分别用于各自接口，不能互换。资源地址按本次示例保留 `:4090`；OIDC issuer 及端点仍按身份接入文档使用不带端口的正式地址。

## 创建会话

```bash
curl --request POST 'https://aimarket.hncig.cn:4090/api/ai_apaas/v1/app/conversation' \
  --header 'Authorization: Bearer YOUR_AIMARKET_KEY' \
  --header 'Content-Type: application/json' \
  --data '{
    "app_id": "e7446555-5f01-4aab-b749-7b2683b7e4e9"
  }'
```

此请求创建规范通会话，未包含用户问题。本次尚未提供创建响应，因而还不能确定会话标识在响应中的确切字段位置；也未提供后续发起对话的请求格式。

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

- 创建会话的成功/失败响应，以及后续发起对话的完整请求。
- 流式增量正文、结束、错误和引用等事件样例，会话续接/归属校验与取消机制。
- 如选 MCP：工具定义、参数、返回结果及取消语义。
- 集市语音合成模型名称、调用示例、音色、音频格式/采样率和流式能力。
- 私有配置中的有效测试授权，用于实际验证会话创建、问答和合成。

这里的 `YOUR_AIMARKET_KEY` 是占位符；未进行真实会话创建、问答、合成或 MCP 工具发现。本资料补充不代表 #12 已完成，也不替代设备真机验收。
