# ESP32-P4 集市语音实机联调

2026-11-10，Asia/Hong_Kong。Ubuntu `jt@192.168.168.114`，既有工程 `/home/jt/xiaozhi-ubuntu-test`。本轮按用户确认沿用此前有积分的集市测试账号，未启用公网管理后台。

## 设备与配置

- Waveshare ESP32-P4 WiFi6 Touch LCD 4.3，固件 2.2.6。
- 联网设备标识为 `10:bd:a3:80:72:80`，对应 C6 Wi-Fi STA。P4 工厂 MAC `80:f1:b2:d7:8e:a5` 只用于硬件核对，不用来模拟该设备。
- 既有测试智能体：`issue-6-esp32-p4-test`。绑定、OTA 和 WSS 地址保留。
- ASR：集市 `qwen-audio-3.0-asr-flash-filetrans`。
- LLM：集市 `qwen3.8-flash`，测试适配限制 `max_tokens=256`、`enable_thinking=false`。
- TTS：`TTS_EdgeTTS`，`zh-CN-XiaoxiaoNeural`。
- 此测试智能体关闭记忆、意图工具和聊天历史持久化，使用简短中文回答提示。原设置已私有备份。

管理 API 镜像仍为 `xiaozhi-manager-api:6e354edf292c3df14fd783c5dc7314a4402e3b74`；语音镜像仍为 `xiaozhi-voice:local-20261106-netmirror`。没有部署 #13 产品绑定分支，没有合并代码或重新刷机。

## 测试适配

旧语音服务支持 OpenAI 兼容上传及流式对话，不支持集市异步转写。新增 [market-test-bridge.py](../../scripts/market-test-bridge.py) 作为独立容器 `xiaozhi-issue6-market-bridge`，接入 `xiaozhi-ubuntu-test_default`。管理库新增两个非默认测试模型，只分配给上述智能体；全局默认模型不变。

接口仅在 Docker 网络可访问，不发布宿主机管理端口。公网 Nginx 只新增 `/xiaozhi/test-asr-audio/<43字符随机句柄>.wav` 的 GET 路由，用于集市下载当前录音。录音在适配服务内存中保存，限制单声道 16 kHz / 16 bit PCM、60 秒、最多两个并发请求；结束、失败、取消或 75 秒到期后删除。结果下载不发送集市 Key，禁止重定向，限制结果来源和响应大小，转写提交不自动重放。旧语音服务自身的音频临时文件处理仍由其原实现负责，不能把适配服务的内存边界表述为全链路无落盘。

管理库只保存该适配服务的专属随机凭据。集市 Key 由 Ubuntu 私有工具从**指定的、用户已确认的**测试授权读取，只有该授权仍处于已验证、ACTIVE 且未过期状态才生成 50 秒凭据租约，每 30 秒更新一次。异常时覆盖为空租约，适配服务拒绝新请求及后续轮询。最多 50 秒的失效传播不是产品的即时撤销保证；本次仍是单账号测试接线，不能代替设备归属账号的正式产品调用权限。

本轮租约任务是 transient systemd 服务 `xiaozhi-issue6-credential-lease`，测试窗口截止 **2026-11-11 11:07:23（UTC+08:00）**；不是永久部署。主机重启自动恢复未配置、未验收。续测需要先核对原账号授权，再显式更新私有截止时间和启动租约服务，不能直接复制旧 Key 延长使用。

## 已通过

| 验证 | 结果 |
| --- | --- |
| Ubuntu 镜像内 HTTP 集成检查 | 5 项通过：转写与 URL 清理、失败不重试、授权到期中止、认证/无效音频拒绝、模型 SSE 转发 |
| 真实集市链路 | ASR HTTP 200，2.28 秒，非空识别；LLM HTTP 200 / SSE DONE / 非空回答；Edge TTS 26,640 字节，FFmpeg 解码通过 |
| 当前语音服务完整协议 | 专属临时 MAC，OTA→绑定→WebSocket hello→同会话 ready→16 kHz Opus 输入→ASR→LLM→Edge TTS；收到 68 帧，解码为 4.08 秒 WAV，收到 tts.stop |
| 临时协议数据清理 | 临时智能体及其绑定已删除，真实设备归属保留 |
| 实机第一轮 | 用户确认屏幕显示识别文字、听到完整回答、没有错误提示；测试问题为“请用一句话介绍你自己” |
| 测试服务重启 | 重启适配容器和原语音容器；HTTP 恢复，容器 ID、镜像、真实设备绑定与模型选择前后相同 |
| 服务及音箱重启后的实机复验 | 用户确认无需重新激活，屏幕显示识别文字，能够完整播放回答；按“一加一等于几”的复验步骤反馈 |
| 重启后服务器调用计数 | 适配服务记录 ASR 提交/成功 2/2、LLM 提交/成功 2/2，失败 0，当前处理数与临时音频数均为 0；计数不单独证明具体问题内容 |

实际集市测试会使用已授权账号的额度。模拟设备验证和现场确认分开记录，不把生成的测试录音说成物理麦克风采集。

## 验收边界

本轮单设备真实 ASR → 模型 → Edge TTS → 扬声器播放，以及设备与测试服务重启后的恢复复验均已通过。屏幕和扬声器结果来自现场用户确认，服务器侧补充绑定、配置、协议和调用计数证据。未执行主机重启、长时间运行或多设备容量验收，也不据此关闭 #11/#13 产品身份与绑定的其他验收项。

## 证据与恢复

服务器版本、对话关联、耗时和资源已另行[归档](2026-11-10-device-market-archive.md)。11:27 的后续快照观察到累计 ASR 73 次提交、13 次成功及适配服务 60 次失败，原因尚未诊断；此前两轮成功验收不代表此后持续运行无异常。资源为事后单点采样，端到端延迟未采集。

服务器私有目录 `.runtime/market-test-bridge/` 保存 `agent-before.json`、`agent-after.json`、`model-ids.json`、Nginx 原配置以及 `evidence/real-chain.json`、`evidence/protocol-chain.json`、`evidence/restart-before.json`、`evidence/restart-after.json`。完整录音、管理员令牌、能力 URL、集市 Key 和适配凭据不提交到版本库。

恢复时通过正常管理 API，把本轮修改且仍与 `agent-after.json` 一致的字段逐项恢复为 `agent-before.json` 的值，保留设备绑定；有后续人工改动则先比对，不整份覆盖。随后仅移除 Nginx 中 `BEGIN/END ISSUE6 MARKET ASR` 标记块，执行 `nginx -t` 后 reload，停止上述适配容器和租约服务。保留原语音容器、数据库卷及其他 OIDC 路由。不以旧的整份 Nginx 备份覆盖之后的其他改动。
