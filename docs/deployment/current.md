# 当前生产部署状态

最近一次部署核验为 **2026-11-27 10:35（Asia/Shanghai）**。生产目录、构建、传包、备份、启停和 OTA 流程见[生产部署规范](../../deploy/production/README.md)。

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
| Java API | `5f76040875fc47b6c46c24f1bab2829316634ca9` | 2026-11-27 10:34:46 完成应用层升级。集市应用 Key 在登录后和对话前准备，迁移 `202611270900` 已核验。 |
| 管理 Web | `127fd603872cc8750dbd33956e84f4b4f46aa22d` | 2026-11-24 08:59:56 完成应用层升级，11-27 确认运行实例一致。 |
| Python 语音服务 | `5f76040875fc47b6c46c24f1bab2829316634ca9` | 2026-11-27 10:34:46 完成应用层升级。支持集市凭据恢复与分类短提示，Java 运行配置读取和公网 WSS 鉴权检查通过。 |
| MySQL、Redis、Nginx | 8.0.41、8.0.0、1.27.4 | 运行实例保持一致，MySQL、Redis 健康检查通过。 |
| Waveshare ESP32-P4X 4.3 寸 OTA 下发 | 2.6.32，`e77513d2f33a5408a426b1b4c3ac6a72b24d6494` | 2026-11-27 10:35:11 发布，本机和公网下载校验一致。设备安装以版本心跳核验。 |

### 服务端升级记录

2026-11-27 10:34:46，Java API 从 `1762bb4e588c37f733499897af758c108df27060`、Python 语音服务从 `e339ce93c5c492459270f9658d5e12659ceffc27` 增量升级到 `5f76040875fc47b6c46c24f1bab2829316634ca9`。迁移 `202611270900` 为共享应用 Key 增加核验时间和核验调度字段。管理入口、设备鉴权、扫码、充值鉴权、语音配置读取、公网 WSS 鉴权及存储健康通过。管理 Web、MySQL、Redis 和 Nginx 实例一致。

开发环境完成 56 项定向 Java 测试和 31 项定向 Python 测试。生产复查此前失败的测试账号时，应用 Key 通过集市核验。音箱对话和短提示显示属于实机验收。

应用层包 `services.tar` 为 102,768,640 字节，SHA-256 为 `3c1e29d1f6e8db79453cae291e86ad9d56bec242ec95b5a7a77ed1c0f1f06fc5`。包、镜像清单和回执位于 `/data/packages/xiaozhi/5f76040875fc/component-upgrade/`。原 Compose、镜像基线和加密授权表备份位于 `/data/backup/xiaozhi/before-5f76040875fc/`。

2026-11-27 08:06:15，Java API 以 `99fc383658e7012a21652497458687091a2e1cd5` 为基线升级到 `1762bb4e588c37f733499897af758c108df27060`。设备状态下发本机记事归属标识，待机天气增加体感温度和空气质量。本次无需数据库迁移。开发环境 26 项定向测试通过。生产核验覆盖基线和目标镜像层、管理入口、扫码及充值鉴权、语音运行配置读取和公网 WSS 鉴权。

应用层包 `services.tar` 为 79,687,680 字节，SHA-256 为 `2d1a9ad06975efd59a45f086cc46fc9cafe79d624fa67d2c718f2377a4699de4`。包、清单和发布回执位于 `/data/packages/xiaozhi/1762bb4e588c/component-upgrade/`。原 Compose 和镜像基线备份位于 `/data/backup/xiaozhi/before-1762bb4e588c/`。

2026-11-26 12:18:50，以 `d3c17d3ecdae321ace062be354335db08d0dede3` 为基线，仅升级 Java API 至 `99fc383658e7012a21652497458687091a2e1cd5`。迁移 `202611261800` 增加设备登录的扫码时间。生产验证覆盖失效码拒绝、预检请求不消耗二维码、设备刷新与充值接口鉴权、语音配置读取和公网 WSS 鉴权。41 项定向开发测试通过。管理 Web、Python 语音、MySQL、Redis 和 Nginx 实例一致。

