# 仅唤醒词打断：实现与验证（未部署）

关联 [Issue #80](https://github.com/yanghan24/xiaozhi/issues/80)。本记录交付候选源码和自动化验证，不代表真实 P4 已完成验收。

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

## 审查与未完成项

Standards 审查未发现硬性规范违规；一项非阻断建议是集中三处打断配置判断。Spec 审查发现的工具续答和历史写入竞态、固件转换覆盖不足均已修复；最终定向静态复核无新增阻断问题。

完整 ESP-IDF v5.5.5 构建尚未完成，没有新 `.bin`。Windows 与 Ubuntu114 均没有原完整工具链；Ubuntu 拉取官方 IDF 镜像网络超时，替代镜像被镜像站拒绝。原工程记录位于另一工作站 `/home/bimcenter/Projects/xiaozhi/.worktrees/`，需取得其可达地址，或准备完整工具链与依赖缓存。

本轮未部署服务、未烧录设备、未修改当前部署入口。只读发现生产语音镜像已由另一任务变为 `xiaozhi-product-voice:p4-maintenance-20261112`；后续部署须重新核对，不能使用旧记录覆盖。

继续交付前需在完整环境构建配套固件，核对 sdkconfig 中 AFE/设备管理/新开关，记录产物校验值；随后在 Ubuntu 配套服务与真实 P4 验证：普通插话和噪声不停止、喊词立即停止并直接听新问题、无问候/提示音/旧回答续播、正常结束后可直接提问、连续多次打断、权限撤销仍立即停止。构建通过不能替代这些真机听音结果。
