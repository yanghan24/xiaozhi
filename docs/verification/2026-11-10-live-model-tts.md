# 真实集市模型到 Edge TTS 验证

2026-11-10 10:07（香港时间），源码 `eb3a806`。用户提供有积分账号并完成真实 OIDC 授权后，明确要求先验证模型到 TTS。未更改产品代码、未部署生产、未连接物理音箱。

## 已通过

Ubuntu 隔离环境运行正式 `WebSocketServer`、`ConnectionHandler`、`MarketModel`、Edge TTS 适配器、音频队列及 Opus 编码器。协议客户端发送短文本（本轮不经过 ASR）：

```text
请说：你好，语音测试成功。
  → 真实 qwen3.8-flash：HTTP 200 / SSE [DONE]
  → 回答：你好，语音测试成功。
  → 真实 Edge TTS（zh-CN-XiaoxiaoNeural）
  → 正式 WebSocket：54 个二进制音频帧
  → 解码 WAV：24 kHz / 单声道 / 16 bit / 3.24 秒
```

音频峰值 30041、RMS 4521.54，文件可由 WAV 解码器读取且不是静音；未以自动解码检查冒充人工听感验收。SHA-256：`8cecf92c83378cca9edf26e6de6b4e3fdc1632d87d7ded77c7dabe8dca05541d`。音频内容仅为本轮测试句，保留在工作区 `.runtime/voice-test.wav` 和 Ubuntu 私有证据目录，不提交音频到 Git。

生成完成后调用真实 Java 单台退出 HTTP 接口：既有 WebSocket 关闭，直接经正式 MarketModel 尝试的下一次调用在许可检查处拒绝，旧令牌重连也拒绝。因此旧连接不能继续提交文本给 TTS；本次只验证完成一轮后的退出，进行中迟到 TTS 的隔离证据仍来自[前一轮受控回归](2026-11-10-product-voice.md)。

本轮仅 1 次真实模型请求。测试代理施加 `max_tokens=128`、`enable_thinking=false` 和最多一次上游调用限制；没有合并为产品默认设置。未获得实际扣分数据。

## 凭据和测试替代边界

本次按刚生成的授权请求查找 `IDENTITY_VERIFIED` 流程，以及同 subject、同 creation_order/started_order 的当前 ACTIVE 授权，不选择任意最新账号。真实 Key 由现有身份库解密，短暂放在 root-only `/run` 文件并只读挂载到临时测试容器，测试后已删除；未打印、未写入版本库、未放入使用固定加密密钥的合成测试库。

Java 设备许可和退出仍使用真实 HTTP、MySQL/Redis 与隔离的合成设备/身份 fixture。私有 loopback 代理只在外部模型边界把合成 Key 替换为本次获授权账号的真实 Key，并透明转发实际 SSE；Edge 直接访问真实服务，没有替代响应。该设置证明正式模型/音频代码与外部服务的兼容性及退出闸门，但不等于真实 OIDC 账号与产品设备归属已经在同一部署完成端到端验收。

运行目录 `/home/jt/issue13-integration/.runtime/live-voice/` 保留 `run.log`、`result.json`、`voice-test.wav`；私有执行器为 `/home/jt/run-live-voice.sh`、`check-live-voice.py` 和 `load-live-model-key.py`。测试使用 `issue13-db`、`issue13-redis`、`issue13-runner` 和一次性语音容器；结束后停止本轮测试容器，原业务服务保持运行。

## 前置探测与剩余范围

此前无积分账号的 HTTP 400 属历史结果；有积分账号授权后，8-token 最小请求返回 HTTP 200/[DONE] 但正文为空、finish_reason=length，随后 128-token 且关闭思考的请求返回 `OK`/stop。真实生成与 SSE 已不再受积分阻挡。

真实 ASR、用户录音到回答、物理扬声器播放、设备端停播 ACK、固件凭据交付、真实账号与产品设备完整映射、公网整链路仍未验收。容量及令牌/会话清理仍是上线前事项；#11/#13 不关闭。