应用层包 `services.tar` 为 79,677,440 字节，SHA-256 为 `5fac9c0079835b8090a4579ec9cc657ea6c913e0f29d3dfb7f3f215badec1a5c`。包、清单和发布回执位于 `/data/packages/xiaozhi/99fc383658e7/component-upgrade/`，原 Compose、镜像基线和登录表结构备份位于 `/data/backup/xiaozhi/before-99fc383658e7/`。

2026-11-24 13:13:49，以 `127fd603872cc8750dbd33956e84f4b4f46aa22d` 为基线，仅升级 Java API 至 `d3c17d3ecdae321ace062be354335db08d0dede3`。启用 `speaker.recharge-enabled`。新发起的扫码登录同时申请充值权限，已有授权缺少权限时可用原账号补充。迁移 `202611261700` 新增授权范围及补授权事务标记字段。37 项定向开发测试通过。生产检查覆盖三条充值接口的设备鉴权和禁止缓存、管理入口、Python 语音配置读取与公网 WSS 鉴权。管理 Web、Python 语音、MySQL、Redis 和 Nginx 的运行实例一致。真实集市扫码授权及微信付款为实机验收项。

应用层包 `services.tar` 为 79,677,440 字节，SHA-256 为 `b122f75ae1c269dfbd5256a8f1e04ec7f4130515e44eefda5f59b7c9cdc2f749`。包、清单和发布回执位于 `/data/packages/xiaozhi/d3c17d3ecdae/component-upgrade/`。首次切换因验收脚本按单条匹配重复的 `Cache-Control: no-store` 响应头，触发 API 与配置回退。核实响应头后修正验收脚本并重新切换。两次切换前的 Compose、应用参数及受影响的授权数据和表结构分别备份在 `/data/backup/xiaozhi/before-d3c17d3ecdae/` 和 `/data/backup/xiaozhi/before-d3c17d3ecdae-retry/`。

2026-11-24 08:59:56，以 `e339ce93c5c492459270f9658d5e12659ceffc27` 为基线，将 Java API 和管理 Web 更新到 `127fd603872cc8750dbd33956e84f4b4f46aa22d`。迁移 `202611261600` 增加令牌重试及指定音箱重新授权所需字段。管理入口、重新授权前端资源、管理员用户详情、重新授权接口的未登录访问拦截、语音服务读取配置及公网 WSS 鉴权均通过。Python 语音、MySQL、Redis 和 Nginx 容器实例及 2.6.23 OTA 目录保持一致。原账号扫码后恢复实时积分由用户实机核验。

应用层包 `services.tar` 为 137,523,200 字节，SHA-256 为 `6896b0e777be78169351f844b0cba67cfc88dc88facbc5ff089911db70ea0ff1`。包、清单和发布核验位于 `/data/packages/xiaozhi/127fd603872c/component-upgrade/`，原 Compose、镜像基线和涉及的授权与绑定数据备份位于 `/data/backup/xiaozhi/before-127fd603872c/`。

2026-11-23 23:33:22，API、Web 和语音服务以 `fea5cc273ca4cec57d404cbfb3e3d5c1f664a344` 为基线执行应用层升级。部署前核验源镜像层，导入后核对目标镜像 ID、层与源码提交。数据库迁移创建全局功能插件配置表并补充和风 JWT 配置字段。MySQL、Redis 和 Nginx 的容器实例保持一致。

应用层包 `services.tar` 为 160,583,680 字节，SHA-256 为 `0995510fe2f561f14a468ae91aa236614971c73ae914225223ea5b25c21a2a35`。包、校验、发布回执和配置核验位于 `/data/packages/xiaozhi/e339ce93c5c4/component-upgrade/`。原 Compose、镜像基线和涉及变更的第三方服务及智能体插件关系备份位于 `/data/backup/xiaozhi/before-e339ce93c5c4/`。

和风天气使用生产 JWT，生产侧通过城市查询、实时天气和七天预报接口校验。高德沿用原凭据提供设备 IP 定位。默认智能体维持大模型自主函数调用，启用和风天气与 NewsNow。生产 Python 插件实测澎湃新闻、百度热搜、财联社、知乎、微博、IT之家和抖音均返回新闻列表。设备从屏幕查看待机天气及语音问答属于实机验收。

