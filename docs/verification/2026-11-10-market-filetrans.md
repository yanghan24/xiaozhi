# 集市异步文件 ASR 接口验证

2026-11-10，Ubuntu 私有身份测试环境，用户刚授权的有积分账号。用户指定模型 `qwen-audio-3.0-asr-flash-filetrans` 并提供模型详情调用示例。本轮只验证外部接口，不修改产品 ASR、不发布音频服务、不宣称完成录音到回答闭环。

## 依据和差异

- [Qwen-Audio-ASR-Filetrans 官方 HTTP API](https://help.aliyun.com/zh/model-studio/qwen-audio-asr-filetrans-http-api)，本日读取；当前官方示例为 3.1 系列，集市实际使用用户指定的 3.0 模型标识。
- [Qwen ASR API](https://help.aliyun.com/zh/model-studio/qwen-asr-api-reference)区分其他 ASR 调用形式；不能把单数 `file_url` 的其他模型示例混入本接口。
- 集市路径由用户详情示例和实际调用确认，不能将集市 Key 发给官方 DashScope API。

| 步骤 | 集市实际接口与结果 |
| --- | --- |
| 提交 | `POST https://aimarket.hncig.cn/v1/services/audio/asr/transcription`，Bearer Key、`X-DashScope-Async: enable`、JSON；`input.file_urls` 数组；HTTP 200，output.task_id / PENDING |
| 查询 | `GET https://aimarket.hncig.cn/v1/tasks/{task_id}`，同一 Key；先 RUNNING，后 SUCCEEDED |
| 下载 | output.results 中每项检查 subtask_status；本次返回集市同域 `/v1/asr-results/…` 的不透明结果地址，并非直接 OSS 地址。下载不附带 Authorization，HTTP 成功。不得把带访问能力的完整地址写入日志或公开证据。 |
| 解析 | JSON 的 transcripts[].text；同时带 channel_id、sentences/words 时间戳和 properties。不能按旧适配器读取顶层 text。 |

官方明确整个任务 SUCCEEDED 不保证所有子任务成功，必须逐项检查 subtask_status。本次仅提交一个文件、一个任务，没有重试提交。

## 实际结果

输入是用户示例中的[阿里云公开样本](https://dashscope.oss-cn-beijing.aliyuncs.com/samples/audio/paraformer/hello_world_female2.wav)，没有上传客户录音或公开本地音频。

- 输入属性：16 kHz、PCM s16le，原始长度 3834 ms。
- 转写：`hello world，这里是阿里巴巴语音实验室。`
- 提交 HTTP 响应约 0.47 秒。
- 服务记录 submit_time `10:13:25.408`、scheduled_time `10:13:25.454`、end_time `10:13:25.951`；相差约 0.543 秒。这只是任务端记录差值，不是应用端到端延迟或容量结论。
- usage.duration=3；未确认实际积分扣费数量。
- 原始任务响应及脱敏结果保存在 `/home/jt/xiaozhi-issue29/.runtime/real-identity/asr-filetrans-*.json`，权限 0600。真实 Key 只在进程内解密，按本次 OIDC 请求及当前授权代际选择，不输出 Key。

## 产品接入仍需完成

现有 OpenAI ASR 适配器采用 multipart 文件上传并读取同步 text，不能只替换模型名称/地址使用本接口。需要专门实现：

1. 将录音转换为受支持音频，提供上游可访问的短时 HTTPS URL。优先内存/tmpfs 暂存、随机或签名访问能力、严格时限和容量限制；完成、退出或超时后移除，不进入录音持久化及备份。尚未部署或验证集市到该 URL 的网络可达性。
2. 按当前归属账号和许可取得 Key，提交一次异步任务；请求超时且未知是否受理时，不盲目重试创建，避免重复计费。
3. 有界查询、检查子任务、受限下载并解析结果。下载结果链接不转发业务 Key；限制协议、目的主机、重定向、响应大小和耗时。
4. 提交前、查询/接收迟到结果及进入模型/TTS 前重检当前许可；退出丢弃本地结果并清理临时音频，不能宣称已经取消集市在途任务或收费。
5. 用本地 3.24 秒 TTS 样本验证自己的音频 URL，再以模拟设备音频跑 ASR→模型→Edge。真实麦克风及音箱验收仍需设备。

原始录音默认不持久化，依据[已确认产品规则](../planning/product-rules-2026-11-09.md)。本地删除不等于集市侧删除；远端保留策略仍需核实。


后续实现与真实音频闭环已推进，见[产品 ASR 验证](2026-11-10-product-asr.md)。以上保留为首次接口探测的历史范围。
