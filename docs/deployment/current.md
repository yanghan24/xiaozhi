# 当前部署与公网入口

2026-11-13 17:27（Asia/Hong_Kong）已部署[数列连续播放修复](../verification/2026-11-13-voice-optimization.md)：当前语音 `sha256:578524ae8454c0d69334844a563af35df2c6c7bd73ed550865155fd09176b686`，将纯数列保持在一次合成中，保留缓存唤醒和增量音频。候选 99 项单测、35 个 WebSocket 场景通过；受控真实 Edge 验证未丢失或重排音频。仅重建语音，其他容器、配置和持久挂载保持，TLS 发现 200、无授权 WSS 1008。用户此前确认唤醒立即出声，补丁后的数数连贯性待现场复验。[部署／回退证据](../verification/evidence/2026-11-13-voice-optimization/numeric-deployment.json)。

2026-11-13 16:54（Asia/Hong_Kong）完成[唤醒与首段语音优化部署](../verification/2026-11-13-voice-optimization.md)：语音运行镜像 `sha256:923a86f241f40aed526e3f9a5d0156f1a34de7224733122d774b4d06e79fc5ff`，固定问候缓存、最早安全首段与 Edge 增量播放生效。受测镜像通过 99 项单测及 31 个 WebSocket 场景；部署只更改语音 image，保留部署前其他任务更新的 API／管理页面，MySQL／Redis／入口容器及原配置、持久挂载保持。TLS 发现 200，无授权 WSS 1008；用户确认唤醒立即出声，但数数在首个数字后长停顿；连续性补丁正在验证。[部署与回退证据](../verification/evidence/2026-11-13-voice-optimization/deployment.json)。

2026-11-13 16:35（Asia/Hong_Kong）已上线后台 OTA 上传修复（PR #106，合并提交 `1fedfd0`）：API `sha256:2828d49aab09ae80489c27c1d3e388b68ad47fb67e2c865847740412c639dbbe`、管理页面 `sha256:4bd43d3269f1c6156fce2186d7cd3173b2f0d1291cc506787bbffc29b011b5df`，语音保持 `sha256:f931a8ea6c7442019a0ef64254adb377cf855678974db7f9ff3899d5928599f2`。生产备份已保存；12f2ff1 通过小智后台上传并校验为 `backend_upload/verified`，SHA、ELF、P4 revision 301–399 与 16MiB 分区布局一致。全部在线预检已完成但 A、B 均离线，可推送数为 0，未发送 OTA。详见 [deployment.json](../verification/evidence/2026-11-13-ota103-deployment.json)。

2026-11-13 15:57（Asia/Hong_Kong）完成[唤醒与首音延迟诊断](../verification/2026-11-13-voice-latency.md)：短时启用元数据计时后已恢复原语音镜像 `sha256:f931a8ea6c7442019a0ef64254adb377cf855678974db7f9ff3899d5928599f2`，临时计时关闭；Compose 与诊断前逐字节一致，API／管理页／入口容器未重建，TLS 发现 200、无授权 WSS 1008。两轮实测定位到模型与首段语音合成等待；本次没有部署延迟优化。

2026-11-13 14:43（Asia/Hong_Kong）只读核对确认服务器已运行合并 PR #93 后的语音镜像 `xiaozhi-product-voice:ota99-26b1703`，digest 为 `sha256:f931a8ea6c7442019a0ef64254adb377cf855678974db7f9ff3899d5928599f2`；API digest 为 `sha256:292846c40a6279bf720ccc75eed08796eb042b287350b0b13e048d04a9fa500d`，管理页 digest 为 `sha256:6f36cacdb1ab2d8eb7234eb8cf07e2f8996be29179e3ea6a7c1f34ac70aaa9c5`。部署目录仍为 `/home/jt/pr26-deployment-885918d`，本次没有覆盖后续维护/ASR 变更或重复重启。PR #93 合并后部署和 B 现场复验证据见 [#14 合并后部署与复验](../verification/2026-11-13-issue14-postdeploy.md) 及其 [deployment.json](../verification/evidence/2026-11-13-issue14-postdeploy/deployment.json)。