2026-11-23 00:22:08，API 和 Web 以 `132e1bacfc61a3efc4bd180b7d547f33038fa0b6` 的镜像层为基线更新至 `fea5cc273ca4cec57d404cbfb3e3d5c1f664a344`。按部署规范提交 `7673b86` 生成应用层包，114 校验基线后直接由 Docker 导入，核对镜像 ID、镜像层及源码提交后只切换两个组件。本次没有数据库结构变更。开发环境通过设备换绑后历史会话归属的回归测试。生产抽查 20 条近期会话，设备和集市账号 ID 均与会话历史归属一致。

应用层包 `services.tar` 为 137,472,000 字节，SHA-256 为 `fdea6b47fdee2d49557c0d7f63fad39eecf889cd0114b03bafee4c1e6c6d5e70`。包、清单和 `release.json` 位于 `/data/packages/xiaozhi/fea5cc273ca4/component-upgrade/`，原 Compose 和服务记录备份在 `/data/backup/xiaozhi/before-fea5cc273ca4/`。部署证据覆盖日常应用层升级，首次部署沿用规范的独立流程。

07:08:44 使用相同部署规范验证 Python 语音服务单独升级。以 `132e1bacfc61a3efc4bd180b7d547f33038fa0b6` 的语音镜像层为基线，只导入 `fea5cc273ca4cec57d404cbfb3e3d5c1f664a344` 的语音应用层并切换 `voice`。API、Web、MySQL、Redis、Nginx 的容器实例均与切换前一致。验证覆盖服务启动、配置读取、就绪和 WSS 握手鉴权，语音对话保留为实机验收项。

语音应用层包 `services.tar` 为 23,080,960 字节，SHA-256 为 `d5d6af521bd032b63f72e71430e9b99b63a7612ea2b7507266cb519843853306`。包、清单和 `release.json` 位于 `/data/packages/xiaozhi/fea5cc273ca4/voice-upgrade/`，切换前的 Compose 和容器记录备份在 `/data/backup/xiaozhi/voice-before-fea5cc273ca4/`。

### 固件 OTA 记录

2026-11-27 10:35:11，从 2.6.31（`989a6a2f45c1bd63a992e771a323006d8186d63b`）更新至 2.6.32（`e77513d2f33a5408a426b1b4c3ac6a72b24d6494`）。沿用 2.6.31 的屏保退出行为，增加积分不足、集市凭据及重新授权短提示。上游基线为 v2.6.0，构建变体为 `esp32-p4x-wifi6-touch-lcd-4.3-market`。

应用镜像为 4,107,536 字节，最小应用分区余 21,232 字节，SHA-256 为 `532a3aa84e4fd05f5441cdf76d4b4ee565cf8fa4cb67606c2a46955ac5b70c0b`。ESP32-P4 v3.1 至 v3.99 镜像信息、内部校验、本机和公网下载校验均通过。产物与回执位于 `/data/packages/xiaozhi/e77513d2f33a/`。2.6.31 的目录项和应用包已备份至 `/data/backup/xiaozhi/ota-before-2.6.32/`，旧包 SHA-256 为 `afaa6780073c0a6635955542389a64cd77a4275e2f1958cadc1e359446e14317`。

2026-11-27 08:38:41，从 2.6.29（`25e2bab2ac3165f29f070cda9c2a94979230a21d`）更新至 2.6.30（`864b25c54e8a94b35c9baee4319eab52f0049f09`），上游基线为 v2.6.0，构建变体为 `esp32-p4x-wifi6-touch-lcd-4.3-market`。屏保样式一增加薄雾、霾、沙尘、雪和雨夹雪图标。样式二用「空气」标注空气质量，并在城市名前显示白色定位图标。开发环境通过 800×480 LVGL 预览及 P4X 固件构建。

