# 唤醒词打断部署

2026-11-11（Asia/Hong_Kong），Ubuntu jt@192.168.168.114。用户要求改为唤醒词打断。

产品语音镜像更新为 `xiaozhi-product-voice:wake-word-20261111`，基于现用 `prompt-7f10cbd`，仅修改 config_loader、receiveAudioHandle 和 listenMessageHandler。保留前一轮五模式提示词及产品鉴权。持久配置 `private/voice-data/.config.yaml` 设置 `interrupt_mode: wake_word`；未配置时保留 auto 行为。

播报期间不将普通麦克风音频交给 VAD/ASR，不处理迟到的普通识别结果和普通 detect 消息。设备发送匹配配置唤醒词的 detect 消息时，先执行 abort 再处理唤醒回应；关闭唤醒回应时仍可停播。显式设备 abort（包括固件唤醒词、按钮停止）继续接受。设备权限停播不受此设置影响。唤醒词识别由固件负责，服务端仍保留既有唤醒词列表，含“你好小智”。

Ubuntu 派生镜像运行 `scripts/check-wake-word-interrupt.py`，覆盖普通音频、迟到 ASR、普通 detect、唤醒回应开/关、空闲提问、显式 abort，均通过。实际容器配置回读确认 wake_word、产品授权开启、原 2707 字符提示词 SHA256 不变；服务运行无重启循环，8000 监听可达。未操作实体音箱、未验证固件播报期间的唤醒检测效果。

备份与构建：`/home/jt/wake-interrupt-20261111`，包含原 Compose、原语音配置、补丁脚本、测试、构建日志。恢复前确认无后续部署，仅恢复原语音镜像 `prompt-7f10cbd` 并移除此次 interrupt_mode 配置，然后重建 product-voice；不要覆盖后续私有配置。未执行恢复演练。未修改数据库或设备固件。