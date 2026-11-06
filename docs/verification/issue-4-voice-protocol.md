# #4 语音服务与设备协议验证记录

验证日期：2026-11-06，本机 Fedora 44 / x86_64。任务：[#4 接通语音服务并验证设备激活、绑定与 WebSocket 协议](https://github.com/yanghan24/xiaozhi/issues/4)。运行方法见[本机语音服务说明](../development/voice-baseline.md)。

## 源码、产物与环境

- 上游固定提交：`xinnan-tech/xiaozhi-esp32-server@c8e60d1325650c8a31d00e36d8528f8bb74aeb11`。
- 任务分支：`feat/4-voice-protocol`，基于 `origin/main@46e7f33c9d0d94603c2a58c8e4dc197c1d9e0464`。
- 最终镜像的实现源码提交：`8205dbb81f92478d0c52aa659939ec4fd9ecd8bd`。标签 `org.opencontainers.image.revision` 指向该提交，`io.xiaozhi.upstream.revision` 记录上游版本；报告及证据在后续提交中保存。
- 在上游 `core/connection.py` 和 `core/websocket_server.py` 增加会话初始化状态与经过设备认证的 HTTP 就绪入口，确保后台配置错误不能被 `hello` 和 Pong 掩盖；保留正常握手时序。其他新增内容为构建、隔离运行、协议客户端和检查脚本。
- 复用项目 `xiaozhi-baseline` 的真实管理 API、页面、MySQL、Redis、匹配的私有 `.env` 及既有本地测试账号。管理镜像继续使用 `fd104daaa3fc7c3017ece00d13deb319778f69c7`，没有重新初始化数据库或移除数据卷。
- Docker Engine 29.8.0 / Compose 5.5.1，约 7.7 GiB 物理内存。语音端口仅发布到 `127.0.0.1:18000` 和 `127.0.0.1:18003`，管理页面为 `127.0.0.1:18002`。
- 本机客户端在 Compose 网络内使用实际 OTA 返回的 `ws://voice:8000/xiaozhi/v1/`。管理端发布地址经过上游真实连通性校验，宿主机另行通过回环端口检查 HTTP 存活。

| 产物 | 本机 Docker image ID | 镜像大小（字节） |
| --- | --- | --- |
| `xiaozhi-voice:8205dbb81f92478d0c52aa659939ec4fd9ecd8bd` | `sha256:30bbbc3d21b4f379caeacd60b363c2c18b3a234f0726feb6732d1a9921806bce` | 1295230648 |
| `xiaozhi-voice-tests:8205dbb81f92478d0c52aa659939ec4fd9ecd8bd` | `sha256:c69de746c7a78eb0b6f772499728f343dfbfb0328391f2bc2ee1ba589f1970a4` | 1314884849 |

这是本机构建记录，镜像层之间有共享；没有发布到镜像仓库。基础镜像固定为 `python:3.10-slim-bookworm@sha256:5be5aaecb962a41567bc8041f1e9e3432cb085b037fe057f9a562630b2e4b370`。

## 依赖版本

运行镜像实际版本：Python 3.10.22、NumPy 1.26.4、ONNX Runtime 1.23.2、websockets 14.2、OpenAI SDK 2.8.1、Edge TTS 7.3.1、MCP 2.3.0、portalocker 3.2.0、PyYAML 6.0.3。

本地音频处理包为 FFmpeg `7:5.1.9-0+deb12u1` 和 libopus `1.3.1-3`。构建执行 `pip check`，未发现依赖冲突。完整实际版本分别保存为[运行依赖清单](issue-4-runtime-requirements.txt)（81 项）和[测试依赖清单](issue-4-test-requirements.txt)（90 项）；测试工具包括 pytest 8.4.2、pytest-asyncio 0.26.0、freezegun 1.5.5。

Silero VAD 使用真实 ONNX Runtime 载入仓库模型：`models/snakers4_silero-vad/src/silero_vad/data/silero_vad.onnx`，SHA-256 为 `2623a2953f6ff3d2c1e61740c6cdb7168133479b267dfef114a4a3cc5bdd788f`。本机远程 ASR 方案无需安装 Torch 或本地 FunASR 推理栈。

## 实服务检查结果

下列检查通过真实 HTTP／WebSocket 边界执行。Java API、MySQL、Redis 和 Python 语音进程均实际运行，设备身份由协议客户端模拟。

| 检查 | 实际结果 |
| --- | --- |
| 启动与 HTTP | 最终镜像健康；宿主机和容器内 HTTP 均返回 `200 / Server is running` |
| 真实服务凭据 | `POST /config/server-base` 返回 HTTP `200`、业务码 `0`，实际选择 `ASR_OpenaiASR`、`VAD_SileroVAD` |
| 错误服务凭据 | 同一入口返回 HTTP `200`、业务码 `401`；这是该上游过滤器的实际拒绝方式 |
| 未注册设备 | `POST /config/agent-models` 返回业务码 `10041` |
| 激活请求 | `POST /ota/activate` 返回 `202`；`POST /ota/` 发放六位激活码 |
| 待绑定设备 | 配置入口返回业务码 `10042` |
| 测试账号绑定 | 经 `POST /device/bind/{agentId}/{code}` 绑定；使用同一账号的 `GET /device/bind/{agentId}` 回读同一 MAC |
| 绑定后状态 | `/ota/activate` 返回 `200 / success`；OTA 不再返回激活信息，提供预期 WebSocket URL 和非空 token |
| 设备配置 | 实际智能体模型配置可读，ASR 与测试智能体一致，聊天记录关闭 |
| 错误 WebSocket token | 服务返回 `认证失败` 并关闭连接 |
| 正确 WebSocket token | 使用 Java OTA 签发的 token，Python 服务返回 `hello`、有效 session ID 和协商后的 Opus / 16 kHz / 单声道 / 60 ms 参数 |
| 会话提供者就绪 | 同一设备 token 和会话返回 `ready / providers`；错误 token 为 `401`，另一已认证 Client-Id 为 `403`，连接关闭后为 `404` |
| 消息交换 | 三次 WebSocket 控制帧 Ping/Pong，负载匹配；当前上游未启用应用层 JSON 心跳 |
| 测试数据清理 | 删除本轮智能体后，列表不再包含它；同一设备激活入口恢复 `202`，确认设备记录也已移除 |
| 原管理环境 | 页面、公共配置、匿名参数访问拒绝检查仍通过 |

最终镜像上两轮独立身份各 19 个步骤全部通过，第二轮在音频参数恢复后执行：

| 运行（UTC） | 设备 MAC | Client-Id | 结果 |
| --- | --- | --- | --- |
| 03:47:13–03:47:21 | `02:61:7a:e9:d8:4c` | `protocol-617ae9d84ce74afe864390e4b3689fba` | passed |
| 03:48:16–03:48:22 | `02:72:85:7f:a0:2e` | `protocol-72857fa02eaa4926abc1cc802b7efe74` | passed |

完整脱敏请求和响应见[首轮结果](issue-4-protocol-result.json)和[独立身份重复结果](issue-4-protocol-repeat.json)。实际服务密钥、管理 token、激活码和 OTA token 均不在证据中。设备 MAC、Client-Id、智能体 ID、session ID 和时间可用于关联本机私有日志。

额外使用错误服务密钥的配置副本执行最终客户端，HTTP 存活步骤通过，配置获取步骤停止，退出码为 `1`，结果为 `status: failed`，没有创建设备或智能体。见[失败记录](issue-4-rejected-settings-result.json)。这与每轮主动验证“错误密钥被拒绝”的通过步骤分别记录。

## 配置失败与恢复

`voice-config-check` 在一次性容器中实际启动 `python app.py`，使用临时数据目录、同一真实管理 API 和只读的正确配置副本。三个错误情形均以非零进程状态结束：

| 输入 | 实际诊断 |
| --- | --- |
| 缺少 `.config.yaml` | `找不到data/.config.yaml文件` |
| 缺少管理密钥 | `manager-api的url或secret配置错误` |
| 错误管理密钥 | `无效的服务器密钥` |
| 恢复正确配置 | 新的真实语音进程返回 HTTP `200 / Server is running` |

四项均通过，见[配置检查结果](issue-4-startup-result.json)。失败及恢复日志保存在本机私有目录，检查没有修改运行中服务的配置。

## 提供者初始化与实际调用范围

服务启动时实际初始化 ONNX VAD 和远程 ASR。握手后异步读取绑定设备的专属配置，并创建 Edge TTS、OpenAI 兼容 LLM、无意图识别和无记忆提供者。最终实际提供者类型、凭据状态及会话就绪响应见两轮协议证据；上游日志包含完整认证头，未直接提交日志。

`hello` 先于初始化完成返回，客户端还必须从经过认证的 HTTP 就绪入口确认当前会话为 `ready`。服务端等待异步音频通道初始化，包括本地 Opus 编码器，并保留配置、提供者和本地组件初始化的首个失败阶段；就绪结果不包含原始配置或异常详情。ASR 当前没有有效 API key，LLM／SLM／VLLM 仍是上游占位 key。上游允许这些提供者创建对象，LLM 会记录密钥警告；初始化通过不能用于宣称识别或生成调用可用。实际 ASR 和 LLM 调用受有效端点、模型配置及凭据条件限制，未在本任务中执行。

提供者回归通过实际管理 API 创建专属 TTS 模型，先将类型设为不受支持：设备已经收到真实 `hello`，就绪接口仍返回 `failed / providers`，完整客户端返回退出码 `1`。修正同一模型为 Edge TTS 后，新的独立设备会话返回 `ready / providers`，检查通过。最后删除测试智能体、设备及专属模型，见[提供者失败和恢复证据](issue-4-provider-result.json)。

另以管理 API 暂将 `xiaozhi` 参数中的输出采样率设为 Opus 不支持的 `22050` Hz，并重启实际语音进程。修复前（`367de09`）设备收到 `hello` 后仍被误判通过；等待 TTS／ASR 通道初始化 Future 后，最终镜像返回 `failed / components`、完整客户端退出 `1`。随后恢复原 `24000` Hz，按 JSON 内容读回核对，重启后第二轮 19 步检查通过。此负例仅在本机测试环境执行，原参数保存在本机私有备份，见[音频通道失败及恢复证据](issue-4-audio-channel-result.json)。

Edge TTS 不需要用户 API key；独立上游 TTS 用例实际访问了 Bing 语音服务并通过，但该用例仅断言输出文件存在。经设备协议传输的音频识别、模型问答、语音合成、麦克风输入、音箱播放及音质均保持待验证。当前使用上游本地测试账号；集市 OIDC、规范通权限和计费不在这条基线中。

## Python 测试

[完整输出](issue-4-python-tests.txt)：**129 passed, 5 skipped in 139.34s**。完整套件在修复后的工作树测试镜像执行，已核对最终固定版本镜像的服务源码 SHA-256 及依赖清单与该镜像完全一致，见[产物与资源明细](issue-4-artifacts.json)。另行验证时间工具文件为 7 passed；Python 编译检查、Node 语法检查和 Git diff 格式检查通过。

上游两个测试模块包含固定的模块级跳过，三项 LLM 测试因占位 key 跳过。测试镜像使用独立公开配置；上游默认模式替换部分提供者网络与管理配置加载，所以这套测试与上面的实服务协议证据分别解释。

首轮完整测试因测试容器关闭外网导致一个 Edge TTS 用例失败，结果为 128 passed / 5 skipped / 1 failed。恢复正常 Compose 网络后，该用例单独通过，完整套件也通过。该网络依赖已写入运行说明，真实音频内容与播放质量仍需后续验证。

## 资源观测与限制

最终协议检查及完整测试结束后的单次资源快照（UTC 2026-11-06 04:18:05），使用 `docker stats --no-stream`：

| 服务 | CPU | Docker 显示内存 / 限制 |
| --- | --- | --- |
| voice | 34.39% | 8.906MiB / 640MiB |
| web | 0.00% | 4.348MiB / 96MiB |
| api | 2.00% | 67.19MiB / 896MiB |
| redis | 0.61% | 3.871MiB / 192MiB |
| mysql | 0.42% | 16.22MiB / 640MiB |

当时宿主机可用内存约 2.20 GiB。语音进程状态为 `VmHWM=158008 kB`、`VmRSS=10316 kB`、`VmSwap=111652 kB`；容器 `memory.peak=324882432` 字节（约 309.8 MiB），`memory.swap.current=114364416` 字节。Docker 显示的当前内存扣除了部分缓存，且本机正在使用交换内存，不能将 8.906 MiB 解读为运行容量需求。容器峰值包含服务启动和容器内检查命令；没有持续采集构建峰值，没有并发压测。

首次依赖下载曾超时；补充下载缓存和镜像源后构建成功。首次启动定位到上游唤醒词模块必需的 `portalocker`，已将其固定版本纳入运行依赖。默认 ASR、认证和设备 URL 的原值已在本机私有备份中保存；正常停止只停止语音容器，数据与管理设置保留。

本记录覆盖 Fedora 本机隔离环境及协议模拟。服务器部署、局域网真机地址、固件兼容、真实音频链路和生产容量属于后续验证条件。

标准与规格双轴复审均无未解决问题；两项初始化漏报均已修复并完成真实负例／恢复验证，见[复审记录](issue-4-review.md)。
