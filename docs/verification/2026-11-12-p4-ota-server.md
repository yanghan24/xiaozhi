# B 受控 OTA 安装与正常恢复出厂验收

2026-11-12 10:49 起（Asia/Hong_Kong），服务器 `jt@192.168.168.114`。接续 [PR #77](https://github.com/yanghan24/xiaozhi/pull/77)；本轮完成产物接收、状态快照、受控下载和本机维护通道。**11:20 仅下发一次；硬件方报告目标固件全量读回一致、`ota_0 → ota_1` 且身份/凭据保持，用户确认扫码账号及完整语音正常。随后正常恢复出厂的取消、确认重启、重新配网及功能核验通过，12:21 服务器比对仍为许可 19／归属 7，A 不变。** 原调用回执仍未知，11:20 下载/写入/自动重启没有实时日志；不重发。本次正常维护通过，不代表 #42 / #13 全部验收完成。

## 产物及私有存储

收到 `D:/AI/issue42-p4-a30f6a8-ota-r2.tar.gz`，归档大小 2,254,169 字节，SHA-256 为 `715e5188b13fff14fb192b730640e8ad1c5021518c0ff32d79e4bb945b8d423a`。这是包装修订 2，与 PR #77 原先记录的归档大小/hash 不同；四个归档文件均经 SHA256SUMS 核对，产品 bin 保持不变：

| 项目 | 固定值 |
| --- | --- |
| 候选源码 | `a30f6a8d3e47068004af0685d5c229cb7648cdae` |
| 板型 | Waveshare P4 WiFi6 Touch LCD 4.3，P4 rev 3.2，32 MB 物理 Flash、保留原 16 MB 布局；接续 PR #77 硬件记录 |
| B 原应用 | 用户/硬件前置证据为 `860e045`；升级后运行目标 ELF 由硬件方读回报告确认 |
| 产品 bin | 4,003,072 字节；`b34b7ccf64ca50d2ae6052e8bdd24a5abfc6bacb50d7fca03b1c8495a36a84b7` |
| 目标 ELF | `364a1d6f2d33a9fa0d1312956aa22daae0076c758a8d5ff4e433b0e7d52022a3` |
| 应用版本 | `2.2.6`，本轮不以版本字符串变化判断成功 |
| 服务器产物目录 | `/home/jt/issue42-p4-ota-20261112/p4-a30f6a8-r2`，私有目录 |

r2 元数据纠正了原包“无凭据”的宽泛说法：没有设备接入凭据、管理员登录或原始 Flash/NVS，但 bin 保留了原构建中的 IoT HTTP token，`public_distribution_allowed=false`。不将归档/bin 放 Git 或公开 release；服务器曾提供短时签名下载，安装确认后已撤销该路由。归档校验脚本 `/home/jt/issue42-p4-ota-stage.sh` 验证固定大小/hash、四个普通文件的允许清单和完整校验清单，不直接解包任意路径。

## 即时状态及服务

10:49:37 与 10:52:20 服务器私有快照保存在 `/home/jt/issue42-p4-ota-20261112/` 各时间目录。通过唯一 delivered 补发记录及硬件映射定位 B，不通过昵称猜测。B `admitted=1`、`local_enabled=1`、`reason=ready`、许可版本 17、归属版本 5；有归属账号，当前凭据匹配补发记录。两台设备的凭据 hash、归属、许可及硬件映射均已保存，原始标识不入库。

期间另一个网络维护任务修改 Docker 网桥，曾观察到 Docker failed，随后恢复。本轮没有修改 Docker 地址池或重启 Docker。上述两次私有状态逐字节一致；恢复后 API 内部 readiness 200、本机正常 TLS 域名验证下的发现入口 200。它们不替代硬件语音核验。脱敏[状态摘要](evidence/2026-11-12-p4-ota-server/state-snapshot.json)保留该时间点观察。

11:14:07 单独更新语音容器以启用下述维护通道，受测源代码提交 `497688809b81ce9c4da997d93d443e0e5bdfe91d`。基于原 `xiaozhi-product-voice:prompt-7f10cbd` 派生 `xiaozhi-product-voice:p4-maintenance-20261112`，仅覆盖两个 Python 文件，保留原系统提示词及私有配置。更新前核对现网 websocket_server.py SHA-256 与源码基线一致。API 保持 `xiaozhi-product-api:1b50709`；API、数据库、Redis、TLS 入口容器 ID 和启动时间均未改变。语音重建前后设备关键字段、补发和硬件映射一致，见[部署摘要](evidence/2026-11-12-p4-ota-server/control-deployment.json)。

## HTTPS 下载与停用

现有 TLS Nginx 增加一个精确产品文件路径，使用 secure_link 校验签名和到期时间。每个链接有效 15 分钟；缺失/错误签名 403，签名正确但过期 410，正确链接返回 `Content-Length: 4003072`、`Cache-Control: no-store`。访问与错误日志不记录能力 URL。URL 是持有者可用的短时下载凭证，下载层本身不证明设备是 B；B 的目标限制由维护通道单独执行。

Ubuntu 已使用私有 URL 完整下载并核对固定 SHA-256；公网 `bridge.hncig.cn:18443` 复核同样通过，保留 TLS 证书校验。公网复核经过临时 loopback SSH CONNECT 代理，已关闭，不宣称 Ubuntu 无代理公网出站已恢复。见[HTTPS 证据](evidence/2026-11-12-p4-ota-server/https.json)。能力 URL/签名密钥只保存在服务器私有 `download-private.json`，不进入本报告。

11:19 下发前执行 `/home/jt/refresh-download.sh` 重新生成 15 分钟链接，并经公网完整校验后使用。硬件确认目标已安装后，11:34 执行 `/home/jt/revoke-download.sh`，只移除此维护块并重载 Nginx，保留其他配置和私有规范产物。[撤销复核](evidence/2026-11-12-p4-ota-server/download-revocation.json)使用正常 TLS 域名/证书、连接 Ubuntu 本机 443，旧能力 URL 返回 404，产品发现仍为 200；该复核不是新的公网 18443 探测。下载路由已撤销，不为恢复出厂重新开放或再次下发。

## 受控下发通道

原语音镜像没有可直接使用的管理员下发入口。本轮新增 `core/product_maintenance.py`，仅在设置 `PRODUCT_MAINTENANCE_DIRECTORY` 和 `PRODUCT_MAINTENANCE_DEVICE_ID` 后启用；现网只允许映射确认的 B。Unix 目录 700、socket 600，使用 Linux SO_PEERCRED 拒绝非 root 对端，不发布 TCP/HTTP 管理入口。

查询/调用须对应唯一活跃的产品语音连接、同一 session_id 和当前许可版本。发送通过原 GuardedSocket，再次检查服务器权威许可；退出/解绑或旧会话不能绕过该闸门。通过同一个 MCP 客户端递增 ID，以 `withUserTools=true` 查询用户专用工具；响应由 RPC future 消费，不加入普通模型工具列表。升级调用只允许固定维护路径的有效 HTTPS 短时链接。发送前持久预留操作号；结果丢失时保留未知状态，不能自动重试。工具返回 true 只记为排队，绝不标记 OTA 完成。

部署前 12 项 Ubuntu 隔离容器测试通过，0 失败/错误/跳过；覆盖实际 Unix socket 权限/状态、陈旧 socket 恢复、目标和代际拒绝、发送前撤销、隐藏工具分页与 ID、非法 URL、操作去重与响应未知。外部设备/集市均由受控夹具代替，未借测试向实物下发；语法检查通过。这不代替现网 B 工具能力和真实安装结果。

11:14:34 实际 root socket 查询返回 `no_unique_session`，表示当时没有唯一活跃产品语音连接，不说明 B 不在线或不支持升级。用户随后确认已唤醒 B，11:19:04 再次查询，唯一会话、许可版本 17、MCP ready 均匹配，真实 `self.upgrade_firmware` 查询可用，见[最新查询摘要](evidence/2026-11-12-p4-ota-server/control-probe.json)。

## 11:20 单次调用及安装结果核对

11:19:43 已重新生成并经公网 TLS 完整验证当前能力 URL。记录用户“硬件方准备并记录，我转达就绪”和“已经唤醒B”的本次就绪信息后，再次采集 B/A 即时快照，核对 B 当前会话及工具，执行一次受控调用。操作号 `e029d00a3ade4616adebfce862108b73`，没有自动重发。

客户端[返回摘要](evidence/2026-11-12-p4-ota-server/dispatch.json)中的 `denied` 是部署时异常包装器的通用状态，**不能解释为命令确定未发送或设备明确拒绝**。11:20:45 的[操作审计](evidence/2026-11-12-p4-ota-server/dispatch-audit.json)确认发送前持久预留存在，状态 `reserved_result_unknown`，原 B 活跃会话已不在。此证据不能判定下载/写入/重启成功，也不能区分发送失败、回执丢失或升级过程中断开。

服务器[调用后状态](evidence/2026-11-12-p4-ota-server/post-dispatch-state.json)仍为 B ready / 许可 17 / 归属 5，新旧应用 ELF 不由该 API 状态证明。随后用户转述硬件方 11:28–11:30 的 Flash 读回和复位启动核验：运行目标 ELF `364a1d6f…`，`ota_0 → ota_1`，状态 VALID、CRC 有效，产品 bin 全量读回与目标一致；核验复位后联网，身份、接入凭据及整个 `device_ctrl` 保持一致。上述证据确认目标安装结果，但不补成缺失的 11:20 实时下载、写入和自动重启日志。

[安装核对记录](evidence/2026-11-12-p4-ota-server/installation-reconciliation.json)明确来源为用户转述的硬件报告；原始 Flash/UART 不进入 Git。服务器 11:33:09 的[安装后快照](evidence/2026-11-12-p4-ota-server/post-installation-state.json)与下发前 11:19:54 快照一致，包括两台设备的归属、凭据、许可、补发记录和硬件映射。另存安装核对记录，原 `reserved_result_unknown` 传输记录保留，没有删除预留或重试。

本轮另补真正经过 Unix socket 的异常重放测试：修正前可重复得到“已预留发送失败却返回 denied”；修正为 `result_unknown_do_not_retry` 后 13 项通过，0 失败/错误/跳过，见[测试摘要](evidence/2026-11-12-p4-ota-server/maintenance-tests.json)。这项返回分类修正已在分支准备，**当前部署仍为 `4976888` 的版本**，尚未重启语音部署修正；操作未知状态及防重试预留本来就已生效。测试没有再次向 B 下发。

## OTA 后功能与独立解绑/重新绑定

用户确认“扫码账号正常，完整语音正常”，见[功能确认](evidence/2026-11-12-p4-ota-server/post-ota-functional-confirmation.json)。11:33–11:35 之间 B 的许可从 17 变为 19、归属版本从 5 变为 7，最终归属账号和凭据相同，A 不变，见[独立版本比较](evidence/2026-11-12-p4-ota-server/post-qr-version-comparison.json)。用户进一步确认这期间执行过“退出并解除绑定”并重新绑定；因此不能声称 OTA 后扫码/语音全程保持 17/5，也不把这次独立操作记为 OTA 导致归属变化。

## 正常恢复出厂：通过

11:38:20 [服务器即时快照](evidence/2026-11-12-p4-ota-server/pre-factory-reset-state.json)确认 B ready / admitted=1 / local_enabled=1 / 许可 19 / 归属 7，原归属和补发凭据一致，A 不变。私有 `factory-reset-before-snapshot` 指针固定为 `/home/jt/issue42-p4-ota-20261112/20261112T033820Z`，不随后续 latest-snapshot 更新；[基线摘要](evidence/2026-11-12-p4-ota-server/factory-reset-baseline.json)保留准备时硬件结果待回传的历史状态。

用户随后转述硬件方五项全部通过，另存[恢复出厂核对记录](evidence/2026-11-12-p4-ota-server/factory-reset-reconciliation.json)。硬件实际操作时间未提供，不用服务器采集时间代替；原始 Flash/NVS/UART 留在硬件方私有存储，服务器端未独立读取原始硬件日志。

| 硬件核验 | 结果 |
| --- | --- |
| 当前应用、NVS 和 `device_ctrl` 私有备份 | 已完成 |
| 设置 → 恢复出厂 → 取消 | 未重启，Wi-Fi 正常 |
| 再进入恢复出厂 → 恢复并重启 | 已记录重启并进入配网，重新配网成功 |
| 固件及受保护状态读回 | 目标 ELF `364a1d6f…`、`ota_1`；应用、身份、接入凭据及整个 `device_ctrl` 与备份一致 |
| 恢复后扫码和语音 | 原账号正常，完整语音回答正常 |

12:21:20 Ubuntu 采集[恢复后快照](evidence/2026-11-12-p4-ota-server/post-factory-reset-state.json)，与固定的 11:38:20 基线比较：

- B 的归属 issuer/subject、凭据 hash、准入、本地调用许可、ready 状态、许可版本 19 和归属版本 7 均一致；当前凭据继续匹配 delivered 补发记录，仍为唯一硬件映射。
- 两台设备关键字段、全部补发记录和硬件映射的已记录快照整体一致，A 的全部记录字段不变。
- API、语音、数据库、Redis 和 TLS 入口均运行；镜像和启动时间不变。API/语音仍为 `1b50709` / `p4-maintenance-20261112`，本轮仅只读核对，没有为分类修正重启服务。

12:22:14 保存的核对记录中全部服务器比较项通过，前后快照指针及原始证据均保留。由硬件反馈和服务器比对共同确认，本次 ready 状态下的正常恢复出厂验收通过。

## 仍未覆盖的验收范围

本次没有验证停用状态下恢复出厂后的停用保持、物理停播、异常身份丢失后的完整授权补发、相关中断/迟到请求边界；历史补发瞬间的归属/许可证据及补发后的服务重启也不能由本轮比较补证。11:20 实时 OTA 过程日志仍缺失。#42 / #13 的其余适用验收项继续单列，本轮正常维护通过不关闭这两个任务。

## 恢复边界

服务器不再执行 queue-upgrade；已有 dispatch-private.json、预留和安装核对记录均保留。语音回退使用部署摘要中备份的 Compose 作为字段参考，仅恢复本次语音 image/environment，保留其他后续配置；仅重建语音服务。下载维护块已单独撤销。回退语音不会回退已安装固件或已执行的恢复出厂；私有原档案、密钥及备份必须保留。本轮服务器没有补发/轮换凭据或主动解绑；硬件侧独立解绑/重新绑定如上记录。#42 / #13 保持打开。
