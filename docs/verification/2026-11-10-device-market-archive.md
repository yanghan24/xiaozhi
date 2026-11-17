# 服务器版本、对话关联、耗时与资源归档

归档编号：`issue6-20261110T032730Z`。主快照采集于 **2026-11-10 11:27:30（香港时间）**；资源采样起止 UTC 时间保存在 JSON 中。采集为只读操作，没有重新发起模型调用或重启服务。运行文件校验在主快照后补采。

验收结论与现场确认见[实机验证记录](2026-11-10-device-market-voice.md)。本归档同时记录成功验收之后观察到的异常，不能只引用成功轮次描述整个运行窗口。

## 服务器版本

Ubuntu `192.168.168.114`，工作目录 `/home/jt/xiaozhi-ubuntu-test`。记录了 10 个相关容器的完整 ID、镜像 ID、tag、可用的 registry digest、启动时间、健康状态和重启策略。没有导出 Docker 镜像本体或数据库备份；本归档是版本和运行证据，不是经过演练的完整灾备包。

| 组件 | 可核验标识 |
| --- | --- |
| 管理 API | 源码标签 `6e354edf292c3df14fd783c5dc7314a4402e3b74`；实际 `/app/app.jar` SHA-256 `6ab94a4b12bfb2ac3ce027db10e159955f76e594ea599cdd60f87401cd6979da` |
| 原语音服务及测试适配运行镜像 | `xiaozhi-voice:local-20261106-netmirror`；镜像 ID `sha256:30cc7d741a54babdd753cb60ffeb5b1c85d61b94d49ad499c0d420cd62af4fa7` |
| 语音源码版本限制 | 镜像 revision 标签为 `working-tree`，不能声称对应某个精确 Git 提交；补采实际运行容器 142 个 Python 源文件的 SHA-256 |
| 本轮适配脚本 | 提交 `d8dd4b8`；运行文件 SHA-256 `2fd9278c1b6600f04d56c777a6bb64675f26a6584c0e5e2f456568f5aa8bdef5`，与本地文件一致 |
| 身份服务 | 记录单独镜像 ID；实际 `/app/app.jar` SHA-256 `a0fd9487b90009f250bae754670cb7a431a719d40f49dcd69b40d878edafd04f` |
| 固件 | ESP32-P4 WiFi6，2.2.6；固件身份依据交接与绑定回读，不是在服务器重新读取 Flash |

完整清单：[versions.json](evidence/2026-11-10-device-market-voice/versions.json)、[runtime-artifacts.json](evidence/2026-11-10-device-market-voice/runtime-artifacts.json)。

## 对话关联与调用结果

联网 MAC `10:bd:a3:80:72:80` → 智能体 `issue-6-esp32-p4-test`（ID `8d47067dedd14d8bb04fa48f7edf5731`）→ 本轮专用 ASR/LLM 模型 → 用户明确选择的原有积分集市测试账号。P4 工厂 MAC 不用于该关联。配置及现场确认保存在 [association.json](evidence/2026-11-10-device-market-voice/association.json)。

| 轮次/范围 | 关联证据及结论 |
| --- | --- |
| 独立服务链路 | 测试录音 → 集市 ASR → 集市 LLM → Edge TTS，成功；不是物理音箱输入 |
| 完整协议模拟 | 专用临时身份完成 Opus 输入和输出，成功后删除；不借用真实设备身份 |
| 实机首轮 | 按“请用一句话介绍你自己”发起；用户确认显示识别文字、完整播放、无错误提示 |
| 服务及设备重启后 | 按“请告诉我一加一等于几”复验；用户确认无需重新激活、显示识别文字、完整播放 |
| 早期重启后检查点 | ASR 2/2、LLM 2/2、失败 0，已记录在此前验收记录中 |
| 本次归档时累计值 | ASR 提交 73、成功 13；LLM 提交 13、成功 13；适配服务失败计数 60，活动请求 0、内存中临时音频 0 |

**后续异常仍未诊断。** 提交与成功之差均在 ASR 阶段，适配服务共记 60 次失败，但现有计数不包含逐次原因。不能推断为 60 次用户有意问答、60 次收费、网络故障或模型错误，也不能把累计 13 次成功直接等同于 13 次扬声器实播。归档时的这些数据扩大了已观察运行范围，不撤销用户对两轮成功实测的确认，但不支持“持续运行无异常”的结论。

