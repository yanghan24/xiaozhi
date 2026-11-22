# 当前生产部署状态

最近一次部署核验为 **2026-11-23 06:56（Asia/Shanghai）**。生产目录、构建、传包、备份、启停和 OTA 流程见[生产部署规范](../../deploy/production/README.md)。

## 业务入口

| 用途 | 地址或路径 |
| --- | --- |
| 管理后台 | [生产管理登录页](https://bridge.hncig.cn:18443/#/login) |
| 音箱控制与心跳 | HTTPS `/xiaozhi/speaker/v1/` |
| 音箱 OTA 检查 | HTTPS `/xiaozhi/ota/` |
| 音箱语音 | WSS `/xiaozhi/v1/` |

音箱接口与管理页经同一公开 HTTPS 网关访问。设备请求使用各自的现有认证机制。

## 现网版本与证据

| 组件 | 最近核验版本 | 核验结果 |
| --- | --- | --- |
| 管理 Web、Java API | `fea5cc273ca4cec57d404cbfb3e3d5c1f664a344` | 2026-11-23 00:22:08 按组件升级。本机及公网管理入口、设备接口鉴权和 WSS 检查通过。抽查 20 条近期会话，设备和集市账号 ID 均与会话历史归属一致。 |
| Python 语音服务 | `132e1bacfc61a3efc4bd180b7d547f33038fa0b6` | 仍为 2026-11-21 发布的实例，WSS 握手与鉴权检查通过。 |
| MySQL、Redis、Nginx | 8.0.41、8.0.0、1.27.4 | 运行实例保持一致，MySQL、Redis 健康检查通过。 |
| Waveshare ESP32-P4X 4.3 寸 OTA 下发 | 2.6.21，`5f1c564fa141c47a0e6ea3dd181b128aa59eb22e` | 2026-11-23 06:56:37 重新发布原包，本机和公网下载校验一致。发布复查时尚未收到该版本设备心跳。 |

### 服务端升级记录

API 和 Web 以 `132e1bacfc61a3efc4bd180b7d547f33038fa0b6` 的镜像层为基线。按部署规范提交 `7673b86` 生成应用层包，114 校验基线后直接由 Docker 导入，核对镜像 ID、镜像层及源码提交后只切换两个组件。本次没有数据库结构变更。开发环境通过设备换绑后历史会话归属的回归测试。

应用层包 `services.tar` 为 137,472,000 字节，SHA-256 为 `fdea6b47fdee2d49557c0d7f63fad39eecf889cd0114b03bafee4c1e6c6d5e70`。包、清单和 `release.json` 位于 `/data/packages/xiaozhi/fea5cc273ca4/component-upgrade/`，原 Compose 和服务记录备份在 `/data/backup/xiaozhi/before-fea5cc273ca4/`。部署证据覆盖日常应用层升级，首次部署沿用规范的独立流程。

### 固件 OTA 记录

2.6.21（`5f1c564fa141c47a0e6ea3dd181b128aa59eb22e`）于 00:47:19 发布。首页日期和天气改为约 24 px 并增加行距，日历调整了当前周、今天按钮、提醒圆点和页底提示。开发环境通过 800×480 LVGL 模拟检查和 P4X 构建。

测试设备两次在进度 98% 和 99% 时报告升级失败。公网网关在 00:49:20 和 00:51:17 记录 HTTP 200、响应体均为 4,034,560 字节。随后设备仍上报 2.6.20。应用包校验和 OTA 分区大小检查通过，设备端具体失败阶段待串口日志确认。

2.6.21 首次发布后暂停下发，包、发布回执和恢复回执位于 `/data/packages/xiaozhi/5f1c564fa141/`，应用包 SHA-256 为 `19bbf78557793495a6ff4d2f81baba975d295d24253f6acbad45d0d10c849f6a`。00:53:52 先恢复到 2.6.20，该目录备份位于 `/data/backup/xiaozhi/ota-before-2.6.21/`。

00:57:55 按确认恢复到 2.6.19，使用 `/data/backup/xiaozhi/ota-before-2.6.20/` 中的目录和应用包。2.6.19 应用包为 4,031,744 字节，SHA-256 为 `139133e1bfbf4550d32926d8d4181c352c7b85c66876c422f198a7c4c634ebc3`。恢复回执位于 `/data/packages/xiaozhi/abbeda268c40/ota-2.6.20-restored-to-2.6.19.json`。目录恢复后，设备仍按现有版本比较规则判断是否升级。

01:15:52 发布对照版本 2.6.22。源码基线为 2.6.19 的 `2f29df69b28a1d3a64bb461a03981af6357d16bd`，差异仅为 `CMakeLists.txt` 中的内嵌版本号。构建配置、分区表和资源与 2.6.19 一致。应用包为 4,031,744 字节，最小应用分区余 97,024 字节。SHA-256 为 `a90be085ee8c5f31b1f18835df476c0b2137a533c40d4adbbb9b0fcf12685fda`。发布回执位于 `/data/packages/xiaozhi/31d3790c00f0/ota-2.6.22.json`，切换前的 2.6.19 目录和原包备份在 `/data/backup/xiaozhi/ota-before-2.6.22/`。发布前后的后台服务实例一致。

2.6.22 复现升级末段失败。公网网关在 01:18:03 和 01:19:47 记录 HTTP 200，响应体均为 4,031,744 字节。未收到 2.6.22 心跳或此次升级的设备端错误。01:21:32 从备份恢复到 2.6.19，恢复回执位于 `/data/packages/xiaozhi/31d3790c00f0/ota-2.6.22-rollback.json`。

06:56:37 根据确认重新发布 2.6.21，复用原应用包，大小 4,034,560 字节，SHA-256 与首次发布一致。重新发布回执位于 `/data/packages/xiaozhi/5f1c564fa141/ota-2.6.21-republish.json`。本次切换前的 2.6.19 目录和原包备份在 `/data/backup/xiaozhi/ota-before-2.6.21-republish/`。发布前后的后台服务实例一致。

## 验证与运维资料

- [设备自动登记与首次扫码](../archive/completed/identity/speaker-auto-enrollment-2026-11-20.md)
- [音箱触屏配网开发验证](../archive/completed/firmware/speaker-touch-wifi.md)
- [固件界面与诊断验证](../archive/completed/ui/speaker-dark-ui-implementation.md)及[语音与显示诊断](../archive/completed/firmware/speaker-display-diagnostics.md)

服务器的部署视图、运维说明和更新日志位于 `/data/`。开展生产操作前核对实际运行状态。较早验收按各自日期、版本和实测范围查阅。
