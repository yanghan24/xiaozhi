# #99 在线 OTA 隔离候选与恢复准备

2026-11-13，Ubuntu `jt@192.168.168.114`。本轮只做候选构建、隔离回归和合成数据恢复演练，没有部署、重启生产服务、修改生产配置/数据库或调用真实设备。#99 真实安装验收仍未完成。离线补发、空闲等待、停用/未绑定维护属于 #100，不构成本轮阻塞。

## 现场基线与候选

读取 [current](../deployment/current.md)、其链接的 #18 / ASR / 初始部署记录及 GitHub 最新 main 后，重新只读核对容器。现网语音已由其他任务更新为 `xiaozhi-product-voice:issue14-wake-69398b3`，镜像 `sha256:22c9e780db3fe00d2f7d78982b6d84cfc6f32d7f7679744f0f6d6ac193a47585`，启动时间 `2026-11-13T04:32:14.520271114Z`；API 仍为 `asr-switch`、`sha256:e3453400512165dbf1be0ab6c4fcc2c579d94801dec614f1bbafdf40ed5b9add`。这些只读结果不代表本任务实施过 #14 部署。

[候选脚本](../../scripts/ota99/prepare-voice-candidate.sh)固定上述实际 #14 镜像作为基底，仅加入 `2249e3c` 的 `product_ota_bridge.py`，对从现网导出的 `websocket_server.py` 应用 [OTA 生命周期补丁](../../scripts/ota99/websocket-lifecycle.patch)。基底/导出文件 SHA 不一致立即终止；不从旧 integration 源码重建整个语音服务。#14 的 ProductConversation 初始化、撤销失效、音频与唤醒行为保留。

候选 `xiaozhi-product-voice:ota99-2249e3c` 的固定 ID 为 `sha256:4cf6a3006dc8274e4dd9b9bba1f15748be52b3cd3e7e0eb3ff11208ba4bd08d6`。Ubuntu 私有材料位于 `/home/jt/ota99-voice-candidate-2249e3c`。标签不是最终整套上线产物：API 正在修复批量预检/排队过期问题，最终 API 与 UI 尚须在修复集成后固定。

## 已通过

[组合语音证据](evidence/2026-11-13-ota99-preparation/combined-voice.json)：在候选镜像内，以无外部网络、只读根文件系统、临时配置和合成设备运行 **79 项测试，0 失败/错误/跳过**，包含原 #14 的 66 项与 OTA 的 13 项；另有 **13 个正式 ConnectionHandler 场景**全部通过，涵盖唤醒、重复 start、abort 后下一段、普通/知识问答、权限撤销、工具失败及设备工具。真实外部模型/MCP 请求为 0。原 #14 检查脚本来自其已受测归档；OTA 桥接测试通过真实本机 HTTP/WebSocket 边界验证。

[恢复演练](evidence/2026-11-13-ota99-preparation/backup-drill.json)：独立、无网络的临时 MySQL 容器内，备份并恢复 2 MiB 全字节范围固件 BLOB，SHA256 一致；未知操作、批次中断记录和 SQLite WAL 中的活动锁保持；对话正文表未进入备份。生成的回退脚本通过 `bash -n`，默认执行因保护条件不满足而拒绝。容器随演练结束清理。没有拿生产数据作恢复实验。

本分支没有补做生产构建页面的登录/菜单导航验收。主集成任务的真实组件浏览器验证另存，不应将组件夹具等同于完整生产 bundle 的登录验证；也没有创建生产管理员或读取真实管理凭据。

## 可复用恢复材料

