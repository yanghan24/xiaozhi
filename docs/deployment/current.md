# 当前部署与公网入口

本页是查找小智服务端地址、部署版本和运行方式的固定入口。最近核验：2026-11-12 13:20（Asia/Hong_Kong），API 部署依据[PR #67 服务端部署记录](../verification/2026-11-11-pr67-deployment.md)，补发后重启及当前新旧凭据依据[历史证据复核](../verification/2026-11-12-api-restart-audit.md)；语音已按[PR #79 修复部署记录](../verification/2026-11-12-pr79-deployment.md)更新维护通道的异常／取消分类及最终会话检查，保留原系统提示词和私有配置。API 已包含 PR #67 本机解绑、PR #63 管理员补发设备凭据、PR #52、长期烧录凭据、设备账号查询与旧版停用，手机扫码页为 AI 集市风格，音箱名称优先显示集市昵称；语音为 PR #26 基础上的系统提示词派生版本。操作服务器前仍须检查实际运行状态。

2026-11-13 设备现场状态补充：用户确认 B 当前可用，关闭手机登录/绑定页面后仍能完成普通语音问答。设备侧已有步骤及本次确认已整理为 [#13 设备验收与服务器交接](../handoff/2026-11-13-issue13-device-to-server.md)。这项确认晚于 [PR #87 当时的连接失败记录](https://github.com/yanghan24/xiaozhi/blob/6a20c8c8de190ac9b8ce7eda212ed277fdf2245a/docs/verification/2026-11-12-pr84-pr85-continuation.md)；历史失败结论保留。当前服务/固件版本及剩余服务器检查由服务器侧核对，本页下列部署值仍对应各自标注的最近测量，不视为本次重新读取。

## 手机与音箱访问

| 用途 | 当前地址 |
| --- | --- |
| 公网 HTTPS origin | https://bridge.hncig.cn:18443 |
| 音箱 OTA 产品发现 | https://bridge.hncig.cn:18443/xiaozhi/ota/ |
| 设备控制状态（GET） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/state |
| 创建绑定/恢复请求（POST） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/requests |
| 停播确认（POST） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/state-acks |
| 本台归属账号与积分（GET） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/account |
| 本台退出（POST） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/sign-out |
| 本台退出并解除绑定（POST） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/unbind |
| 语音 WebSocket | wss://bridge.hncig.cn:18443/xiaozhi/v1/ |
| 手机绑定页 | 设备发码返回的 `https://bridge.hncig.cn:18443/xiaozhi/bind/{handle}` |

手机扫描音箱生成的有效二维码进入绑定页，通过集市确认身份后完成设备操作。公网根路径返回 404 是预期行为；没有可代替设备二维码的通用绑定首页。设备控制需要独立接入凭据，语音连接需要当前设备许可签发的产品令牌。OTA 地址用于产品发现，不代表已经提供固件下载。

## 部署版本与运行方式

| 项目 | 最近确认值 |
| --- | --- |
| Ubuntu / SSH | `jt@192.168.168.114` |
| API 源码 | `1b50709fd1cadfad966b12d4afedf78b1f279aa6`，PR #67 `71130c1` 与现网 PR #63 `7738647` 的组合版本；运行产物固定于该组合提交 |
| 部署目录 | `/home/jt/pr26-deployment-885918d` |
| Compose 文件 | 部署目录内 `private/compose.json` |
| 产品 API | `xiaozhi-pr26-api` / `xiaozhi-product-api:1b50709` |
| 产品语音 | `xiaozhi-pr26-voice` / `xiaozhi-product-voice:pr79-f830a8c`，PR #79 合并提交 `7864e25` 与受测 head `f830a8c` 文件树一致；在原维护镜像基础上同时更新 `product_maintenance.py` 与 `product_voice.py`，2026-11-12 12:58 单独重建，原配置和系统提示词保留 |
| 公网入口 | 既有 TLS Nginx，公网 18443 映射到 Ubuntu 443 |
| 管理页面 | `xiaozhi-pr26-web`，Ubuntu `127.0.0.1:18052`，经 SSH 隧道访问 |

外网烧录工作站继续使用已签发的长期烧录凭据；本次验证其有效状态及已撤销凭据的拒绝状态均保留。它不能登录管理页面。管理页面可用 `ssh -N -L 127.0.0.1:28052:127.0.0.1:18052 jt@192.168.168.114` 后访问 `http://127.0.0.1:28052/#/login`。

用户确认现有集市客户端已勾选 `balance:read` 后，已设置并保留 `CUSTOMER_OIDC_BALANCE_ENABLED=true`；不是通过开关证明客户端开通。历史账号可能需要扫码补授权，真实账号实时积分尚未验收。账号接口使用设备接入凭据，查询本台当前归属账号；旧 `sign-out` 保留归属和集市授权，仅停用本台许可。新 `unbind` 在本机明确确认后释放本台归属，停用并递增许可及归属代际；其他账号可扫描新二维码接手。配套组合固件 `860e045` 已安装到 B，用户确认立即停播、换账号绑定及两台均可回答；Ubuntu 17:17 只读回查确认停播 ACK 为 revision 18 / permission_version 13 / audio_stopped=true，随后当前状态为 ready / version 14 / ownership_version 3。详见[固件与现场验收记录](https://github.com/yanghan24/xiaozhi/blob/cd5dff6b49151a49eab461b9a5ffd013401c703b/docs/verification/2026-11-11-device-local-unbind-firmware.md)及[本轮只读结果](../verification/evidence/2026-11-11-pr67-deployment/b-stop-ack.json)。新路由与长期凭据管理路由为精确放行，普通管理登录、配置与内部 API 仍不公开。

服务、回归和设备协议验证在 Ubuntu 执行。授权 sudo 操作前按私有凭据说明使用已有凭据通道；本页不存放密码或密钥。在服务器部署目录查看状态：

```bash
cd /home/jt/pr26-deployment-885918d
sudo docker compose -f private/compose.json ps
```

启停或恢复前先读部署记录的[实际部署](../verification/2026-11-10-pr26-server-deployment.md#实际部署)、[备份与运维](../verification/2026-11-10-pr26-server-deployment.md#备份与运维)和[暂停与恢复原则](../verification/2026-11-10-pr26-server-deployment.md#暂停与恢复原则)。该记录还列出外部数据库、Redis、旧管理配置 API、代理可信地址与持久失效通知目录等依赖；仅启动产品 Compose 不能代替这些依赖的检查。JetLinks 保持停机。

PR #26 本次部署已完成服务端回归、公网协议检查和容器服务重启验证；其真实手机最终确认、产品固件麦克风到扬声器闭环、物理停播 ACK、Ubuntu 主机重启和并发容量尚未由该部署记录验收。另有 [P4 基础固件真实语音与恢复记录](../handoff/2026-11-10-device-firmware.md)，对应测试智能体和当时固件，不能直接视为 PR #26 产品绑定协议的验收。

2026-11-11 补充[单台已绑定设备验收](../verification/2026-11-11-pr26-main-sync.md)：固件 `2.2.6 / 00896ab`，用户确认扫码账号页、完整语音回答、退出约 1 秒停播、设备重启保持退出及原账号明确恢复后完整回答；服务端对应许可 `7 → 8 → 9`，退出 ACK 为 `audio_stopped=true`，归属保持不变。首次绑定、第二设备隔离、Ubuntu 主机重启和并发容量不在这次结果内。

2026-11-11 再补充[双机交付、中断恢复及隔离验收](../verification/2026-11-11-two-device-delivery.md)和[服务器核验](../verification/2026-11-11-pr52-server-acceptance.md)：两台独立凭据、B完整交付、三处进程中止续作和双机隔离通过，B许可版本4/revision5停播ACK已核对。上述结果不包含写入瞬间断电或历史单次重启根因定位。
2026-11-11 16:25（Asia/Hong_Kong）部署 PR #63 服务端：新增补发记录表与管理员 recovery 精确入口，已完成备份、受测产物核对及 56 项公网检查。未执行真实设备补发或 P4 写回。补发后不能退回不理解恢复记录的旧 API；暂停新增补发应撤回 recovery 路由并保留兼容 API、当前凭据和原密钥。详见[部署证据与恢复边界](../verification/2026-11-11-pr63-deployment.md)。

PR #67 本轮先在 Ubuntu 完成原候选 357 项及组合版本 362 项完整 Java 回归，再备份、部署 API 与 `unbind` 精确路由；69 项公网检查通过，保留 PR #63 的恢复路由与原私有配置。受测与运行 JAR 一致，仅 API 重建。详见[部署证据与回退边界](../verification/2026-11-11-pr67-deployment.md)。

2026-11-11 继续完成 [#42 真实 B 维护与补发写回](../verification/2026-11-11-device-identity-retention.md)：先验证 `f03d98d` 的应用更新、USB 复位及重新配网保留身份，再由管理员补发原档案的新凭据，P4 提交/读回、新凭据设备验证和后台完成回执均通过。后续另一任务将 B 应用更新为 `860e045`（ELF `a04fda6d…`），该构建已合入相同身份保护；只读备份及复位核对通过，用户确认原账号和完整语音回答正常。A 本轮未更新。旧凭据独立拒绝、后台归属/许可状态比对、服务重启及其余恢复场景仍待 Ubuntu 接续，#42 保持打开。

后续 Ubuntu 已独立验证旧凭据拒绝、新凭据 delivered 及当前状态，见 [PR #74 接续核验](https://github.com/yanghan24/xiaozhi/pull/74)。B 后来的解绑/换绑不能倒推补发瞬间归属保持。2026-11-12 已[核实并补档](../verification/2026-11-12-api-restart-audit.md)昨晚20:31的 API 重启成功证据：当时 B 许可14／归属3及两台设备、补发和硬件映射前后快照完全一致，原配置／密钥检查通过。原归档遗漏不应继续视为“没有 API 重启证据”；最初补发瞬间的历史比对、原重启后紧接实机确认及其他硬件恢复关卡仍未补齐。

2026-11-12 B 的 [OTA 与正常恢复出厂记录](../verification/2026-11-12-p4-ota-server.md)记录 r2 包校验、短时签名 HTTPS 和仅限 B 当前会话的本机维护通道。11:20 只下发一次；硬件方报告目标 ELF `364a1d6f…`、产品 bin 全量读回一致、`ota_0 → ota_1` VALID，身份/凭据及 `device_ctrl` 保持；用户确认扫码账号和完整语音正常。实时下载/写入/自动重启日志缺失，原未知传输记录保留且未重发。11:34 已撤销下载路由，旧链接本机 TLS 核验 404、发现仍为 200，私有产物保留。用户确认后续另做解绑/重新绑定，B 从许可17/归属5变为19/7；11:38 以19/7保存恢复出厂前基线。

随后硬件方确认备份、取消无重启、确认恢复并重新配网、目标固件/身份/凭据及整个 `device_ctrl` 保持、原账号及完整语音五项通过。12:21 Ubuntu [恢复后比对](../verification/evidence/2026-11-12-p4-ota-server/factory-reset-reconciliation.json)确认 B 仍为 ready / admitted=1 / local_enabled=1 / 许可19 / 归属7，归属/凭据、补发与硬件映射及 A 均不变；当时服务镜像/启动时间不变。本次 ready 状态正常恢复出厂通过，其余异常恢复和停用保持继续待验，#42/#13 保持打开。API 仍为 `1b50709`。bin 含原构建 IoT token，不提供匿名或永久下载链接。

2026-11-12 12:58 已部署 [PR #79 修复](../verification/2026-11-12-pr79-deployment.md)：维护通道的异常／取消结果分类及最终会话检查在 [15 项 Ubuntu 隔离回归](../verification/evidence/2026-11-12-p4-ota-server/maintenance-review-fixes.json)与[候选镜像内 15 项回归](../verification/evidence/2026-11-12-pr79-deployment/candidate-preparation.json)通过后，配套更新两个模块，仅重建语音。13:00 [只读核验](../verification/evidence/2026-11-12-pr79-deployment/post-deployment-audit.json)确认受测镜像 ID 与实际一致，B 仍为 ready／许可19／归属7，设备状态、补发、硬件映射及原 OTA 预留不变；API、数据库、Redis 与 TLS 入口未重建。API 内部健康与本机正常 TLS 发现均为 200；当时维护 status 返回 `no_unique_session`。随后用户[确认 B 已唤醒、原账号及一次完整回答正常](../verification/evidence/2026-11-12-pr79-deployment/post-deployment-functional-confirmation.json)，13:08 [只读核对](../verification/evidence/2026-11-12-pr79-deployment/post-functional-server-state.json)确认 status=`current`、MCP 就绪、上述设备状态和原预留保持，服务没有再次重建。本轮没有重启 API／Ubuntu 主机，没有公网 18443 新探测或再次下发 OTA；#42／#13 的其他验收继续保持打开。

13:20 的[新旧凭据独立复查](../verification/evidence/2026-11-12-api-restart-audit/current-credential-check.json)通过本机正常 TLS 设备状态入口确认旧凭据401／`INVALID_DEVICE_CREDENTIAL`、当前凭据200／`OK`，B仍为ready／许可19／归属7，记录快照保持。本次只读请求晚于当前 API 11:13启动，不是昨晚重启的即时检查；没有再次重启服务、补发或轮换凭据，也没有新的公网18443探测。

## 本地资料缺失或过时

本地分支/worktree 不随 GitHub 合并自动更新。找不到本页、入口仍指向历史报告，或文档状态互相矛盾时，先核对 GitHub 最新 `main` 的[本页](https://github.com/yanghan24/xiaozhi/blob/main/docs/deployment/current.md)和它引用的部署证据，再判断资料是否缺失。

无需切换工作分支即可只读获取：

```bash
gh api -H 'Accept: application/vnd.github.raw+json' 'repos/yanghan24/xiaozhi/contents/docs/deployment/current.md?ref=main'
```

随后用相同方式读取本页引用的证据文件。GitHub 暂时不可访问时，说明当前只能依据哪一版本地记录，恢复访问后再核实；保留原分支与未提交修改。

## 维护与历史边界

部署地址、版本或运行方式改变时，在归档日期报告的同一个文档 PR 中更新本页，链接新的验证证据，并复查 README 与 AGENTS.md 能到达本页。只有完成这条导航链核验，才算完成部署文档交付。日期报告保留当时事实；当前状态统一在本页维护。

[2026-11-06 独立测试部署流程](ubuntu-isolated-test.md)用于复现旧基线，其中旧镜像、端口及“公网映射未开通”等描述仅属于当时环境。历史文档与本页不一致时，以本页所链接的较新部署证据为准，并在执行前核对服务器。
