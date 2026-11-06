# 千问语音合成接入调查

查阅日期：2026-11-05。用户暂定通过 AI 集市调用千问语音合成；本文核对阿里云官方资料，未进行真实模型调用，也不表示 AI 集市已开放任何具体型号。集市已提供的规范通接口见 [接口资料](ai-market-api.md)。

> 当前执行更新（2026-11-06）：测试先使用微软 Edge TTS，已通过 Ubuntu 合成与解码检查。本文保留千问官方资料调研，集市 TTS 尚未上线；不代表当前测试模型或集市已支持的型号。

## 联调候选与边界

官网将 **Qwen-Audio-TTS** 标记为推荐系列，实时合成示例使用 `qwen-audio-3.1-tts-flash` 和 `longanhuan_v3.1`。[1][2] 建议将该型号作为优先联调候选，待集市确认开放型号和映射后确定；不据此承诺延迟、音质或设备兼容性。

| 项目 | 建议基线 | 依据与待验证事项 |
| --- | --- | --- |
| 模型 | `qwen-audio-3.1-tts-flash` | 官网示例型号；集市是否开放、使用原名还是别名待确认。[1][2] |
| 音色 | `longanhuan_v3.1` | 官网列出的系统音色；仅作测试默认值，正式音色由试听确定。[5] |
| 输出 | `format: "pcm"`、`sample_rate: 24000` | 两个值均在 WebSocket 参数允许范围内；这是建议值，官方默认值为 MP3、22050 Hz。[3] |
| 输入方式 | 分段输入文本、流式接收音频 | 官方 WebSocket 支持双向流式，适合承接智能体逐步返回的文本。[1][2] |

设备实际支持的采样率、声道、PCM 位深与字节序、音频帧边界需联调确认；如需转换，由小智语音服务适配后下发。官方支持 `opus` 输出，[3] 但不能据此认定其封装及分帧与小智设备协议兼容。

HTTP 接口要求一次提交文本，支持非流式或流式输出；WebSocket 则支持多次提交文本。[1][2][6] 本次优先验证 WebSocket 路径，不同时建设两套实现。Qwen3-TTS 的 `input_text_buffer.append` 等事件属于另一系列；不能混用其会话协议与本文的任务协议。[1]

## 上游协议参考

以下地址和鉴权仅代表阿里云上游接口，**不是 AI 集市的 TTS 入口**。小智语音服务应使用集市确认的资源地址和对应集市 Key，不将集市 Key 发送到阿里云端点。

| 项目 | 阿里云官方契约 |
| --- | --- |
| 北京地域地址 | `wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference`，替换为真实业务空间 ID。[2] |
| 鉴权 | 握手请求头 `Authorization: Bearer <百炼 API Key>`，使用与地域对应的 Key。[1][2] |
| 握手失败 | Key 无效或缺失时返回 HTTP 401/403。[2] |
| 文本事件 | JSON，客户端通过 `header.action` 指定操作；服务端通过 `header.event` 返回事件。[3][4] |
| 音频数据 | 经 WebSocket binary 通道返回；`result-generated` 的 `sentence-synthesis` 子事件后传输音频帧。[2][4] |

官方流程如下；发送文本与接收音频需要并行处理。[2][3][4]

1. 建立连接，发送 `run-task`；收到 `task-started` 后才发送后续指令。
2. 顺序发送一个或多个 `continue-task`，同时接收 `result-generated` 及二进制音频。
3. 所有文本发完立即发送正常 `finish-task`，继续接收剩余音频，直到 `task-finished`。
4. 正常结束或取消后，可复用当前连接；必须等本轮 `task-finished`，再用新 `task_id` 发送下一轮 `run-task`。[1]

服务端会自动分句：完整语句立即合成，不完整语句先缓存；正常 `finish-task` 会强制合成剩余缓存，因此不能省略。[2] `task-failed` 表示失败，应读取 `header.error_code` 和 `header.error_message`，关闭连接并处理错误。[4]

同一任务的客户端控制事件使用同一个 UUID；每轮新任务生成新的 UUID。[2][3] 以下示例共用一个合法 UUID，仅供展示，生产代码不能固定复用。正常结束和取消是两种互斥的结束示例。

启动任务，参数结构依据客户端事件定义，模型和音色依据官方联调示例：[2][3]

```json
{
  "header": {
    "action": "run-task",
    "task_id": "3f2d5c86-0550-45c0-801f-a25c8e4c8619",
    "streaming": "duplex"
  },
  "payload": {
    "task_group": "audio",
    "task": "tts",
    "function": "SpeechSynthesizer",
    "model": "qwen-audio-3.1-tts-flash",
    "parameters": {
      "text_type": "PlainText",
      "voice": "longanhuan_v3.1",
      "format": "pcm",
      "sample_rate": 24000
    },
    "input": {}
  }
}
```

