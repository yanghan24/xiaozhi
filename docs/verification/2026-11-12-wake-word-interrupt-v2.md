# 仅唤醒词打断：实现、服务器部署与验证

关联 [Issue #80](https://github.com/yanghan24/xiaozhi/issues/80)、[服务端 PR #81](https://github.com/yanghan24/xiaozhi/pull/81) 和 [固件 PR #82](https://github.com/yanghan24/xiaozhi/pull/82)。13:19 已部署服务器配套版本，13:20 复核通过；用户报告设备端正在编译或烧录，真实 P4 听音验收尚待完成。以下时间均为 2026-11-12 Asia/Hong_Kong，标有 UTC 的除外。

## 13:19 服务器部署及 13:20 复核

用户明确授权继续部署验证后，在 Ubuntu `192.168.168.114` 以当前 `xiaozhi-product-voice:pr79-f830a8c` 为基础派生，保留 #79 维护修复、现网配置及系统提示词。逐文件基线核对发现 `config_loader.py` 有现网提示词补丁，已在 `75290c4` 保留并定向审查通过；没有直接覆盖为旧版本。

| 标识 | 实际值 |
| --- | --- |
| 本次源码 | `75290c44b2b6116d58674b94d6ef5e4f03eb131e`（包含原 #81 `c0d4ee6`，增加保留现网提示词） |
| 组合镜像 | `xiaozhi-product-voice:pr81-75290c4-pr79-f830a8c` |
| 镜像 ID | `sha256:b19fcbf3f44aafeead93bf4e7e9cf670165bc19b03439253f3340bdfbd225a8d` |
| 运行容器 / 启动时间 | `xiaozhi-pr26-voice` / 13:19:43 |
| 回退镜像 | `xiaozhi-product-voice:pr79-f830a8c`，ID `sha256:19070d27ddd2332d43799c3caff669a6a131f180f98d500ef37824c3cc1bd7ca` |

部署前在**实际候选镜像**中完成真实 WebSocket 回归和 15 项维护 unittest，均通过。网络关闭、无生产配置挂载；仅外部模型/Edge 传输受控。镜像中五个变更 Python 文件逐项校验 hash；`product_voice.py`、`product_maintenance.py`、`websocket_server.py` 及 `config.yaml` 与现网原镜像逐字一致。

部署前完成既有 `backup.py` 备份及私有 Compose/设备状态快照，仅重建语音容器。部署后校验运行文件 hash、实际加载 `wake_word`、产品授权仍启用、唤醒词列表含“你好小智”、API 内部就绪 200、本机 TLS 发现 200、经正常证书验证的本机 TLS WSS 无效令牌以 1008 拒绝。API、数据库、Redis 和 TLS 入口容器及启动时间未变；设备归属/许可/凭据、补发与硬件映射快照及原维护操作记录未变。语音重启计数 0。

[部署摘要](evidence/2026-11-12-wake-word-interrupt-v2/deployment.json)与[复核结果](evidence/2026-11-12-wake-word-interrupt-v2/audit.json)仅含脱敏结果。私有备份、完整日志和回退 Compose 参考保存在 `/home/jt/wake81-deploy-75290c4/deployment-20261112T051940Z`；候选回归在同父目录 `20261112T051825Z`。失败时部署脚本可恢复原语音镜像，本次没有触发回退。后续手动恢复只改语音 image 字段，保留其他任务后续配置、持久数据与维护操作，不覆盖整个旧 Compose。

本轮未执行公网 18443 外部链路探测、未向设备下发 OTA、未烧录设备、未合并 #81。13:20 维护状态为 `no_unique_session`，不能据此确认目标新固件已连接。设备端报告正在编译或烧录；待其提供源码/ELF/文件校验值，再完成普通插话不打断、指定唤醒词停止后立即听新问题、无问候/提示音/旧回答续播和正常结束后连续提问的现场核验。自动化协议验证不能代替此项。

## 行为约定

- 播报期间，普通说话、聊天、环境声音不触发语音打断；仅“你好，小智”取消当前回答。
- 取消后直接聆听新问题，不播问候、不响提示音、不恢复旧回答。
- 正常播报结束后保持连续对话，不要求每轮唤醒。
- 按键停止、账号权限撤销等独立停止机制保留。

## 源码与配套关系

| 部分 | 基点 | 受测源码 | 分支 / 合并目标 |
| --- | --- | --- | --- |
| 小智语音服务 | `b448aa0` | `0949422` | `codex/wake-word-interrupt-v2` → `main` |
| P4 固件 | `a30f6a8` | `6116b72` | `codex/p4-wake-word-interrupt` → `firmware/p4` |

两部分历史不同，固件分支不能合入服务端 `main`。本地旧 server-only 尝试 `f4476ae` 曾在真机失败并恢复，不能当作本次验收。

服务端默认 `interrupt_mode: wake_word`，管理 API 加载配置时保留本地设置；`auto` 可恢复旧语音自动打断策略。播报时在 VAD/ASR 前丢弃麦克风帧，并拒绝迟到识别文本。兼容固件 `abort(wake_word_detected) → listen.start` 和直接 `listen.detect` 两条路径。回答编号及短临界区检查防止取消后的模型流、工具续答和历史写入进入新轮次；不持锁等待模型或工具网络响应。

固件新增 `WAKE_WORD_ONLY_INTERRUPT`（依赖 AFE，在设备管理产品配置下默认开启）。AEC 播报时继续运行 AFE 唤醒检测，停用普通语音处理；其他检测词只重新启用检测。“你好小智”按取消、丢弃待发送麦克风包、清空播放解码、无提示音转聆听的顺序执行。结束后使用自动停听模式保留连续对话。

## Ubuntu 自动化结果

均在 `jt@192.168.168.114` 隔离执行，不使用生产账号、数据库或真实模型凭据。

| 验证 | 结果及边界 |
| --- | --- |
| 真实 WebSocket 回归 | 通过，最终源码 `0949422`，2026-11-12 04:01 UTC；脚本 `scripts/check-wake-word-interrupt.py` |
| 服务端完整 pytest | 128 passed、5 skipped、1 failed；基点 `b448aa0` 在同一环境也是相同结果 |
| 固件 Debug CTest | 29/29 通过，源码 `6116b72` |
| 固件 Python 主机检查 | 43 项运行成功，其中跳过 1 项，源码 `6116b72` |

WebSocket 回归使用真实服务器、连接处理、队列、provider、Opus 编解码；仅外部 OpenAI 兼容 HTTP 和 Edge TTS 传输受控。覆盖普通 detect/麦克风 Opus 包不打断且不进入外部 ASR、其他配置唤醒词不打断、两种唤醒协议取消且无问候、新问题产生实际编码音频、迟到旧回答不续播也不污染后续模型上下文、播报后直接提问。测试在 `--network none` 的语音依赖容器运行，命令为 `python /check-wake-word-interrupt.py`，源码复制到 `/app`。

TDD 红灯：原逻辑收到普通 detect 会发送停止并启动新回答；取消后迟到模型文本仍进入下一请求历史。修复后上述协议检查通过。固件原策略在 AEC/realtime 播报时不启用唤醒检测；新策略和生产实际使用的转换 helper 均由主机测试覆盖，硬件音频服务作为边界替身。这不验证声学识别率、扬声器残留音频或物理麦克风。

完整 pytest 的唯一失败为 `tests/test_tts.py::test_tts_text_to_speak_writes_file`：现有测试在已安装 Edge SDK 时仍访问 `speech.platform.bing.com`，离线环境 DNS 失败。基线与候选均如此，未放宽断言或接入生产凭据来规避。其余跳过为既有配置/导入条件。命令：`python -m pytest -q --disable-warnings`。

固件主机检查使用 CMake Debug 构建 `tests/device_control`，随后执行 CTest；另执行 Python unittest discovery。Ubuntu 日志保留于 `/home/jt/wake-v2-test/{test,pytest}.log`、`/home/jt/wake-v2-baseline/pytest.log`、`/home/jt/wake-v2-firmware/{host-suite,python-host}.log`。

## 初次源码交付时的审查与未完成项（部署前历史）

Standards 审查未发现硬性规范违规；一项非阻断建议是集中三处打断配置判断。Spec 审查发现的工具续答和历史写入竞态、固件转换覆盖不足均已修复；最终定向静态复核无新增阻断问题。

完整 ESP-IDF v5.5.5 构建尚未完成，没有新 `.bin`。Windows 与 Ubuntu114 均没有原完整工具链；Ubuntu 拉取官方 IDF 镜像网络超时，替代镜像被镜像站拒绝。原工程记录位于另一工作站 `/home/bimcenter/Projects/xiaozhi/.worktrees/`，需取得其可达地址，或准备完整工具链与依赖缓存。

本轮未部署服务、未烧录设备、未修改当前部署入口。只读发现生产语音镜像已由另一任务变为 `xiaozhi-product-voice:p4-maintenance-20261112`；后续部署须重新核对，不能使用旧记录覆盖。

继续交付前需在完整环境构建配套固件，核对 sdkconfig 中 AFE/设备管理/新开关，记录产物校验值；随后在 Ubuntu 配套服务与真实 P4 验证：普通插话和噪声不停止、喊词立即停止并直接听新问题、无问候/提示音/旧回答续播、正常结束后可直接提问、连续多次打断、权限撤销仍立即停止。构建通过不能替代这些真机听音结果。