2026-11-13 12:32（Asia/Hong_Kong）已部署 [#14 唤醒后无声修复](../verification/2026-11-13-issue14-wake-start.md)：源码 `69398b3`，镜像 `xiaozhi-product-voice:issue14-wake-69398b3` / `sha256:22c9e780db3fe00d2f7d78982b6d84cfc6f32d7f7679744f0f6d6ac193a47585`。旧镜像复现配套监听误取消问候，修复后66项测试及13个WebSocket场景通过。仅重建语音、平滑重载Nginx，knowledge配置及B许可24／归属7、ASR market／revision5等状态保持。用户已确认完整问候；随后两道知识题与普通介绍补测均完成真实麦克风输入和完整播放，三题功能验收通过。实际转写采用人工语义对应，本轮工具正文未留存，内容依据审查边界详见报告。PR #93尚未合并，#14在合并前保持打开。此次独立回退仅恢复9ff语音镜像，保留knowledge。

2026-11-13 12:08（Asia/Hong_Kong）已部署 [#14 普通回答与自动知识库续答候选](../verification/2026-11-13-issue14-live-continuation.md)：语音源码 `5f01f35`，镜像 `xiaozhi-product-voice:issue14-5f01f35` / `sha256:9ff0a3a4dd5d74d15c7206f2064211d92137dbb5447a8716416fecf65f253c35`。候选镜像66项测试、10个集成场景和三题真实模型／MCP／Edge音频链通过；两题回答已逐项核对资料依据。仅重建语音，并平滑重载Nginx；B许可24／归属7、ASR market／revision5及其他业务状态保持。知识库仅开放获准B与当前账号组合；B实际麦克风／扬声器三题验收仍待用户确认，#14和草稿PR #93尚未签收／合并。下列更早记录保留各自历史范围。

2026-11-13 11:17（Asia/Hong_Kong）[#18单台OTA验收通过](../verification/2026-11-13-issue18-ota-preparation.md)：B本轮12f2ff1同版本重装的UART证明完整写入、自动重启、ota_1→ota_0、目标ELF及有效标记；真实产品会话恢复，许可24/归属7与有效凭据保持，A不受影响。11:15已撤销本轮下载路由（目标404、发现200），下方11:10的限时链接已提前失效。原传输回执未知记录保留且没有重发，安装确认另存；#19继续普通/MCP问答复测。

2026-11-13 11:10（Asia/Hong_Kong）[#18单次下发记录](../verification/2026-11-13-issue18-ota-preparation.md)：已修复后端重建后Nginx缓存旧语音地址导致的502（仅平滑重载，配置不变），B重新建立会话后单次下发12f2ff1 OTA。回执未知，操作号`198bf4223d1740cb95fd1ea9dbf314c7`，未重发；当前凭据200/OK、许可24/归属7保持，安装结果待硬件日志。新签名下载有效至11:24:30，完成后须撤销。API和语音当前均为后续任务的`asr-switch`镜像。

2026-11-13 11:05（Asia/Hong_Kong）[#18下发前只读复查](../verification/2026-11-13-issue18-ota-preparation.md)发现现网已由后续任务切换为 `xiaozhi-product-voice:asr-switch`，实际镜像ID `sha256:66045ff8230ad09d705c938c380a50ab26f19e9e4469aaf5761aab942a066897`。该镜像保留 #18 维护路径更新，16项隔离维护回归通过；本任务没有执行这次镜像切换。B许可24/归属7，尚未下发OTA。

2026-11-13 10:49（Asia/Hong_Kong）最新维护更新：[#18准备与部署记录](../verification/2026-11-13-issue18-ota-preparation.md)确认现网语音更新为 `xiaozhi-product-voice:issue18-12f2ff1`，基于现场实际 `rtasr-3739aad` 镜像，仅增加新固件精确维护路径。API与身份数据不变；新路径短时HTTPS下载验证后已撤销，尚未向B下发OTA。下方更早的签收及部署说明保留历史范围。

本页是查找小智服务端地址、部署版本和运行方式的固定入口。最近核验：2026-11-13 12:32（Asia/Hong_Kong），当前已部署[集市/讯飞语音识别切换](../verification/2026-11-13-asr-provider-switch.md)，下述旧记录保留历史依据，API 部署依据[PR #67 服务端部署记录](../verification/2026-11-11-pr67-deployment.md)，补发后重启及当前新旧凭据依据[历史证据复核](../verification/2026-11-12-api-restart-audit.md)；语音已按 [rtasr-llm 切换记录](../verification/2026-11-13-rtasr-llm.md)更换集市语音转写模型，保留 PR #79 维护修复、原系统提示词和私有配置。API 已包含 PR #67 本机解绑、PR #63 管理员补发设备凭据、PR #52、长期烧录凭据、设备账号查询与旧版停用，手机扫码页为 AI 集市风格，音箱名称优先显示集市昵称；语音为 PR #26 基础上的系统提示词派生版本。操作服务器前仍须检查实际运行状态。

2026-11-13 设备现场状态补充：用户确认 B 当前可用，关闭手机登录/绑定页面后仍能完成普通语音问答。设备侧已有步骤及本次确认已整理为 [#13 设备验收与服务器交接](../handoff/2026-11-13-issue13-device-to-server.md)。这项确认晚于 [PR #87 当时的连接失败记录](https://github.com/yanghan24/xiaozhi/blob/6a20c8c8de190ac9b8ce7eda212ed277fdf2245a/docs/verification/2026-11-12-pr84-pr85-continuation.md)；历史失败结论保留。服务器接续已完成，#13 已关闭，详见下方签收；各历史报告保留各自日期和版本。

2026-11-13 （Asia/Hong_Kong；服务器快照10:05）完成 [#13 服务器签收](../verification/2026-11-13-issue13-server-closeout.md)：API 1b50709、语音 pr79-f830a8c 及运行产物已核对；B 为 ready／许可24／归属7，当前 delivered 凭据有效、前驱旧凭据被拒绝。用户确认当前 B 已刷入12f2ff1，关闭手机页后仍可完整问答；固件版本来自用户确认，非新读回。PR #76 秘密保护专项原结果已取回，1项通过且未跳过。#13 已按限定范围关闭，#14/#18、#42及扩展矩阵不随之验收；本次没有部署或重启服务。

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
| API 源码 | `7a935aba50c0e177e79badcb17a45e6a7ae43308`，从 main 基线加入集市/讯飞识别切换；运行产物固定于该实现提交，代码审阅入口为 [PR #94](https://github.com/yanghan24/xiaozhi/pull/94) |
| 部署目录 | `/home/jt/pr26-deployment-885918d` |
| Compose 文件 | 部署目录内 `private/compose.json` |
| 产品 API | `xiaozhi-pr26-api` / `xiaozhi-product-api:asr-switch`，11:02:36 重建；#18 下发前只读复查确认 |
| 产品语音 | `xiaozhi-pr26-voice` / `sha256:578524ae8454c0d69334844a563af35df2c6c7bd73ed550865155fd09176b686`；17:26 重建，缓存问候、增量播放及数字序列连续合成。代码入口 [PR #107](https://github.com/yanghan24/xiaozhi/pull/107)；17:27 服务核验通过，数数连贯性待实机复验 |
| 公网入口 | 既有 TLS Nginx，公网 18443 映射到 Ubuntu 443 |
| 管理页面 | `xiaozhi-pr26-web` / `xiaozhi-manager-web:asr-switch`，Ubuntu `127.0.0.1:18052`，经 SSH 隧道访问；模型配置 → 语音识别可切换来源 |

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

2026-11-13 10:24 已完成 [rtasr-llm 切换与核验](../verification/2026-11-13-rtasr-llm.md)：候选镜像内 10 项接口测试、真实 Java 权限／失效上报及正式设备模拟语音链路通过；真实集市合成语音识别成功。仅重建语音，设备身份／归属／许可及原私有配置保持，其他现网容器未重建。本机网卡 443 的正常 TLS 发现 200、无授权 WSS 关闭码 1008；实机发问确认尚待用户执行。

2026-11-13 11:03 已完成[集市/讯飞切换部署与核验](../verification/2026-11-13-asr-provider-switch.md)。当前选择集市、讯飞密钥已加密配置，保存后下一段转写生效；363 项 Java、14 项语音接口及两种来源的模拟设备链路通过。重建产品 API/语音/管理页面；设备身份/归属/许可及原配置保持，数据库/Redis/TLS 入口未重建。镜像 digest 与启动时间见部署证据。本轮未做物理音箱验收。

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
