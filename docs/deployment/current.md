# 当前生产部署状态

最近一次发布核验为 **2026-11-22 11:45（Asia/Shanghai）**。生产目录、构建、传包、备份、启停和 OTA 流程见[生产部署规范](../../deploy/production/README.md)。

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
| 管理 Web、Java API、Python 语音服务 | `132e1bacfc61a3efc4bd180b7d547f33038fa0b6` | 2026-11-21 19:19 发布，管理入口、设备接口鉴权和 WSS 检查通过。2.6.19 固件发布后确认各服务保持原运行实例。 |
| MySQL 与 Redis | 8.0.41、8.0.0 | 小智独立实例。2.6.19 发布复查时均在运行。 |
| Waveshare ESP32-P4X 4.3 寸 | 2.6.19，`2f29df69b28a1d3a64bb461a03981af6357d16bd` | 2026-11-22 11:45:02 通过原版固件管理按板型发布，本机与公网下载校验一致。发布复查时尚未收到该版本设备心跳。 |

服务包及校验清单位于 `/data/packages/xiaozhi/132e1bacfc61/`，更新前的配置备份在 `/data/backup/xiaozhi/before-132e1bacfc61/`。该版本包括音箱积分接口、通用诊断、第三方服务列表修复、集市账号 ID 展示及集市讯飞 TTS。

2.6.19 基于官方 `v2.6.0`，包含运行中 Wi-Fi 换网修正、首次配网提交复用、20 px 键盘和计时字体精简。OTA 应用包为 4,031,744 字节，分区余 97,024 字节。SHA-256 为 `139133e1bfbf4550d32926d8d4181c352c7b85c66876c422f198a7c4c634ebc3`。固件包和回执位于 `/data/packages/xiaozhi/2f29df69b28a/`，2.6.18 目录备份在 `/data/backup/xiaozhi/ota-before-2.6.19/`。

## 验证与运维资料

- [设备自动登记与首次扫码](../archive/completed/identity/speaker-auto-enrollment-2026-11-20.md)
- [音箱触屏配网开发验证](../archive/completed/firmware/speaker-touch-wifi.md)
- [固件界面与诊断验证](../archive/completed/ui/speaker-dark-ui-implementation.md)及[语音与显示诊断](../archive/completed/firmware/speaker-display-diagnostics.md)

服务器的部署视图、运维说明和更新日志位于 `/data/`。开展生产操作前核对实际运行状态。较早验收按各自日期、版本和实测范围查阅。