收到 `task-started` 后提交文本：[3]

```json
{
  "header": {
    "action": "continue-task",
    "task_id": "3f2d5c86-0550-45c0-801f-a25c8e4c8619",
    "streaming": "duplex"
  },
  "payload": {"input": {"text": "你好，请告诉我你想查询的规范问题。"}}
}
```

文本发送完毕后的正常结束：[3]

```json
{
  "header": {
    "action": "finish-task",
    "task_id": "3f2d5c86-0550-45c0-801f-a25c8e4c8619",
    "streaming": "duplex"
  },
  "payload": {"input": {}}
}
```

打断时取消当前任务：[3]

```json
{
  "header": {
    "action": "finish-task",
    "task_id": "3f2d5c86-0550-45c0-801f-a25c8e4c8619",
    "streaming": "duplex"
  },
  "payload": {"input": {"directive": "cancel"}}
}
```

官方说明取消后立即返回 `task-finished`，且不再输出后续音频；随后可在同一连接重新发起任务。[3] 已到达小智语音服务或设备的音频不会因这条协议指令自动消失。

## 小智侧设计要求

以下是接入设计要求，尚未实现或通过真机验证。

- `continue-task` 单次最多 20000 字符、单任务累计最多 200000 字符，发送间隔不得超过 23 秒，否则连接超时。[3] 等到有可发送的正文后再建立并启动 TTS 任务，避免等待智能体首字时耗尽输入时间；后续文本停顿需设置超时处置。
- 智能体返回内容先按集市确认的增量或累计语义整理成新增正文，再送入 TTS，避免重复朗读。当前规范通示例尚未说明该语义，见 [接口资料](ai-market-api.md)。
- 打断时立即停止本地播放和继续送入旧轮次文本，清理服务端及设备端待播缓冲，并使迟到的旧轮次音频失效；同时在可发送控制指令的任务状态下向上游发送取消。如果任务尚未启动或连接失效，则终止该本地任务并关闭相关连接。
- 同一连接只顺序处理任务，等上一轮终止后再启动下一轮；超时或失败的连接先关闭再重建。连接、文本队列和音频缓冲按归属账号及对话轮次隔离，不跨账号复用。
- 沿用已确认业务边界：仅在归属账号登录与授权有效时使用对应集市 Key；退出后立即停止调用并清理在途任务。身份和权限以集市为准，设备归属由小智管理后台维护，参见 [ADR-0001](../adr/0001-use-ai-market-identity-and-permissions.md)、[ADR-0005](../adr/0005-backend-owns-device-management.md) 及 [接口资料](ai-market-api.md)。
- 沿用已确认需求，额度和结算由集市处理，小智不维护本地账本。上游服务端会返回 `usage` 信息，[4] 但不能用它代替集市的计费结果；本次不进行定价比较。

## 仍需 AI 集市确认

| 待确认项 | 联调所需材料 |
| --- | --- |
| 开放型号及映射 | 实际可用的千问 TTS 型号、资源标识、模型别名与音色范围；是否可用本文候选。 |
| 资源 URL 与协议 | 正式 TTS 地址、地域、鉴权方式；是否透传 WebSocket 双向流式及二进制音频，或由集市提供不同协议。 |
| 结束与取消 | 是否透传 `finish-task` / `directive: cancel`，或等价取消机制；任务终止、断连、超时和错误响应样例。 |
| 账号 Key 权限 | 设备归属账号专用 Key 是否具备 TTS 权限，登录或授权失效及退出后的停用契约。 |
| 有效测试授权与实际流 | 通过私有配置提供测试授权，取得包含启动、文本、音频、结束、取消和失败的原始流式样例。 |
| 音箱适配 | 实测首段可听时间、连续播放、尾音完整性、打断后无旧音频串入，以及实际采样率与封装适配。 |

本文只完成官方资料核对和 JSON 语法检查；集市转发行为、模型可用性、音箱播放与计费归属仍须取得有效授权后联调。

## 官方来源

- [1] [实时语音合成：推荐系列、输入方式与连接复用](https://help.aliyun.com/zh/model-studio/qwen-tts-realtime)
- [2] [Qwen-Audio-TTS WebSocket 接入指南](https://help.aliyun.com/zh/model-studio/qwen-audio-tts-websocket-api)
- [3] [Qwen-Audio-TTS 客户端事件](https://help.aliyun.com/zh/model-studio/qwen-audio-tts-client-events)
- [4] [Qwen-Audio-TTS 服务端事件](https://help.aliyun.com/zh/model-studio/qwen-audio-tts-server-events)
- [5] [Qwen-Audio-TTS 音色列表](https://help.aliyun.com/zh/model-studio/qwen-audio-tts-voice-list)
- [6] [Qwen-Audio-TTS HTTP API 参考](https://help.aliyun.com/zh/model-studio/qwen-audio-tts-http-api)
