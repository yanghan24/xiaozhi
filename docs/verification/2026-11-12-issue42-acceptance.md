# #42 续验：停用状态下恢复出厂

2026-11-12（Asia/Hong_Kong），Ubuntu `jt@192.168.168.114`。本轮完成 B 的“停用 → 恢复出厂 → 重新配网仍停用 → 原账号恢复 → 完整语音”验收，并通过六项真实重试／迟到请求检查。服务器许可 `19 → 20 → 21`、归属版本始终为7，身份、凭据、原归属及 A 保持；异常身份丢失恢复等其余关卡继续待验，[#42](https://github.com/yanghan24/xiaozhi/issues/42)／#13 保持打开。

## 本轮环境与基线

准备时发现实际语音服务已由另一任务于 13:19 更新为 PR #81 候选，而最新 main 的部署入口仍记录 #79。本轮查阅固定 [PR #81 head 的部署报告](https://github.com/yanghan24/xiaozhi/blob/6dc375c6acef1a04aa83e7bba3284776447703ea/docs/verification/2026-11-12-wake-word-interrupt-v2.md)与其审计，并核对运行镜像、两个 #79 模块 hash；没有切换或重启服务。

| 项目 | 本轮实际值 |
| --- | --- |
| 产品 API | `xiaozhi-product-api:1b50709`，启动 11:13:39 |
| 产品语音 | `xiaozhi-product-voice:pr81-75290c4-pr79-f830a8c`，启动 13:19:43 |
| 语音镜像 ID | `sha256:b19fcbf3f44aafeead93bf4e7e9cf670165bc19b03439253f3340bdfbd225a8d` |
| `product_maintenance.py` SHA-256 | `d5544dc41dbc62047b3da581ccb74b038a7b2c16ae4a82e598e598d414a91277` |
| `product_voice.py` SHA-256 | `c131613f028e89e2adba0ae4b4781b624ea3c3ae1e891dc2d43acfb797c3a57c` |
| B 服务器基线 | ready／admitted=1／local_enabled=1／许可19／归属7，存在原归属 |
| 硬件基线 | 原 B，P4 rev3.2、Waveshare WiFi6 Touch LCD4.3、物理32MB／原16MB布局；本轮硬件回传目标 ELF `364a1d6f…`／`ota_1` 保持，仅收到 ELF 前缀而非新的全量读回文件 |

13:38 保存不可随 latest 指针变化的私有基线，13:39:46 完成准备核验，见[准备摘要](evidence/2026-11-12-issue42-acceptance/prepared.json)。B 通过唯一 delivered 补发与原交付硬件映射定位，保存两台设备身份／凭据 hash／准入／归属／许可、全部补发和交付记录、服务 ID／版本／启动时间、私有配置／密钥及原 OTA 预留。原始身份和秘密仅在服务器私有目录。

执行现有 `backup.py`，核对设备、补发和交付表纳入备份，排除对话正文和音频；额外保存原交付加密密钥及原 OTA 预留。备份程序输出为完成提示，先前按历史包装脚本的目录提示解析而停止；核实实际备份成功后，复用该备份继续，没有重复备份或发送停用请求。未演练数据恢复。

## 已执行：保留归属的单台停用

13:39:56 通过真实 `POST /xiaozhi/device-control/v1/sign-out`，使用 B 当前凭据及 `expected_permission_version=19`，得到 HTTP 200／`OK`／许可20。该接口保留归属；本轮没有调用 `unbind`。凭据仅在 Ubuntu 进程内存中从现有已完成补发及原密钥恢复，并核对 hash。

| 检查 | 结果 |
| --- | --- |
| B 状态 | `user_signed_out`／admitted=1／local_enabled=0／许可20／归属7 |
| 设备状态接口 | 200／OK，`use_permission=blocked`，voice 与 lease 均为 null |
| B 身份、凭据、准入、原归属及归属版本 | 与停用前一致 |
| A 的身份、凭据、准入、归属、许可及版本 | 与停用前一致 |
| 全部交付及补发记录 | 与停用前一致 |
| 所记录的Compose／身份与交付密钥及 `e029d00a3ade4616adebfce862108b73` OTA 预留 | 保持 |

见[停用结果](evidence/2026-11-12-issue42-acceptance/disabled.json)。首次停用只发送一次；13:41 的同参数重复请求是下述明确的幂等验收，不是结果未知后的自动重发。正常 TLS 连接 Ubuntu 本机443，保留正式域名及公网 origin 的 Host，不跟随重定向；这不是新的公网18443探测。

本轮证明的是 `user_signed_out`。现有文档和代码识别 `device_disabled`，但未找到已部署产品 API 中可供运营管理员定向设置／恢复该状态的接口；未直接写数据库构造该状态，也未切换全局语音开关影响 A。管理员禁用的实机保持没有在本轮通过。

## 已执行：停用时的重复与迟到请求

13:41:19，真实 HTTPS 接口[六项检查](evidence/2026-11-12-issue42-acceptance/disabled-boundaries.json)均通过：

| 请求 | HTTP／协议结果 |
| --- | --- |
| 重复原停用，expected=19 | 200／OK，仍为许可20 |
| 更旧停用，expected=18 | 409／STATE_CHANGED |
| 迟到解绑，expected=19 | 409／STATE_CHANGED，原归属保持 |
| 迟到发码，expected=19 | 409／STATE_CHANGED |
| 原交付旧凭据发起停用 | 401／INVALID_DEVICE_CREDENTIAL |
| 原交付旧凭据查询状态 | 401／INVALID_DEVICE_CREDENTIAL |

请求前后 B 身份／凭据／归属及许可20／归属7保持，A 的业务字段及全部交付／补发记录不变。本轮没有新登记、补发、凭据轮换、物理写入或 OTA。

13:42 检查原补发管理员的既有登录有效性，没有找到该账号未过期的运营登录；因此未执行原补发查询、完成回执重试及同操作重放，见[未执行记录](evidence/2026-11-12-issue42-acceptance/recovery-receipt-boundaries.json)。没有创建登录、延长会话或把烧录凭据当作管理员权限。这部分不计为失败或通过。

## 已通过：恢复出厂后仍停用

用户回传[硬件三项结果](evidence/2026-11-12-issue42-acceptance/hardware-disabled-reset.json)：当前应用、NVS 和 `device_ctrl` 已私有备份；从恢复出厂入口完成恢复、重启及重新配网；目标 ELF `364a1d6f…`、`ota_1`、身份、凭据及全部 `device_ctrl` 存活记录保持一致。现场仍显示已停用、绑定原账号，实际尝试语音后无法进入正常对话。实际操作时间未提供，服务器没有独立 UART／音频采集。

硬件方明确该固件没有在 `device_ctrl` 持久化停用原因或许可版本，不能把存活记录保持写成这两个字段本地持久化通过。14:01:15 的[服务器独立核对](evidence/2026-11-12-issue42-acceptance/verify-reset.json)确认 B 仍为 `user_signed_out／admitted=1／local_enabled=0／许可20／归属7`，真实状态接口返回 blocked、voice／lease为空；与固定停用基线比较，身份、凭据、原归属、准入保持，A 的业务字段、全部交付／补发记录、所记录的Compose／身份与交付密钥及 OTA 预留均不变，服务版本及启动时间保持。本结果验证重新联网后的服务器停用状态保持，不是离线重建本地停用字段的验证。

## 已通过：原账号恢复与完整语音

服务器确认停用保持后，硬件方按安排扫描 B 当前二维码，以原归属账号明确恢复本台使用。用户回传“原归属账号恢复成功、原账号显示正常、一次完整语音问答正常”，见[现场结果](evidence/2026-11-12-issue42-acceptance/hardware-original-account-resume.json)。实际操作时间未提供，服务器没有独立采集 UART 或音频；这次恢复使用的反馈未提供新增 Flash 读回材料。

14:05:14 的[服务器复核](evidence/2026-11-12-issue42-acceptance/restored-check.json)确认 B 已为 `ready／admitted=1／local_enabled=1／许可21／归属7`，真实状态接口为 allowed。身份、凭据、准入、原归属与停用前一致；A 的业务字段、全部交付／补发记录、所记录的Compose／身份与交付密钥、原OTA预留以及服务版本／启动时间保持。许可从20增加到21来自本次明确恢复，不能把正常增加记作恢复出厂期间的状态变化。

本轮停用保留、正常维护及显式恢复的闭环通过。期间没有直接修改数据库、还原旧 NVS／秘密、解绑、轮换凭据、重复OTA或重启服务。

异常身份丢失另起维护窗口；用户已确认硬件方可以准备恢复使用后的新备份、最近已完成的恢复 journal 及匹配产品／维护构建，材料就绪仍待回传。实际补发还需原运营管理员通过正常入口重新登录；准备材料不等于异常丢失或补发已执行。本轮未执行破坏性擦除。

## 验收完成后的环境变化

补充准备恢复后的旧请求检查时，版本守卫发现语音镜像已变化，检查在发送任何补充HTTP请求前停止，没有将其计入六项通过结果。14:11:24 的[只读环境核对](evidence/2026-11-12-issue42-acceptance/environment-change.json)确认，语音现为 `xiaozhi-product-voice:cutoff81-trace-20261112`，镜像 ID `sha256:3dde4f523ed217a3fc97cfeafc11ba54c4575b861ddd398ce516a0189500a36a`，14:06:54启动。Compose已随之变化；API、数据库、Redis、TLS入口的容器及启动时间不变，B仍为ready／许可21／归属7，身份、凭据、原归属、A、交付／补发记录、身份与交付密钥及原OTA预留保持。

本轮通过结论对应13:38–14:05的原受测环境；后续镜像的源码对应、功能及硬件听音不由这次只读观察验收。本任务没有执行该部署／重启，不覆盖其他任务的诊断配置或将其回退。接续异常身份恢复前须再次确认维护窗口、当前服务和材料。

14:17:40 的[再次只读核对](evidence/2026-11-12-issue42-acceptance/restored-runtime.json)确认语音已于14:12:27恢复为原候选 `pr81-75290c4-pr79-f830a8c`，镜像ID及两个维护模块hash与本轮受测版本一致，容器及启动时间已变化。B仍为ready／许可21／归属7，身份、凭据、原归属、A、交付／补发记录、身份与交付密钥和原OTA预留保持；其他四项服务的容器／启动时间保持。Compose与原基线仍有差异，未将整份配置宣称为完全恢复。本任务仅记录观察，没有执行该恢复部署，也没有新发协议请求或补做实机语音；14:06临时trace镜像不再是当前镜像。

## 17:43接续：真实单键身份丢失

本日下午已完成[真实单键删除、独立NVS读回及原产品启动](2026-11-12-controlled-identity-loss.md)，并核对服务器原档案、凭据、归属、B许可21／归属7及A保持。当前语音实际为14:50启动的 `pr79-f830a8c`，本轮未部署服务；上述PR81及trace记录保留各自时间范围。B目前为原目标产品／ota_1、身份缺失，尚未新补发。用户要求离场后停止硬件步骤；现场语音拒绝、管理员补发写回及恢复后验收交由设备方按[交接说明](../handoff/2026-11-12-issue42-identity-loss.md)接续并提交报告。此增量不改变本报告13:38–14:05停用闭环的原证据范围。

## #42 六项标准的当前证据范围

| 原标准 | 已有证据 | 尚需完成 |
| --- | --- | --- |
| 正常维护保留稳定身份和凭据，区分维护操作 | [应用更新／USB复位／配网](2026-11-11-device-identity-retention.md)、[真实OTA及ready状态恢复出厂](2026-11-12-p4-ota-server.md)、本轮user_signed_out后的恢复出厂保持／原账号恢复／完整语音、[操作边界](../development/device-identity-recovery.md) | 管理员device_disabled未验，原OTA实时过程日志缺口保留 |
| 普通配置／NVS修复不误清身份，实际布局兼容 | 实际P4原16MB布局、命名空间保留、禁止自动整分区擦除的实现与既有受测记录 | 整个NVS损坏／不可读anchor／全片擦除后的受控修复工序未交付，不能宣称任意损坏可恢复 |
| 授权补发保留原档案／硬件／客户归属，拒绝旧凭据与越权 | [健康身份授权轮换写回](2026-11-11-device-identity-retention.md)、[Ubuntu独立新旧凭据请求](2026-11-12-api-restart-audit.md)、[PR63回归及拒绝](2026-11-11-pr63-deployment.md) | 真正异常身份丢失后完整恢复；最初补发瞬间归属／许可历史比对未补齐 |
| 重放／迟到／竞态不复活旧凭据或错误归属 | 既有隔离HTTP并发／重放回归；本轮6项真实停用边界检查 | 新恢复轮次适用中断续作、迟到完成及代际边界；本轮管理员回执检查未执行 |
| 如实说明物理秘密保护 | [实际构建限制](../development/device-identity-recovery.md#秘密保护与回退)：未启用Secure Boot、Flash/NVS加密，可从未加密Flash提取秘密 | 不能宣称eFuse、不可提取或未经验证的加密保障 |
| Ubuntu与真实P4完整证据 | 上述维护、补发写回、新旧凭据及[历史API重启持久性](2026-11-12-api-restart-audit.md)；本轮停用后恢复出厂与原账号恢复有硬件和服务器双侧结果，完整回答有用户确认 | 异常丢失完整恢复及适用边界未齐；历史重启后的紧接实机确认缺失，Ubuntu主机重启另列 |

上述局部结果不等于全部标准通过，不改成全项勾选或关闭 #42／#13。原11:20 OTA预留保留，不重发、不重新开放下载来弥补历史日志。