应用镜像为 4,106,352 字节，最小应用分区余 22,416 字节，SHA-256 为 `84ece909eb8fc1bb5a8bab4d55d3bdc75bb9bca29d0c1119441c5cc756aeb406`。生产本机与公网下载校验通过。产物与回执位于 `/data/packages/xiaozhi/864b25c54e8a/`，2.6.29 的目录项与原包备份位于 `/data/backup/xiaozhi/ota-before-2.6.30/`，旧包校验通过。

2026-11-27 08:06:40，从现网 2.6.28 更新至 2.6.29（`25e2bab2ac3165f29f070cda9c2a94979230a21d`），上游基线为 v2.6.0，构建变体为 `esp32-p4x-wifi6-touch-lcd-4.3-market`。本版恢复日历日期条的字号层级，支持同账号在原音箱重新绑定后找回记事，并包含屏保样式切换、番茄钟响铃和解绑确认改动。2.6.28 的完整提交未获核实，已备份实际目录项和旧包，旧包 SHA-256 为 `cb033156cd83d14fe1dcf64aad811faf5017bb931669615620f27c8e1a1da757`。

P4X 集市版应用镜像为 4,103,472 字节，最小应用分区余 25,296 字节，SHA-256 为 `f1e4582deb7b741bc822c7bff111035530e2f3de1feba8fed2eae39d5b05fef7`。生产本机与公网下载校验通过。产物和回执位于 `/data/packages/xiaozhi/25e2bab2ac31/`，上一版备份位于 `/data/backup/xiaozhi/ota-before-2.6.29/`。设备安装及交互分别以版本心跳和实机操作核验。

2026-11-26 12:19:29，从 2.6.24（`3bebadf7e6d298b5cefbce8f0b2213545c85b576`）更新至 2.6.26（`7ad0f764f5de8900124f52826aec85f54c24c584`）。版本号高于现场设备上报的 2.6.24.2 和 2.6.25。手机打开扫码链接后，音箱在左侧原位显示「已扫码」和「刷新二维码」，右侧保留核对码。产物对应当次已验证的扫码源码提交。远端固件分支的日历更新另有提交，随团队后续发布。

P4X 集市版应用镜像为 4,053,184 字节，最小应用分区余 75,584 字节，SHA-256 为 `aee6d9cd06d98f980c2c7ba2601dcc132c42756d5a0ff7b64cf77646e0e13f67`。生产本机和公网下载校验通过。产物和回执位于 `/data/packages/xiaozhi/7ad0f764f5de/`，前一版目录和应用包备份位于 `/data/backup/xiaozhi/ota-before-2.6.26/`。设备安装和扫码交互分别以版本心跳、真机操作核验。

2026-11-24 13:13:56，从 2.6.23（`e6a7de9a9cf396341f8d003494437c62b3df05d0`）更新至 2.6.24（`3bebadf7e6d298b5cefbce8f0b2213545c85b576`）。账号页新增积分充值，显示付款二维码并在集市确认到账后刷新积分。P4X 集市版应用镜像为 4,050,112 字节，最小应用分区余 78,656 字节，SHA-256 为 `ebb9bdaf223914950cc4eefa6962232199305edbc1c611bcd64d177f0bbaa4ba`。生产本机和公网下载校验通过。产物和回执位于 `/data/packages/xiaozhi/3bebadf7e6d2/`，前一版目录及应用包备份位于 `/data/backup/xiaozhi/ota-before-2.6.24/`。

2026-11-23 23:34:09，发布 2.6.23（`e6a7de9a9cf396341f8d003494437c62b3df05d0`）。固件用 OTA 响应和设备鉴权心跳恢复时间，校时后再执行本地提醒。日历调整日期数字和提醒圆点的位置。

P4X 集市版应用镜像为 4,035,936 字节，最小应用分区余 92,832 字节。应用包 SHA-256 为 `677531dda9fdd236516247d1e42d301601dfa2df1a8b13abf33f2e3ee7849cf5`，本机和公网下载校验通过。构建产物及回执位于 `/data/packages/xiaozhi/e6a7de9a9cf3/`，上一版 2.6.21 的目录和应用包备份位于 `/data/backup/xiaozhi/ota-before-2.6.23/`。发布核验时尚无 2.6.23 设备心跳。

