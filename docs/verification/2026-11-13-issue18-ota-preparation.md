# #18 指定 B 的 OTA 准备

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
