# Issue #5：服务与协议 A 层验收

2026-11-06，Asia/Hong_Kong。复用已经部署的 Ubuntu `192.168.168.114` 独立项目 `xiaozhi-ubuntu-test`，完成初次部署、两轮协议验证及两次同设备服务重启检查。开发电脑仅用于源码整理和 SSH 操作。实机、主机重启和容量验收继续由后续任务完成。

## 版本与运行条件

#4 已合并为 `afc9ca9e03ce85bf6ebb5fb8943060ccb54a5349`。部署来源仍为 `6e354edf292c3df14fd783c5dc7314a4402e3b74`；以下比较退出 0，确认运行代码和基线检查没有差异：

```bash
git diff --quiet 6e354ed afc9ca9 -- main scripts compose.yaml compose.voice.yaml deploy/voice
```

镜像来源、摘要、网络源修改和初次验收见 [Ubuntu 部署记录](2026-11-06-ubuntu-deployment.md)。本次保持镜像、容器和数据卷，不重新初始化账号或密码。语音镜像仍使用原有 `working-tree` revision，不能将源码一致性核对描述成新版本镜像构建。

检查入口为 [check-device-persistence.py](../../scripts/check-device-persistence.py) 的 prepare、verify、cleanup 三个阶段，按 [Ubuntu runbook](../deployment/ubuntu-isolated-test.md#重启证据与恢复) 配合 Compose stop/up 执行。verify 只读回并重连原设备，不重新绑定；外部 Docker 证据独立证明五个服务确实停止并重新启动。

## 验收项与证据

| #5 验收项 | 结果与证据 |
| --- | --- |
| 工具、配置、端口与资源预检，缺失条件可定位 | [部署预检与操作条件](../deployment/ubuntu-isolated-test.md#执行前复查)；[启动错误配置检查](evidence/2026-11-06-ubuntu/startup-result.json)覆盖缺配置、缺密钥、错密钥及恢复 |
| 独立数据环境初始化并完成管理和协议检查 | [管理页面登录、配置保存与刷新读回](evidence/2026-11-06-ubuntu/manager-browser.json)，[协议首轮](evidence/2026-11-06-ubuntu/protocol-round1.json) 19 项；手动验证码输入有记录 |
| 同一配置和设备绑定跨服务重启保留 | [本轮准备](evidence/2026-11-06-issue5-repeat/prepare.json) 12 项、[重连验证](evidence/2026-11-06-issue5-repeat/verify.json) 11 项；快照一致且变化项为空 |
| 同一版本按文档重复启动检查 | [前轮重启](evidence/2026-11-06-ubuntu/restart-result.json)与[本轮重启](evidence/2026-11-06-issue5-repeat/restart-summary.json)均确认原五个容器的镜像和挂载不变、StartedAt 改变；[本轮完全停止](evidence/2026-11-06-issue5-repeat/restart-stopped.json)有独立记录 |
| 可说明的检查入口、失败非零退出及超时 | 持久化脚本复用已有 HTTP/WS 检查；[缺少 state](evidence/2026-11-06-ubuntu/negative-missing-state.json)和[拒绝覆盖 state](evidence/2026-11-06-ubuntu/negative-existing-state.json)按预期失败；网络及启动等待有超时 |
| 停止、重启和清理分别说明，保护其他资源 | runbook 提供对应命令；[本轮 cleanup](evidence/2026-11-06-issue5-repeat/cleanup.json) 3 项通过，仅清理本轮标记对象；JetLinks 保持停止，卷及私有配置保留 |
| 登录拒绝、服务认证和缺配置回归 | 既有 Ubuntu 浏览器拒绝用例、启动检查及[本轮协议](evidence/2026-11-06-issue5-repeat/protocol.json) 19 项通过；提供者错误恢复见[记录](evidence/2026-11-06-ubuntu/provider-result.json) |
| 记录资源占用与限制 | [Ubuntu 全量 API 源码编译资源](evidence/2026-11-06-issue5-repeat/build-resources.json)及[本轮空闲运行快照](evidence/2026-11-06-issue5-repeat/resources.json)；采样范围与限制见下节，不代表容量通过 |
| 对应父规格 A 层、交付脱敏报告 | 上述各项覆盖 A 层启动、配置、认证、协议、持久化和重复执行；真实数据库、缓存、管理 API 与 voice 均参与，设备由协议客户端模拟 |

本轮管理 HTTP 存活、公开配置初始化和匿名拒绝再次通过，浏览器原来保存的 `baseline.acceptance.1793956177540` 在重启后仍读回 `manager-write-read-verified`。本轮没有重新执行浏览器注册，也没有以新登录替代原配置持久化证据。

源码没有新增运行逻辑变更。既有 Ubuntu 完整回归为 Java 206 项通过、Python 129 passed / 5 skipped，见[汇总](evidence/2026-11-06-ubuntu/regression-summary.json)；本轮重跑的是服务重启、持久化和协议检查，没有将之前套件结果记为本轮新执行。

## 构建资源补充

历史镜像构建未保存明确的构建期资源采样，因此本轮在 Ubuntu 使用已有固定 API 测试镜像中的编译器、源码和依赖缓存，新建无数据挂载的一次性容器，执行 `mvn -B -ntp clean package -DskipTests`。`clean` 删除已有 target，实际重新编译 316 个主源码文件和 27 个测试源码文件，构建成功；该命令跳过测试执行，不能计为套件重跑。

构建总观测时间 16.33 秒，Maven 自报 13.850 秒；约每两秒采样，运行中观察到最高内存 422.1 MiB、CPU 251.82%（多核累计百分比），容器内存上限 1400 MiB。这些是采样观测值，不保证覆盖瞬时峰值，也不代表前端/Python 镜像的历史构建消耗。完整采样、镜像 ID、磁盘前后可用量和产物 SHA-256 见上述 JSON。

生成的 JAR 为 85301022 字节，留存在服务器私有验证目录；没有替换运行中的应用。临时构建容器已清理，原五个服务及数据库不受影响。可复现的核心命令如下，执行前选取未使用的容器名，原始日志留在私有目录，并另行周期采集该容器的 `docker stats --no-stream`：

```bash
sudo docker run --pull never --name xiaozhi-build-check-UNIQUE --memory 1400m \
  xiaozhi-manager-api-tests:6e354edf292c3df14fd783c5dc7314a4402e3b74 \
  mvn -B -ntp clean package -DskipTests
```

确认退出状态、日志中的源码编译与 `BUILD SUCCESS`，按需导出该容器的 `/build/target/xiaozhi-esp32-api.jar` 后，仅删除这个专属构建容器。

## 测试阶段 TTS

用户确认先使用微软 Edge TTS，沿用现有 `TTS_EdgeTTS`。本轮在真实 Ubuntu voice 容器使用 `zh-CN-XiaoxiaoNeural` 合成测试句：MP3 21888 字节、24000 Hz 单声道、3.648 秒，ffprobe 和 FFmpeg 解码通过，见[合成证据](evidence/2026-11-06-issue5-repeat/edge-tts.json)。生成音频留在服务器私有目录，不提交音频文件。

该结果只证明 Edge TTS 实际合成与解码，不代表已听验音质、音箱播放或集市 TTS 验收。ASR/LLM 真实推理、设备网络入口、二维码绑定及最终集市语音调用继续单独验证。当前设备入口未发布，管理和语音端口保持服务器回环地址；SSH 通道可用不等于手机/音箱可达。