2.6.21（`5f1c564fa141c47a0e6ea3dd181b128aa59eb22e`）于 00:47:19 发布。首页日期和天气改为约 24 px 并增加行距，日历调整了当前周、今天按钮、提醒圆点和页底提示。开发环境通过 800×480 LVGL 模拟检查和 P4X 构建。

测试设备两次在进度 98% 和 99% 时报告升级失败。公网网关在 00:49:20 和 00:51:17 记录 HTTP 200、响应体均为 4,034,560 字节。随后设备仍上报 2.6.20。应用包校验和 OTA 分区大小检查通过，设备端具体失败阶段待串口日志确认。

2.6.21 首次发布后暂停下发，包、发布回执和恢复回执位于 `/data/packages/xiaozhi/5f1c564fa141/`，应用包 SHA-256 为 `19bbf78557793495a6ff4d2f81baba975d295d24253f6acbad45d0d10c849f6a`。00:53:52 先恢复到 2.6.20，该目录备份位于 `/data/backup/xiaozhi/ota-before-2.6.21/`。

00:57:55 按确认恢复到 2.6.19，使用 `/data/backup/xiaozhi/ota-before-2.6.20/` 中的目录和应用包。2.6.19 应用包为 4,031,744 字节，SHA-256 为 `139133e1bfbf4550d32926d8d4181c352c7b85c66876c422f198a7c4c634ebc3`。恢复回执位于 `/data/packages/xiaozhi/abbeda268c40/ota-2.6.20-restored-to-2.6.19.json`。目录恢复后，设备仍按现有版本比较规则判断是否升级。

01:15:52 发布对照版本 2.6.22。源码基线为 2.6.19 的 `2f29df69b28a1d3a64bb461a03981af6357d16bd`，差异仅为 `CMakeLists.txt` 中的内嵌版本号。构建配置、分区表和资源与 2.6.19 一致。应用包为 4,031,744 字节，最小应用分区余 97,024 字节。SHA-256 为 `a90be085ee8c5f31b1f18835df476c0b2137a533c40d4adbbb9b0fcf12685fda`。发布回执位于 `/data/packages/xiaozhi/31d3790c00f0/ota-2.6.22.json`，切换前的 2.6.19 目录和原包备份在 `/data/backup/xiaozhi/ota-before-2.6.22/`。发布前后的后台服务实例一致。

2.6.22 复现升级末段失败。公网网关在 01:18:03 和 01:19:47 记录 HTTP 200，响应体均为 4,031,744 字节。未收到 2.6.22 心跳或此次升级的设备端错误。01:21:32 从备份恢复到 2.6.19，恢复回执位于 `/data/packages/xiaozhi/31d3790c00f0/ota-2.6.22-rollback.json`。

06:56:37 根据确认重新发布 2.6.21，复用原应用包，大小 4,034,560 字节，SHA-256 与首次发布一致。重新发布回执位于 `/data/packages/xiaozhi/5f1c564fa141/ota-2.6.21-republish.json`。本次切换前的 2.6.19 目录和原包备份在 `/data/backup/xiaozhi/ota-before-2.6.21-republish/`。发布前后的后台服务实例一致。

测试音箱确认原包升级成功。07:01 复查到 2.6.21 的心跳，最后上报时间 07:01:09。此前失败更可能与下载链路偶发异常有关，具体环节未确认。

## 验证与运维资料

- [设备自动登记与首次扫码](../archive/completed/identity/speaker-auto-enrollment-2026-11-20.md)
- [音箱触屏配网开发验证](../archive/completed/firmware/speaker-touch-wifi.md)
- [固件界面与诊断验证](../archive/completed/ui/speaker-dark-ui-implementation.md)及[语音与显示诊断](../archive/completed/firmware/speaker-display-diagnostics.md)

服务器的部署视图、运维说明和更新日志位于 `/data/`。开展生产操作前核对实际运行状态。较早验收按各自日期、版本和实测范围查阅。
