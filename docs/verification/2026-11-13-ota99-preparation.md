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

## 已暂存的具体配置与上线剩余条件

生产 Compose 为 `/home/jt/pr26-deployment-885918d/private/compose.json`，服务名为 `product-api`、`product-voice`、`product-web`。[stage-config.py](../../scripts/ota99/stage-config.py) 已从现场当前文件生成最小 delta，暂存在 **`/home/jt/ota99-config-stage-2249e3c-v2`**（0700、root 私有），没有安装到生产路径。[配置摘要](evidence/2026-11-13-ota99-preparation/staged-config.json)固定原始/候选配置 hash 和镜像；API/UI 未最终固定，所以 `deployable=false`。

1. 暂存 API 加入 `product.ota.bridge-url=http://product-voice:8097/internal/product-ota/v1/control`、独立 bridge/download 秘密和 `download-origin=https://bridge.hncig.cn:18443`；保留当前实际 Spring 配置，语音配置同一 bridge 秘密及 origin。两个新秘密只生成在暂存区，尚未启用。Compose 将语音 identity 网络 IPv4 固定为现场当前 `172.26.0.4`，bridge 仅监听此地址 `8097`，不监听另一 application 网络地址、不发布主机端口；生成器核对实际网络/地址及 #14 基底，不符则拒绝。部署前仍须再次核对该地址由当前语音独占，替换同一服务后保持固定地址。持久 `/app/data/product-ota` 对应的 root 所有 0700 目录仍须在部署窗口创建。
2. 暂存公网 TLS 仅新增严格格式的签名 artifact GET 路由，适配真实别名 `product-api`，明确转发 HTTPS/真实入口地址，关闭查询参数日志及临时缓存；原有路由/证书及精确可信代理保留。暂存管理配置仅新增精确 firmware 导入路径的 300 秒读取期限，沿用现有动态上游，其他请求仍为 30 秒。管理页面仍在私有 SSH 隧道入口。
3. Ubuntu `docker compose config --quiet` 以及两个临时、无主机端口的 Nginx 容器 `nginx -t` 均通过；测试仅为语法/DNS解析，没有对后端发请求或加载生产服务的新配置。初次生成器因现场管理配置缩进不同而安全拒绝，改成匹配现有 location 行后在新目录重跑成功；旧空目录保留，不覆盖现场文件。

最终 API JAR/UI 固定后，用生成器的 `--api-image`/`--web-image` 参数建立最终暂存版本，核对当前原始配置/镜像 hash、生产私有备份、密钥安装及回退参数；不得直接把当前 `deployable=false` 暂存文件当作上线包。审批针对具体最终产物/配置，本记录不是上线批准。配置验证未代替实际 TLS 签名下载和完整后台登录导航验收。

## 最终配置补充

修复候选 `4beb8cf` 的 API/页面镜像固定后，已在 Ubuntu 重新生成 `/home/jt/ota99-config-stage-4beb8cf`；[最终脱敏摘要](evidence/2026-11-13-ota99-preparation/final-staged-config.json)为 `deployable=true`，表示三个镜像和最小配置均已固定，不表示已经上线。原始现场配置及 #14 镜像保护仍通过，Compose 与两套临时 Nginx 的语法检查再次通过。

- API：`sha256:292846c40a6279bf720ccc75eed08796eb042b287350b0b13e048d04a9fa500d`。
- 页面：`sha256:6f36cacdb1ab2d8eb7234eb8cf07e2f8996be29179e3ea6a7c1f34ac70aaa9c5`。
- 语音：保留上述组合候选 `sha256:4cf6a3006dc8274e4dd9b9bba1f15748be52b3cd3e7e0eb3ff11208ba4bd08d6`。

没有生产改动。上线窗口内仍须重新核对现场镜像与配置 hash、执行协调备份、创建持久目录并安装密钥，随后执行正常 TLS/签名下载和完整后台页面验收。

## 合并前同步最新主线

用户批准合并并上线后，主线新增 PR #93 的初始化等待、退出兼容与输出日志保护修复，基线更新为 `66d77ee`。在合并提交 `26b1703` 上重新准备组合语音候选 `xiaozhi-product-voice:ota99-26b1703`，固定 ID `sha256:f931a8ea6c7442019a0ef64254adb377cf855678974db7f9ff3899d5928599f2`，替代此前语音候选；API/页面源码及镜像保持。

[最新组合证据](evidence/2026-11-13-ota99-preparation/combined-voice-latest-main.json)：83/83 项测试和22/22个正式WebSocket场景通过，无失败、错误或跳过，包含最新初始化、退出及日志回归；全部7个覆盖模块在镜像中逐字节核对。配置生成器改为必须显式传入语音镜像，避免误选旧候选。现场版本/配置只读复核通过，生产私有范围备份已完成于 `/home/jt/ota101-rollout-20261113/backup-before`；这些不等同于已执行上线。

## 指定设备与验收边界

用户指定 **A** 为实机目标，并报告 A 当前为 `12f2ff1`；B 也是该产物。当前目录候选同为 `12f2ff1`，不得为了验收强制重装。最终系统信息/ELF一致时应跳过，不能靠版本字符串推定安装成功。现有 #18 的 B 重装证据属于旧单台维护通道，不能替代本次管理页面流程。

当前可验证的是同产物排除与不下发；真实新安装验收等待另一个兼容且已核验的不同产物。届时只对 A 做明确目标验收，固定当前分区/槽位、备份与 UART，保存下载、写入、自动重启、目标 ELF/实际运行、身份/凭据/归属/许可保持和未选 B 隔离证据。本轮没有读取 A/B 的真实工具信息、下发 OTA 或获取新的身份快照；这不是 #100 扩展范围的阻塞。