- [backup.py](../../scripts/ota99/backup.py)：一次 MySQL 一致事务备份受控表的结构和数据，显式 `--hex-blob`；`dc_*` 包含固件、预览、操作及批次表。复制 OIDC/身份/交付密钥、当前 Compose、管理与公网代理配置及语音配置到 0700 私有目录；原密钥和新增 OTA 密钥均须保留。对 `product-voice`、`product-ota` SQLite 使用在线 backup API 并校验 integrity；保留原 maintenance JSON 预留，不复制 Unix socket。原部署 backup.py 本来已按 `dc_` 前缀选择表，不能误称它完全漏掉 OTA 表；本脚本补充的是显式 BLOB 编码、新 journal、代理配置和固定镜像证据。
- [prepare-rollback.py](../../scripts/ota99/prepare-rollback.py)：从实际备份的三个镜像 ID 生成脚本；先核对当时预期的候选 ID，防止覆盖并行任务。只修改当前 Compose 的 image 字段，不恢复过时的完整 Compose。仅允许 **从未下发 OTA** 的窗口，要求已停止发布并安全移除新增下载路由、通过 Nginx 校验，且操作表为空；已有预留时拒绝退回旧 API。保留新增表、原秘密、所有日志和锁。后端重建后重载入口以刷新 DNS，随后还须做正常 TLS/授权语音检查。
- [verify-backup.py](../../scripts/ota99/verify-backup.py)：本轮合成恢复演练的可重复脚本。生产备份与恢复没有执行，回退服务也没有实际演练。

多存储备份不等于跨 MySQL/SQLite 原子快照：部署窗口先停止新发布，等待活动派发工作器结束，再做协调备份。恢复备份不得覆盖后续未知操作来允许重发。已有真实下发时保留兼容 API、下载路由/密钥及原锁，先逐台核对；服务回退不回退设备固件。

## 上线仍需完成的具体配置

生产 Compose 为 `/home/jt/pr26-deployment-885918d/private/compose.json`，服务名为 `product-api`、`product-voice`、`product-web`。只在最终候选固定后准备当前文件的最小 delta：

1. API 加入 `product.ota.bridge-url=http://product-voice:8097/internal/product-ota/v1/control`、独立 bridge/download 秘密和 `download-origin=https://bridge.hncig.cn:18443`；原身份/ASR配置保持。语音配置相同 bridge 秘密、origin、端口和持久 `/app/data/product-ota` 0700 目录。现网语音连接两个私有 Docker 网络；绑定 `0.0.0.0` 会被两网可达，不能宣称单网隔离。需明确选择私网绑定方案，禁止发布主机端口或公网 bridge 路由。
2. 公网 TLS 仅新增严格格式的签名 artifact GET 路由，适配真实别名 `product-api`，关闭查询参数日志及临时缓存。保留现有路由/证书及精确可信代理 `172.26.0.7`（应用前重新核对实际 IP）。管理页面仍只在私有 SSH 隧道入口；套用已验证的精确导入路由超时配置，不能只验证绕过代理的直连端口。
3. 以最终 API JAR/UI 和本组合语音候选准备具体差异、生成两个独立秘密、生产私有备份及完整恢复参数。当前尚未生成或改动任何生产秘密、目录、配置或最终 API/UI 镜像。审批应针对届时可审阅的具体产物/配置，不把本准备记录当作上线批准。

## 指定设备与验收边界

用户指定 **A** 为实机目标，并报告 A 当前为 `12f2ff1`；B 也是该产物。当前目录候选同为 `12f2ff1`，不得为了验收强制重装。最终系统信息/ELF一致时应跳过，不能靠版本字符串推定安装成功。现有 #18 的 B 重装证据属于旧单台维护通道，不能替代本次管理页面流程。

当前可验证的是同产物排除与不下发；真实新安装验收等待另一个兼容且已核验的不同产物。届时只对 A 做明确目标验收，固定当前分区/槽位、备份与 UART，保存下载、写入、自动重启、目标 ELF/实际运行、身份/凭据/归属/许可保持和未选 B 隔离证据。本轮没有读取 A/B 的真实工具信息、下发 OTA 或获取新的身份快照；这不是 #100 扩展范围的阻塞。
