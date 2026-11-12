# B 受控 OTA 的 Ubuntu 准备

2026-11-12 10:49 起（Asia/Hong_Kong），服务器 `jt@192.168.168.114`。接续 [PR #77](https://github.com/yanghan24/xiaozhi/pull/77)；本轮完成产物接收、状态快照、受控下载和本机维护通道。**11:20 已向 B 尝试一次升级调用，结果未知、未重试；真实 OTA、应用槽/身份读回及恢复出厂尚未验收。** 用户安排硬件方记录串口，随后确认已唤醒 B。

## 产物及私有存储

收到 `D:/AI/issue42-p4-a30f6a8-ota-r2.tar.gz`，归档大小 2,254,169 字节，SHA-256 为 `715e5188b13fff14fb192b730640e8ad1c5021518c0ff32d79e4bb945b8d423a`。这是包装修订 2，与 PR #77 原先记录的归档大小/hash 不同；四个归档文件均经 SHA256SUMS 核对，产品 bin 保持不变：

| 项目 | 固定值 |
| --- | --- |
| 候选源码 | `a30f6a8d3e47068004af0685d5c229cb7648cdae` |
| B 原应用 | 用户/硬件证据为 `860e045`，本轮没有串口读回 |
| 产品 bin | 4,003,072 字节；`b34b7ccf64ca50d2ae6052e8bdd24a5abfc6bacb50d7fca03b1c8495a36a84b7` |
| 目标 ELF | `364a1d6f2d33a9fa0d1312956aa22daae0076c758a8d5ff4e433b0e7d52022a3` |
| 应用版本 | `2.2.6`，本轮不以版本字符串变化判断成功 |
| 服务器产物目录 | `/home/jt/issue42-p4-ota-20261112/p4-a30f6a8-r2`，私有目录 |

r2 元数据纠正了原包“无凭据”的宽泛说法：没有设备接入凭据、管理员登录或原始 Flash/NVS，但 bin 保留了原构建中的 IoT HTTP token，`public_distribution_allowed=false`。不将归档/bin 放 Git 或公开 release；服务器只准备短时签名下载。归档校验脚本 `/home/jt/issue42-p4-ota-stage.sh` 验证固定大小/hash、四个普通文件的允许清单和完整校验清单，不直接解包任意路径。

## 即时状态及服务

10:49:37 与 10:52:20 服务器私有快照保存在 `/home/jt/issue42-p4-ota-20261112/` 各时间目录。通过唯一 delivered 补发记录及硬件映射定位 B，不通过昵称猜测。B `admitted=1`、`local_enabled=1`、`reason=ready`、许可版本 17、归属版本 5；有归属账号，当前凭据匹配补发记录。两台设备的凭据 hash、归属、许可及硬件映射均已保存，原始标识不入库。

期间另一个网络维护任务修改 Docker 网桥，曾观察到 Docker failed，随后恢复。本轮没有修改 Docker 地址池或重启 Docker。上述两次私有状态逐字节一致；恢复后 API 内部 readiness 200、本机正常 TLS 域名验证下的发现入口 200。它们不替代硬件语音核验。脱敏[状态摘要](evidence/2026-11-12-p4-ota-server/state-snapshot.json)保留该时间点观察。

11:14:07 单独更新语音容器以启用下述维护通道，受测源代码提交 `497688809b81ce9c4da997d93d443e0e5bdfe91d`。基于原 `xiaozhi-product-voice:prompt-7f10cbd` 派生 `xiaozhi-product-voice:p4-maintenance-20261112`，仅覆盖两个 Python 文件，保留原系统提示词及私有配置。更新前核对现网 websocket_server.py SHA-256 与源码基线一致。API 保持 `xiaozhi-product-api:1b50709`；API、数据库、Redis、TLS 入口容器 ID 和启动时间均未改变。语音重建前后设备关键字段、补发和硬件映射一致，见[部署摘要](evidence/2026-11-12-p4-ota-server/control-deployment.json)。

## HTTPS 下载与停用

现有 TLS Nginx 增加一个精确产品文件路径，使用 secure_link 校验签名和到期时间。每个链接有效 15 分钟；缺失/错误签名 403，签名正确但过期 410，正确链接返回 `Content-Length: 4003072`、`Cache-Control: no-store`。访问与错误日志不记录能力 URL。URL 是持有者可用的短时下载凭证，下载层本身不证明设备是 B；B 的目标限制由维护通道单独执行。

Ubuntu 已使用私有 URL 完整下载并核对固定 SHA-256；公网 `bridge.hncig.cn:18443` 复核同样通过，保留 TLS 证书校验。公网复核经过临时 loopback SSH CONNECT 代理，已关闭，不宣称 Ubuntu 无代理公网出站已恢复。见[HTTPS 证据](evidence/2026-11-12-p4-ota-server/https.json)。能力 URL/签名密钥只保存在服务器私有 `download-private.json`，不进入本报告。

硬件就绪时执行 `/home/jt/refresh-download.sh` 重新生成 15 分钟链接，重新运行 `/home/jt/issue42-p4-ota-public-check.sh` 核对后再使用。停止提供下载使用已准备的 `/home/jt/revoke-download.sh`：只移除此维护块并重载 Nginx，保留其他任务可能增加的配置和私有规范产物。该撤销脚本仅通过语法检查，尚未实际撤销；链接过期后的 410 已实测。

## 受控下发通道

原语音镜像没有可直接使用的管理员下发入口。本轮新增 `core/product_maintenance.py`，仅在设置 `PRODUCT_MAINTENANCE_DIRECTORY` 和 `PRODUCT_MAINTENANCE_DEVICE_ID` 后启用；现网只允许映射确认的 B。Unix 目录 700、socket 600，使用 Linux SO_PEERCRED 拒绝非 root 对端，不发布 TCP/HTTP 管理入口。

查询/调用须对应唯一活跃的产品语音连接、同一 session_id 和当前许可版本。发送通过原 GuardedSocket，再次检查服务器权威许可；退出/解绑或旧会话不能绕过该闸门。通过同一个 MCP 客户端递增 ID，以 `withUserTools=true` 查询用户专用工具；响应由 RPC future 消费，不加入普通模型工具列表。升级调用只允许固定维护路径的有效 HTTPS 短时链接。发送前持久预留操作号；结果丢失时保留未知状态，不能自动重试。工具返回 true 只记为排队，绝不标记 OTA 完成。

部署前 12 项 Ubuntu 隔离容器测试通过，0 失败/错误/跳过；覆盖实际 Unix socket 权限/状态、陈旧 socket 恢复、目标和代际拒绝、发送前撤销、隐藏工具分页与 ID、非法 URL、操作去重与响应未知。外部设备/集市均由受控夹具代替，未借测试向实物下发；语法检查通过。这不代替现网 B 工具能力和真实安装结果。

11:14:34 实际 root socket 查询返回 `no_unique_session`，表示当时没有唯一活跃产品语音连接，不说明 B 不在线或不支持升级。用户随后确认已唤醒 B，11:19:04 再次查询，唯一会话、许可版本 17、MCP ready 均匹配，真实 `self.upgrade_firmware` 查询可用，见[最新查询摘要](evidence/2026-11-12-p4-ota-server/control-probe.json)。

## 11:20 单次调用：结果未知

11:19:43 已重新生成并经公网 TLS 完整验证当前能力 URL。记录用户“硬件方准备并记录，我转达就绪”和“已经唤醒B”的本次就绪信息后，再次采集 B/A 即时快照，核对 B 当前会话及工具，执行一次受控调用。操作号 `e029d00a3ade4616adebfce862108b73`，没有自动重发。

客户端[返回摘要](evidence/2026-11-12-p4-ota-server/dispatch.json)中的 `denied` 是部署时异常包装器的通用状态，**不能解释为命令确定未发送或设备明确拒绝**。11:20:45 的[操作审计](evidence/2026-11-12-p4-ota-server/dispatch-audit.json)确认发送前持久预留存在，状态 `reserved_result_unknown`，原 B 活跃会话已不在。此证据不能判定下载/写入/重启成功，也不能区分发送失败、回执丢失或升级过程中断开。

服务器[后续状态](evidence/2026-11-12-p4-ota-server/post-dispatch-state.json)仍为 B ready / 许可 17 / 归属 5，新旧应用 ELF 不由该 API 状态证明。硬件方需提供串口与当前应用槽/ELF 读回；此前 `ota_0` 和目标 `ota_1` 只是待验证前后条件，不能据会话断开自动打勾。

本轮另补真正经过 Unix socket 的异常重放测试：修正前可重复得到“已预留发送失败却返回 denied”；修正为 `result_unknown_do_not_retry` 后 13 项通过，0 失败/错误/跳过，见[测试摘要](evidence/2026-11-12-p4-ota-server/maintenance-tests.json)。这项返回分类修正已在分支准备，**当前部署仍为 `4976888` 的版本**，尚未重启语音部署修正；操作未知状态及防重试预留本来就已生效。测试没有再次向 B 下发。

## 下一步与恢复

1. 先由硬件方确认本次是否真正下载/写入/重启、当前 ELF/应用槽及身份/凭据保持。服务器不再执行 queue-upgrade，已有 dispatch-private.json 和预留记录必须保留，不因返回 denied 就删除它们重试。
2. 若已安装目标，补服务器前后即时状态、现场扫码及完整语音，再开始恢复出厂的取消/确认/重新配网。若未安装，先定位实际失败阶段，再安排新的显式尝试。
3. 后续新尝试须重新协调硬件就绪、检查当前镜像和会话、更新并验证短时下载能力。`/home/jt/issue42-p4-ota-queue-upgrade.sh` 已要求就绪记录新鲜、当前真实工具存在和相同到期时间的公网校验成功；不得绕过已有操作保护。

语音回退使用部署摘要中备份的 Compose 作为字段参考，仅恢复本次语音 image/environment，保留其他后续配置；仅重建语音服务。下载维护块用上述撤销脚本单独移除。回退语音不会回退已安装固件或已执行的恢复出厂；私有原档案、密钥及备份必须保留。此次未刷新/补发设备凭据、解绑/换绑或执行恢复出厂，不关闭 #42 / #13。