从原语音容器日志提取 150 条白名单事件：80 条输入文件事件、18 条 ASR 文本事件、18 条 LLM 输入事件、34 条 TTS 文本事件，涉及 3 个文件名中明确出现的 session ID。事件范围为 **11:10:10–11:27:26（香港时间）**。公开记录只保留 UTC 时间戳、事件种类、源行号、会话 ID 或文字哈希，不保留文字内容。

旧日志没有在每条事件中携带设备 ID / session ID；不能用时间相邻强行建立一一对应关系。3 个会话包含临时协议测试与真实设备流量，尚无完整的设备→会话→ASR task→LLM request 追踪链。聊天历史持久化此前关闭；两次现场反馈也没有精确采集墙钟时间。详见 [conversation-events.json](evidence/2026-11-10-device-market-voice/conversation-events.json) 的关联边界。筛选后的对话原文仅保存在服务器 root 可读的私有归档，不写入 Git；未复制原始混合日志。

## 耗时

| 指标 | 已有记录 | 边界 |
| --- | --- | --- |
| 单次独立测试 ASR HTTP 往返 | 2.28 秒 | 从提交上传请求到收到识别文字，包含适配、任务轮询和网络；不是纯模型执行时间，也不是实机端到端延迟 |
| 完整协议输出音频 | 68 个 Opus 帧，解码 4.08 秒 | 播放内容时长，不是模型响应耗时 |
| 独立 Edge TTS 输出 | 26,640 字节，解码通过 | 文件大小，不是合成耗时 |
| 实机收音结束→识别完成、LLM 首字、首段 TTS、扬声器首音、整轮完成 | 未采集 | 现有 INFO 日志未保留 ASR DEBUG 耗时；缺失值不补估，不据此计算 P50/P95 |

原始脱敏报告：[real-chain.json](evidence/2026-11-10-device-market-voice/real-chain.json)、[protocol-chain.json](evidence/2026-11-10-device-market-voice/protocol-chain.json)。

## 资源快照

这是一份 **11:27 的事后单次快照**，不是成功对话或失败期间的峰值、均值或压测结果。Docker CPU 百分比以工具原始口径保存；网络/块 IO 是累计计数，不是本轮对话消耗。

| 容器 | CPU | 内存使用 / 限额 |
| --- | --- | --- |
| 管理 API | 1.98% | 470.5 / 896 MiB |
| 原语音服务 | 2.69% | 128 / 640 MiB |
| 集市测试适配 | 0.00% | 32.12 / 192 MiB |
| 管理 Web | 0.00% | 7.305 / 96 MiB |
| 管理 MySQL | 0.54% | 133.9 / 640 MiB |
| 管理 Redis | 0.97% | 4.695 / 192 MiB |
| 公网 Nginx | 0.10% | 10.04 / 96 MiB |

宿主机 8 个逻辑 CPU，load average 为 `0.35 / 0.34 / 0.53`；内存总量 `16,394,560 kB`，可用 `13,113,924 kB`；所在文件系统可用 `473,283,379,200` 字节。身份服务及其独立数据库/Redis 资源也一并保存在 [resources.json](evidence/2026-11-10-device-market-voice/resources.json)，没有纳入上表的管理服务占用。

## 归档位置与完整性

- Git 脱敏证据目录：[evidence/2026-11-10-device-market-voice/](evidence/2026-11-10-device-market-voice/manifest.json)，逐文件 SHA-256 和大小见 manifest。
- 服务器私有归档：`/home/jt/xiaozhi-ubuntu-test/.runtime/market-test-bridge/archive-20261110T032730Z/`，包括 root-only 对话筛选原文；凭据、录音、原始混合日志、能力 URL 未进入脱敏包。
- 本地压缩包：当前任务 worktree 的 `.runtime/archives/issue6-20261110T032730Z.tar.gz`，只打包脱敏证据。
- 本轮适配仍属于临时测试，截止时间保持 2026-11-11 11:07:23（香港时间）；本次归档没有续期或改变服务运行配置。
