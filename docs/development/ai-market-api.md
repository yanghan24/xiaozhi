# 规范通与集市语音接口资料

用于[任务 #12](https://github.com/yanghan24/xiaozhi/issues/12)。规范通接口信息由用户于 2026-11-05 从集市页面提供，包括会话创建、流式对话请求、响应示例和 MCP 配置；语音部分另附同日查阅的千问官方资料，尚未使用真实 Key 调用验证。通用身份与凭证规则见根目录《元筑AI集市应用接入接口文档》v1.0。页面说明业务参数沿用元筑接口，积分计入 Key 所属账号；文件上传后使用返回的文件 ID，文件接口尚未提供。

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

示例中的 `request_id` 和 `conversation_id` 均位于 JSON 顶层。前者标识本次请求，后者是新会话的标识；两个大写值都是占位符。此请求未包含用户问题，取得的 `conversation_id` 放入后续流式对话请求的 JSON 顶层。

## 流式对话

方法为 `POST`，地址为 `https://aimarket.hncig.cn:4090/api/ai_apaas/v1/app/conversation/runs`，内容类型为 `application/json`，使用集市 Bearer Key 认证。

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `app_id` | string | 是 | 应用 ID |
| `conversation_id` | string | 是 | 先创建会话取得的 ID |
| `query` | string | 否 | 本轮问题；工具回传或继续节点时按元筑要求填写 |
| `stream` | boolean | 否 | 是否返回 SSE 流；本次示例显式设为 `true` |
| `file_ids` | string[] | 否 | 上传接口返回的文件 ID 列表；元筑当前仅处理第一个文件 |
| `end_user_id` | string | 否 | 上游业务用户标记，不决定集市计费用户 |
| `tools` | object[] | 否 | 元筑支持的工具定义 |
| `tool_choice` | object 或 string | 否 | 原生工具选择参数 |
| `tool_outputs` | object[] | 否 | 工具执行结果 |
| `parameters` | object | 否 | 工作流开始节点参数 |
| `action` | object | 否 | 继续交互节点的原生 action |
| `mcp_authorization` | object[] | 否 | 元筑支持的 MCP 授权信息 |
| `custom_metadata` | object | 否 | 原生自定义元信息 |

```bash
curl --request POST 'https://aimarket.hncig.cn:4090/api/ai_apaas/v1/app/conversation/runs' \
  --header 'Authorization: Bearer YOUR_AIMARKET_KEY' \
  --header 'Content-Type: application/json' \
  --no-buffer \
  --data '{
    "app_id": "e7446555-5f01-4aab-b749-7b2683b7e4e9",
    "conversation_id": "CONVERSATION_ID",
    "query": "你好",
    "stream": true
  }'
```

`CONVERSATION_ID` 需替换为创建会话返回的真实值。`end_user_id` 仅标记上游业务用户，不能用它切换集市权限或计费账号；积分仍计入调用 Key 所属账号。小智侧继续按已确认要求，在设备归属账号登录及授权有效时使用对应 Key，退出后停止使用。

页面提供的流式响应 JSON 样例：

```json
{
  "request_id": "REQUEST_ID",
  "answer": "本段内容",
  "is_completion": false,
  "content": []
}
```

页面说明响应字段和事件由元筑返回，具体内容取决于应用，SSE 流包含多条 `data` 事件。上述 JSON 仅是页面展示的响应内容，并非包含事件边界的完整原始 SSE 记录。`answer` 是新增片段还是累计全文、完成事件是否带有剩余正文、错误事件与终止标记的格式均未明确；不能据此假定存在 `[DONE]` 或兼容其他模型的流式协议。空的 `content` 数组也未说明引用或非文本元素的结构。

创建会话后可用返回的 ID 发起上述请求；会话有效期、归属校验、续接行为和取消语义仍需文档或授权实测确认。以上均为文档样例，并非真实调用结果。

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

## 语音合成：暂定千问

用户暂定使用千问语音合成，调用仍经 AI 集市。已查阅阿里云官方实时合成指南、WebSocket 协议、客户端/服务端事件及音色列表，详细依据和最小请求示例见[千问语音合成调研](qwen-tts-research.md)。

| 项目 | 已取得的官方依据与接入建议 |
| --- | --- |
| 候选模型 | 官方当前推荐 Qwen-Audio-TTS，实时示例使用 `qwen-audio-3.1-tts-flash`；先作为联调候选，集市实际开放型号尚未确认 |
| 流式方式 | WebSocket 分段发送文本、接收二进制音频；使用 `run-task`、`continue-task`、`finish-task`，不能混用 Qwen3-TTS 的事件格式 |
| 初始测试参数 | 官方示例音色 `longanhuan_v3.1`；建议从 PCM、24000 Hz 开始，音色体验和设备播放格式仍需真机核实 |
| 完成与错误 | `task-finished` 表示任务结束，`task-failed` 表示失败；完整字段见调研引用的官方服务端事件文档 |
| 中断 | 官方支持 `finish-task` 携带 `payload.input.directive: "cancel"`；小智侧还需停止播放并清理旧轮次缓冲，集市是否透传取消待验证 |

官方地址使用百炼 Key，不能直接将集市 Key 发给阿里云。集市侧的 TTS 资源地址、模型映射、支持协议和取消行为，仍须由集市模型详情或正式配置确认；不能仅替换域名或凭据就认定接入完成。官方 HTTP 接口也支持一次提交文本后流式返回，但与 WebSocket 分段输入不同，首版按集市实际开放能力确定一条调用路径。

## 仍需补齐

- 创建会话和发起对话的失败响应，以及可重试条件。
- 原始 SSE 中 `answer` 的增量/累计语义、结束和错误事件、终止标记，以及引用/非文本 `content` 结构；会话有效期、续接/归属校验与取消机制。
- 如选 MCP：工具定义、参数、返回结果及取消语义。
- 集市开放的千问具体型号/映射、TTS 调用地址及协议、可用音色、格式/采样率、流式与取消能力；官方协议资料已取得，集市代理契约和实测仍待补齐。
- 私有配置中的有效测试授权，用于实际验证会话创建、问答和合成。

这里的 `YOUR_AIMARKET_KEY` 是占位符；未进行真实会话创建、问答、合成或 MCP 工具发现。本资料补充不代表 #12 已完成，也不替代设备真机验收。
