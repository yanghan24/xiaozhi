# 当前生产部署状态

最近一次发布核验为 **2026-11-23 00:22（Asia/Shanghai）**。生产目录、构建、传包、备份、启停和 OTA 流程见[生产部署规范](../../deploy/production/README.md)。

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
| Waveshare ESP32-P4X 4.3 寸 | 2.6.20，`abbeda268c405f87531f0a2a405b5ab4118e1d3c` | 2026-11-23 00:22:22 通过现有固件管理按板型发布，本机和公网下载校验一致。发布复查时尚未收到该版本设备心跳。 |

### 服务端升级记录

API 和 Web 以 `132e1bacfc61a3efc4bd180b7d547f33038fa0b6` 的镜像层为基线。按部署规范提交 `7673b86` 生成应用层包，114 校验基线后直接由 Docker 导入，核对镜像 ID、镜像层及源码提交后只切换两个组件。本次没有数据库结构变更。开发环境通过设备换绑后历史会话归属的回归测试。

应用层包 `services.tar` 为 137,472,000 字节，SHA-256 为 `fdea6b47fdee2d49557c0d7f63fad39eecf889cd0114b03bafee4c1e6c6d5e70`。包、清单和 `release.json` 位于 `/data/packages/xiaozhi/fea5cc273ca4/component-upgrade/`，原 Compose 和服务记录备份在 `/data/backup/xiaozhi/before-fea5cc273ca4/`。部署证据覆盖日常应用层升级，首次部署沿用规范的独立流程。

### 固件 OTA 记录

2.6.20 基于官方 `v2.6.0`。日历支持连续按天滑动、年月、回到今天和提醒圆点。首页时钟下方显示日期、星期与天气。应用包为 4,034,176 字节，最小应用分区余 94,592 字节。SHA-256 为 `c5b8751ba3cbacec0c66d1fc580e2d165ffcc275611fa0fb7eff34559788155b`。

固件包和 `ota-2.6.20.json` 位于 `/data/packages/xiaozhi/abbeda268c40/`。上一版 2.6.19 的目录和固件备份在 `/data/backup/xiaozhi/ota-before-2.6.20/`，源码提交为 `2f29df69b28a1d3a64bb461a03981af6357d16bd`。

## 验证与运维资料

- [设备自动登记与首次扫码](../archive/completed/identity/speaker-auto-enrollment-2026-11-20.md)
- [音箱触屏配网开发验证](../archive/completed/firmware/speaker-touch-wifi.md)
- [固件界面与诊断验证](../archive/completed/ui/speaker-dark-ui-implementation.md)及[语音与显示诊断](../archive/completed/firmware/speaker-display-diagnostics.md)

服务器的部署视图、运维说明和更新日志位于 `/data/`。开展生产操作前核对实际运行状态。较早验收按各自日期、版本和实测范围查阅。
