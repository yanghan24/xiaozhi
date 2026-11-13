# #18 指定 B 的 OTA 准备与验收

**最终结果（2026-11-13 11:17，Asia/Hong_Kong）：本轮指定B的同版本OTA重装通过。完整写入、自动重启、ota_1→ota_0、目标ELF及有效标记均有UART证据；真实产品语音会话已恢复，身份、有效凭据、归属及许可保持。下载入口已撤销，没有重发。普通/MCP问答复测交给 #19。** 下文按时间保留准备、故障和未知传输回执，不将传输回执改写成成功。

**历史快照11:10：已按用户授权单次下发，回执未知且未重发；服务器身份状态保持，当时安装结果等待硬件日志。** 下文10时准备阶段的“尚未下发”保留原时间范围。

2026-11-13，Asia/Hong_Kong。用户授权与 #14 并行开发，随后明确“现在已释放 b”。本记录仅为准备证据，尚未下发 OTA，不关闭 #18。

## 固定候选

按用户“暂时先按今天上传的版本准备”选取 [Release](https://github.com/yanghan24/xiaozhi/releases/tag/firmware/p4-2.2.6-12f2ff1-silent-interrupt-rc1)。源码 `12f2ff1b3e224ff012bdce95799ebf19740dcd11`，应用版本 `2.2.6`，Waveshare ESP32-P4 WiFi6 Touch LCD 4.3，发布说明要求 revision 3.1+。

- 应用大小：4,003,072 字节；SHA-256：`7cb0c36077d42d9b64f09727e176c83f6ca2f281abb04d988982dc82e30b8f8c`。
- ELF：`f242b5b68cc3c565335c7eb0b048156209531ebd6fb5e9c54bdf809b9cc58d6e`。
- Ubuntu 已独立核对大小、文件 SHA-256、ESP 镜像及应用描述符 magic、镜像内应用版本和 ELF 标识；大小小于 manifest 声明的 4,128,768 字节应用槽。未以 manifest 替代 B 当前分区读回。
- 私有暂存：`/home/jt/issue18-p4-ota-20261113/p4-12f2ff1/`。

B 最新签收已为同一源码版本。本次如使用该候选，应记录为同版本 OTA 重装验证；仅看到同一版本号或 ELF 不能证明本轮安装发生。须保存本轮下载、写入、重启及槽位/安装结果证据。

## 维护路径与回归

现有维护入口只接受旧 `p4-a30f6a8` 路径，本变更新增精确路径 `/xiaozhi/maintenance-ota/p4-12f2ff1/product.bin`。仍限定 HTTPS、正式域名及18443端口、短期签名参数、单台目标、唯一当前会话与许可；旧路径与既有防重发预留保留。没有开放任意版本路径，不覆盖旧版本 URL 的固件字节。

沿用现有维护控制接口回归：新增新候选路径用例先在 Ubuntu 隔离容器触发 `MaintenanceDenied`；修改后维护套件16项全部通过，无跳过。以现网 `xiaozhi-product-voice:rtasr-3739aad` 的实际镜像 ID 为基底，仅覆盖 `product_maintenance.py`，构建 `xiaozhi-product-voice:issue18-12f2ff1`；在候选镜像内再次运行同一套件16项通过。夹具模拟设备，不向真实 B 发送升级。

准备阶段候选尚未部署。后续部署及下载核验已完成，见下方10:47–10:49接续记录；没有回退到旧 `pr79-f830a8c` 镜像。

## B 的实时只读核对

2026-11-13 10:41:42 香港时间：准入1、本地许可1、ready、许可版本24、归属版本7。根据现网维护设备配置定位 B，核对当前 credential hash 匹配的唯一 delivered 补发记录，并与 PR #87 历史快照核对同一设备/归属及交付硬件映射。历史补发记录已不止一条，不能再按“全表只有一条记录”定位 B。

私有数据库快照位于 `/home/jt/issue18-p4-ota-20261113/20261113T024142Z/`，凭据、账号和设备标识不公开。维护 status 为 `no_unique_session`，没有执行真实工具 probe 或 upgrade。该结果表示没有唯一可用产品语音会话，不能据此认定 B 离线或固件不支持 OTA。

## 下发前仍须满足

1. 硬件侧确认当前应用、NVS及启动材料备份、当前分区/活动槽与板型兼容性，并准备本次串口日志。本机仅发现 COM2，尚无 B 的可用串口。
2. B 建立唯一当前语音会话，维护工具 probe 通过；即时重新核对归属、有效凭据与许可。
3. 维护候选已部署；下发前重新生成短时下载并核对该次链接的外部18443 TLS及全文件SHA-256，A 不获得下发。
4. 固定一次操作号，下发前保存 B/A 基线。回执未知不自动重试；升级后核对实际槽位/安装、身份/凭据/归属及重新接入。
5. 撤销下载入口，交付 #19 在同一设备复测普通/MCP问答。

以上10:41快照时未重启服务或发布下载。设备窗口释放已收到；当前等待的是硬件准备及唯一活跃会话，不能再标为等待 #14 释放。

## 10:46–10:49 接续：维护部署及下载验证通过

用户确认 Fedora `/dev/ttyACM0` 可用，09:35 的 `12f2ff1` 备份位于硬件工程的 `.runtime/wake-word-greeting-20261113/b/b-install-20261113T013139Z`。`product-verified.bin`、`nvs-after.bin`、`backup/boot-prefix.bin` 已由硬件侧核验；`backup/application.bin` 是此前旧应用。旧日志采集09:38结束，不能作为本轮 OTA 日志。用户决定由硬件侧完成当前状态比对；本轮采集尚未收到启动确认。

10:46:37 再查 B 已有唯一当前语音会话，真实工具 probe 返回 `upgrade_tool_available`，许可24/归属7。随后完成以下服务器工作：

- [10:47:44 部署](evidence/2026-11-13-issue18/issue18-deployment-summary-public.json)：仅重建语音，镜像 `xiaozhi-product-voice:issue18-12f2ff1`，ID `sha256:1267d4cd09a8142ea135d6c3d2a8ad2401beacc7ac3f5d8f02c43c4ddd086a0e`。基于实际 `rtasr-3739aad` 镜像，仅更新维护模块。Compose 仅改语音 image，配置及提示词保留；API/数据库/Redis/TLS入口的容器ID与启动时间不变。前后设备业务字段、补发及交付映射一致。私有恢复参考为该证据所列 `compose-before.json`，恢复时只改语音 image 并保留后续更新。
- [10:48:35–10:48:42 下载核验](evidence/2026-11-13-issue18/issue18-https-summary-public.json)：新路径的15分钟签名链接通过本机正常TLS与 Ubuntu 经公网域名18443完整下载，文件SHA-256匹配。无签名/错误签名403、过期签名410；无签名公网请求403。未提供给设备，也不以服务器下载证明 B 的实际可达性。摘要原继承旧包 revision=2 的字段已在导出中纠正，包身份以本次release和SHA为准。
- [10:49:23 撤销](evidence/2026-11-13-issue18/issue18-revocation-summary-public.json)：等硬件采集期间撤销临时路由；本机正常TLS下该路径404、OTA发现200。下发时重新生成并验证新链接。
- [10:49:24 状态](evidence/2026-11-13-issue18/issue18-current-public.json)：B仍ready/许可24/归属7，维护模块SHA与候选一致；重建后暂为 `no_unique_session`，采集启动后需重新唤醒并核对当前会话。

本轮没有下发OTA、补发凭据或修改归属。部署与短时下载准备已完成，实际安装、自动重启与身份保持尚待硬件侧配合验证。

## 11:02 硬件就绪确认与11:05下发前复查

用户明确授权本轮下发，并转述硬件核验：当前2.2.6/12f2ff1应用读回及运行ELF与归档一致；活动槽ota_1、地址0x410000、VALID且CRC有效；NVS仅env/cache变化，身份/凭据/device_ctrl保持，已保存新备份；bootloader、分区表、OTA选择及保护区域与归档一致。Fedora串口持续采集中，具体本轮日志路径尚待补充。这些是用户转述的现场证据，不是服务器独立Flash读回。

11:03:41/11:05:15服务器复查发现另一项更新已将语音切换为 `xiaozhi-product-voice:asr-switch`。B仍ready/许可24/归属7，但维护status为 `no_unique_session`。没有生成新下载链接或下发OTA，已请现场再次唤醒B。

11:04:31在该实际镜像ID `sha256:66045ff8230ad09d705c938c380a50ab26f19e9e4469aaf5761aab942a066897` 的隔离容器重跑维护套件16项通过；维护模块仍为本PR的SHA `3c1a9a6c25044f207e3034cf786dfd60d638f2edb6849f0b5f274507d50b0e5d`，product_voice模块已更新为 `b288c42f686c3c2acf0adfe08acc06fea897ac322d4ad6b723780dedb5bfec74`。保留该更新，不回退现网。私有 `dispatch-runtime-verified.json` 固定本次已验证镜像，实际下发脚本必须再次匹配该ID。硬件就绪记录保留用户报告的11:02时间，不用后续写文件时间延长就绪窗口。

## 11:08 入口故障修复

现场报告B在11:04:07、11:04:11、11:04:57唤醒后握手失败。Docker只读核对确认另一项更新于11:02:36重建API、11:02:48重建语音，均为`asr-switch`；本任务11:03–11:05仅只读核对及隔离测试。入口配置mtime仍为10:49:23，语音环境变量相对10:47前快照无变化，不能由此推断API认证实现完全未变。

入口错误日志在11:04:07.764、11:04:12.015、11:04:58.010分别记录连接旧`172.26.0.3:8000`被拒绝，当前语音实际位于`172.26.0.4`。无凭据公网WebSocket探测稳定返回502；直连`.3`拒绝、`.4`返回101，入口容器DNS已解析到`.4`，确认是Nginx进程缓存旧上游地址。

校验Nginx配置后于11:08:39仅平滑重载，配置文件SHA保持不变；原探测从502转为101，再次复查仍101，见[修复证据](evidence/2026-11-13-issue18/issue18-handshake-fix-public.json)。未修改设备凭据或认证配置，无调试插桩。101仅证明HTTP升级恢复，真实B会话另在11:09:20唤醒后确认current且工具可用。恢复服务/重建后端后必须重新解析入口上游并验证公网握手，不能只看容器running。

## 11:09 单次下发及11:10状态

- [硬件确认](evidence/2026-11-13-issue18/issue18-hardware-ready-public.json)依据用户11:02报告，未独立读取Fedora。保持串口采集，用户授权有效。
- 11:09:20真实B唯一当前会话和`self.upgrade_firmware`工具可用；当前[受测运行镜像](evidence/2026-11-13-issue18/issue18-dispatch-runtime-public.json)匹配下发前检查。
- 11:09:30生成新的短时签名下载，11:09:31完成本机和公网18443 TLS、完整文件SHA及无签名拒绝验证，见[本轮下载证据](evidence/2026-11-13-issue18/issue18-dispatch-https-public.json)。此链接有效至11:24:30，等待硬件确认下载完成后撤销；签名URL仅存私有目录。
- 约11:09:33执行一次下发；[11:09:43回执](evidence/2026-11-13-issue18/issue18-dispatch-public.json)为`result_unknown_do_not_retry`，操作号`198bf4223d1740cb95fd1ea9dbf314c7`。本地dispatch文件和服务器预留均保留，没有重发；不能把未知回执当成设备拒绝或安装完成。
- [11:10:20服务器比对](evidence/2026-11-13-issue18/issue18-post-dispatch-public.json)：预留仍为`reserved_result_unknown`，B ready/许可24/归属7、当前凭据200/OK；B业务状态、其他设备、补发记录及交付映射与下发前一致。11:10:21维护status暂为`no_unique_session`。

正在等待硬件侧本轮下载、写入、自动重启、活动槽、VALID/CRC、应用/ELF及受保护身份结果；已要求保留现场，不主动复位/重新刷机。#18不关闭，#19语音复测未由这些检查代替。

## 11:15–11:17 最终安装及重连验收

用户提供 `uart-private.log`，97,399字节，SHA-256 `2d7d74a3907104d71ded81237c5361f8ea62632b3a2c8e9391bd4eacdc8b1174`。原始日志包含身份、签名URL及其他私有内容，仅保留在本地受控环境，不上传Git。仓库保存[脱敏提取](evidence/2026-11-13-issue18/uart-installation.json)及[与本轮下发关联的安装确认](evidence/2026-11-13-issue18/issue18-installation-reconciliation-public.json)。通过仅在私有环境比较URL摘要，确认UART中的升级URL与本轮操作完全匹配。

UART使用设备启动后的毫秒计数，没有逐行墙钟时间；不能把这些计数直接当成北京时间。以下行号按用户原始文件：

| 证据 | 原日志行号与结果 |
| --- | --- |
| 下发前应用/槽 | 188：完整ELF匹配候选；210：运行ota_1 |
| 收到本轮升级 | 1119：uptime732817，升级URL与操作`198bf4223d1740cb95fd1ea9dbf314c7`匹配 |
| 写入 | 1132：ota_0、0x20000；1185：uptime785024，4,003,072/4,003,072字节完成 |
| 成功并自动重启 | 1202–1204：升级成功、重启；1210：SW_CPU_RESET。请求到重启约53.8秒，开始写入到完整接收约48.1秒 |
| 实际运行目标 | 1397：完整ELF为`f242b5b68cc3c565335c7eb0b048156209531ebd6fb5e9c54bdf809b9cc58d6e`，设备/硬件标识与重启前一致、network_connected=true；1404：2.2.6；1419：运行ota_0；1420：Marking firmware as valid |

本轮有实时下载/写入/自动重启证据，不借助事后主动复位补证。`Marking firmware as valid`是固件日志证据，未声称另做了升级后的Flash全量读回、OTA选择数据CRC或整个NVS逐字节比对。现有受控维护与身份前后证据足以覆盖本轮 #18；异常回滚/掉电恢复仍属 #88。

[11:15:42撤销下载](evidence/2026-11-13-issue18/issue18-final-revocation-public.json)：仅移除本次精确路由，正常TLS验证目标404、发现200；私有固件、预留和记录保留。该摘要的`upgrade_sent=false`仅指撤销动作没有发送升级，不是否认11:09已单次下发。

[11:15:43服务器前后比对](evidence/2026-11-13-issue18/post-installation-state.json)：B ready/许可24/归属7、当前凭据200/OK；B稳定业务字段、其他设备、全部补发记录和硬件交付映射一致。该脚本自身只判断服务器状态，所以其`installation_confirmed=false`仍保留，安装结论由独立UART关联记录提供。

用户再次唤醒后，[11:17:43真实产品会话](evidence/2026-11-13-issue18/issue18-reconnection-public.json)为current，工具probe为upgrade_tool_available，许可24/归属7，证明真实B重新接入；不是仅由代理持凭据访问HTTP代替。未再调用升级工具。

### #18 五项验收映射与恢复条件

1. 固定B、12f2ff1源码、应用SHA/ELF/大小；既有Waveshare P4 rev3.2/16MiB布局资料和用户本轮Flash/启动材料核对，结合实际ota_0写入、运行，确认候选与板型/双槽兼容。
2. 仅B的root维护通道、唯一会话和当前许可通过；正式HTTPS18443完整下载校验通过，设备本轮实际下载完成；未向A下发，其他设备业务状态不变。秘密只存私有环境。
3. UART证明本轮写入、自动重启、实际新活动槽和完整目标ELF，不能仅凭相同2.2.6版本号判定。
4. UART重启前后稳定设备及硬件标识相同，服务器凭据hash/归属/许可保持，当前凭据有效，真实B重新接入。
5. 单次操作、未知传输预留保留且未重发，已保存独立安装确认和撤销下载记录。当前运行已验证的12f2ff1；需人工恢复时，由Fedora串口工作站使用本轮新NVS备份及已核验的`product-verified.bin`、`backup/boot-prefix.bin`恢复对应应用/启动材料并保留身份，不能把早晨`backup/application.bin`旧应用误作当前版本。未验证自动降级/回滚，不执行全片擦除；服务器镜像回退不会撤销已刷入固件。

**按限定范围完成 #18。** 交付 #19：B、2.2.6/12f2ff1、目标完整ELF、ota_0、许可24/归属7、当前asr-switch服务；在同一设备复测普通语音与自动MCP知识库问答，不能将本轮工具probe代作问答验收。
