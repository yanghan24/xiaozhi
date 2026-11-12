# PR #84 / #85 接续：B 身份补发、写回与中断续作验收

2026-11-12（Asia/Hong_Kong），接续 [PR #84](https://github.com/yanghan24/xiaozhi/pull/84) 的设备方交接及 [PR #85](https://github.com/yanghan24/xiaozhi/pull/85) 的单键身份丢失场景。B 已从 `identity_v1` 缺失状态恢复到原产品，完成唯一一次新凭据补发及两个持久检查点的中断续作。20:55 后台已确认 delivered，独立重启后的身份与固件复核通过；**最终使用验收失败**：用户反馈“正在同步登录状态”、无法唤醒，尚未完成原账号扫码和完整语音。

本轮设备协议和后台检查均在 Ubuntu `jt@192.168.168.114` 执行；当前 Windows 仅提供 COM3 串口和 SSH 转发。没有重复删除身份、正常 OTA、恢复出厂或历史 API 重启。

## 环境与固定产物

| 项目 | 受测值 |
| --- | --- |
| API | `xiaozhi-product-api:1b50709`，镜像 `429f8577b4559b27e7dacb558db0e4f53321faed2e2eb4f70798bef1f47ecb63` |
| 语音 | `xiaozhi-product-voice:pr79-f830a8c`，镜像 `19070d27ddd2332d43799c3caff669a6a131f180f98d500ef37824c3cc1bd7ca` |
| B | Waveshare ESP32-P4 WiFi6 Touch LCD4.3；P4 rev3.2；物理32MB；原16MB分区布局；ota_1／VALID |
| 固定产品 | 源码 `a30f6a8d3e47068004af0685d5c229cb7648cdae`；bin 4,003,072字节，SHA256 `b34b7ccf64ca50d2ae6052e8bdd24a5abfc6bacb50d7fca03b1c8495a36a84b7` |
| 产品 ELF 标识 | `364a1d6f2d33a9fa0d1312956aa22daae0076c758a8d5ff4e433b0e7d52022a3`，来自精确原发布bin中的描述符；未伪造缺失的原ELF文件 |
| 普通维护bin | SHA256 `18dbe4c1df33e03f613cbf7e815e97439e7300d62781e8a8dfb1163c76c37b07` |
| 原32MB备份 | SHA256 `ed75c1af3b5c8286ddc8529518f0bd498697b372c324edbef2819cbfbfb524a5`，本轮重新校验；另在新恢复journal内保存本轮应用及启动前缀备份 |
| 参考PR固定head | #84 `89d9f1623618985ccc4a72d2d92346553f37eaec`；#85 `7450e9aa4139e568dcc7a043231ff8ef00a59aec` |

API、语音、数据库、Redis和TLS入口的镜像及启动时间保持。恢复验收的服务器协议检查使用 Ubuntu 本机443、正式域名证书验证及公网origin的Host。后续连接诊断另验证了 Ubuntu 到正式公网18443的产品发现；这一结果不能证明 B 所在 Wi-Fi 到该入口的连接正常。

## 执行结果

| 时间 | 检查 | 结果与证据 |
| --- | --- | --- |
| 19:55、20:32 | 服务器前置条件 | 两次15项均通过：[首次检查](evidence/2026-11-12-pr84-pr85-continuation/server-readiness.json)、[恢复前复查](evidence/2026-11-12-pr84-pr85-continuation/server-readiness-before-recovery.json)。原档案、前驱恢复、原账号、A、配置、产物及正常管理员登录一致。 |
| 20:27–20:32 | 串口修复与实物复核 | [官方驱动修复](evidence/2026-11-12-pr84-pr85-continuation/serial-driver-repair.json)后，[硬件前置检查](evidence/2026-11-12-pr84-pr85-continuation/physical-preflight.json)通过。同一P4/C6、原分区/活动槽/启动区/资源、产品设备端MD5及启动ELF一致。 |
| 20:33–20:39 | 缺身份拒绝正常使用 | [UART及用户现场结果](evidence/2026-11-12-pr84-pr85-continuation/missing-identity-observation.json)：`device_id=null`，已联网，缺少已登记身份的origin检查拒绝；用户尝试唤醒没有反应，屏幕显示“账号信息暂不可用”。捕获期无崩溃/看门狗信号，未进入听音/正常对话。 |
| 20:42 | 新凭据保存后、写入前中断 | [writing检查点](evidence/2026-11-12-pr84-pr85-continuation/checkpoint-writing.json)通过。持久journal已保存唯一的新补发号、写入号、备份及新身份；未取得设备写入回执。旧凭据401，新凭据状态200；提前complete为409／DEVICE_VERIFICATION_PENDING。 |
| 20:46–20:49 | 身份提交后、产品还原前中断 | 原生stored回执与新身份独立NVS读回一致；当前Wi-Fi、锚点及其他非天气缓存记录保持。[written检查点](evidence/2026-11-12-pr84-pr85-continuation/checkpoint-written.json)通过，后台仍为pending且未发生device-check；提前complete仍被拒绝。 |
| 20:55 | 同journal恢复原产品并完成 | 精确原产品应用全量读回一致，保护区域复核；产品自行用新凭据完成device-check，管理员随后complete，journal与后台均为delivered。没有用测试端主动调用device-check代替实物。 |
| 20:56 | 凭据、旧回执和重放边界 | [完成后验收](evidence/2026-11-12-pr84-pr85-continuation/recovery-delivered-and-boundaries.json)的14项状态检查、10项协议请求全部通过，详见下表。 |
| 20:57–21:02 | 独立重启与实际使用 | [硬件复核](evidence/2026-11-12-pr84-pr85-continuation/independent-reboot-hardware.json)通过：新身份、固定产品及锚点/当前Wi-Fi/其他记录保持。用户[现场反馈](evidence/2026-11-12-pr84-pr85-continuation/functional-failure.json)为“正在同步登录状态”、无法唤醒，原账号扫码和完整语音未通过。 |
| 21:22 | 使用失败后的后台复核 | [服务器快照与协议复查](evidence/2026-11-12-pr84-pr85-continuation/server-after-functional-failure.json)仍通过；B ready／许可22／归属7，新恢复delivered，A和服务配置保持。后台ready不能替代实机正常使用。 |

两次中断均为到达持久检查点后退出恢复进程、再次使用同一journal续作，**不是写入瞬间真实断电测试**。新的恢复记录只增加一条，许可 `21 → 22`；原设备ID、P4/C6映射、客户归属和归属版本7保持，admitted=1、local_enabled=1、reason=ready。A的业务状态、原交付和历史恢复记录保持。

| 完成后请求 | HTTP／协议码 |
| --- | --- |
| 前驱旧凭据读取设备状态 | 401／INVALID_DEVICE_CREDENTIAL |
| 新凭据读取设备状态 | 200／OK，ready／allowed |
| 当前恢复回执 | 200／OK，delivered，不返回identity |
| 同一恢复操作重放 | 200／OK，返回与journal完全相同的新身份 |
| 同一写入回执重复complete | 200／OK，结果与当前回执一致 |
| 不同写入号complete | 409／IDEMPOTENCY_CONFLICT |
| 旧恢复GET、旧恢复complete、旧恢复重放 | 均409／STATE_CHANGED |
| 原交付迟到complete | 409／STATE_CHANGED，使用既有有效长期烧录工具凭据 |

各阶段检查后都复核快照，未改变当前凭据、归属、A或原操作记录。旧回执不能将新代际恢复成旧代际。

## 本轮发现及处理

Windows原Microsoft通用串口驱动下，8KB读取可重复丢字节；4096字节公共分区表曾仅收到3783字节，而设备端MD5正确。降速、增大接收缓冲及限制单帧窗口均未解决；相同SSH/RFC2217路径的合成回环4097字节完整一致。安装匹配`VID_1A86&PID_55D3`、微软签名有效的WCH官方2.2.2026.5驱动后，原失败测试与完整实物核对均通过。驱动包来源、摘要及签名记录见[证据](evidence/2026-11-12-pr84-pr85-continuation/serial-driver-repair.json)。

交接后用户确实重新配过网络。当前Wi-Fi记录及天气缓存与下午快照不同；原身份锚点和其他记录一致。恢复保留本轮核实后的网络配置，未覆盖回旧配置，旧快照保留为历史证据。

首次写后比较把解析器的排序返回值与未排序的保存文件直接比较，触发校验失败。按namespace/key/encoding统一排序后，实际值完全一致；独立身份读回和原生回执已先行通过。见[比较修正记录](evidence/2026-11-12-pr84-pr85-continuation/nvs-comparison-reconciliation.json)。处理期间未重复创建恢复或额外写Flash，原恢复引擎未修改。

## 最终使用阻断与待接续

实际状态日志为 `unknown → starting → activating → idle`。设备已经进入待机，但控制请求在 `connect` 阶段出现Network／Cancelled、HTTP status=0，没有取得可供正常使用的同步结果；不能把界面“同步中”当作仍处于activating状态。原始状态计数已按实际 `State: previous -> current` 格式复核。

同次启动的普通HTTPS与产品发现能够完成；Ubuntu另经正式公网18443、正常TLS验证，按B实际硬件请求得到正确的 `https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/`。本轮也发现入口关闭access日志，不能据空日志声称服务器没有收到某个请求。当前尚未确定控制连接失败的根因。

用户确认B连接“其他Wi-Fi”。尝试手机热点对照时，同一部手机同时承担热点与配网；分步关闭热点后，用户仍无法连接B发出的配网热点，因此尚未得到有效的第二网络对照。这台Windows电脑只有有线网卡，不能直接代替手机连接设备热点。后续需要可成功连接B热点的另一台设备完成配网对照，或继续取得更详细的无线连接证据。不得通过再次补发、写旧身份、跳过TLS校验或重复恢复出厂掩盖这一阻断。

普通重启复测（21:30–21:33）仍未恢复正常使用：[启动与状态记录](evidence/2026-11-12-pr84-pr85-continuation/ordinary-reboot-retest.json)确认新身份/原产品和Wi-Fi联网正常，无panic；[具体连接证据](evidence/2026-11-12-pr84-pr85-continuation/connection-failure-summary.json)捕获2次唤醒词识别，随后2次语音WebSocket连接超时（`0x8006`／32774），并反复出现控制连接错误。用户再次确认账号页仍同步/不可用、没有正常回答。仍为同一Wi-Fi，没有完成第二网络对照；不能把短暂进入connecting当作语音验收通过。完整日志仍有时间未设置告警，尚无时间同步完成的证据。

[最终服务器检查](evidence/2026-11-12-pr84-pr85-continuation/server-final.json)仍为ready／许可22／归属7、当前新恢复delivered，新凭据有效、旧凭据拒绝，A、原操作记录和服务版本/启动时间保持。配网[被动采集](evidence/2026-11-12-pr84-pr85-continuation/provisioning-observation.json)收到2174字节UART，但没有匹配到关联/地址分配事件，不能据此确认手机或B热点哪侧故障。

## 账号显示变化后的追加排查（22:02–22:24）

用户随后反馈“账号信息暂不可用”，并观察到普通重启后短暂出现账号信息、很快变成“正在同步登录状态”。[被动采集](evidence/2026-11-12-pr84-pr85-continuation/account-follow-up-passive.json)取得7663字节UART，记录6次控制连接失败（status=0）及一次语音连接超时`0x8006`；同一时刻Ubuntu经正常TLS访问公网产品发现为200。不能把账号页的这两种提示直接当作身份再次丢失。

[完整普通启动采集](evidence/2026-11-12-pr84-pr85-continuation/account-follow-up-boot.json)确认设备仍能加载身份，原产品ELF一致，Wi-Fi连接一次、采集期没有断开或panic。启动约16.8秒和18.5秒各出现一次取消的控制请求，之后采集期未再记录连接失败；这不能证明状态轮询或许可续期正常。用户在这次采集后仍确认“仍在同步，不能完整回答”，使用验收继续失败。

随后在Ubuntu只读读取B的当前NVS并核对设备端MD5，使用实际读出的身份，经Ubuntu LAN443和正式公网18443分别请求`state`和`account`。[四项检查均为200／OK](evidence/2026-11-12-pr84-pr85-continuation/account-follow-up-api.json)：许可仍为22、ready／allowed，账号名称及积分字段均有返回。身份及凭据没有被重新补发或写回。当前账号和设备凭据可用，故障范围收窄到B的持续连接与后续状态刷新。

原产品的许可有效期为60000毫秒；`Model::Tick`在到期后撤销使用许可，`AccountView`将相应状态显示为同步中，应用随之暂停语音。续期没有持续完成可以解释用户观察，但本轮没有直接量到“账号显示→同步”的时间和对应许可到期瞬间；仍未区分网络路径与固件连接的根因，也没有完成第二网络对照。

本轮采集工具有四处修正：Ubuntu Python3.8需使用兼容的路径校验；首次完整启动采集的串口控制线初值有误，未取得有效日志，修正为原bench顺序后取得上述启动报告；身份中的空`control_origin`按产品契约允许回退到OTA同源，不能判为损坏；首次本机接口探测误用了未监听的127.0.0.1:443，按原验收工具改为192.168.168.114:443后两项均通过。原始尝试及NVS/UART仍保留在Ubuntu私有目录`/home/jt/pr84-pr85-screen-diagnostics/`，没有为处理这些工具问题修改产品或身份。

## 证据与恢复边界

原始身份、网络值、UART、Flash/NVS及新恢复journal仅存放在Ubuntu私有目录；本报告只提交脱敏结论。前驱仍使用PR #84明确标记来源的新接续快照，没有补造历史原journal。

凭据已经轮换，旧健康NVS和旧journal只能保留审计，不能写回旧身份作为回退。撤回本文档不会撤销设备或服务器已经发生的恢复。后续应沿用本轮delivered的新journal及新凭据。

管理员`device_disabled`维护路径、最初补发瞬间历史归属/许可证据缺口、真实写入断电等仍不属于本轮结论。#42/#13保持打开，PR #84/#85未合并；本轮不据部分验收关闭它们。
