# 第一批：官方核心与设备身份

与服务端 feat/upstream-restoration-phase1 配套。完整范围见服务端 docs/development/upstream-restoration-phase1-plan-2026-11-17.md。此批完成后等待真机验收，第二、三批不自动开始。

## 来源与运行边界

- 官方基线：78/xiaozhi-esp32 v2.2.6，49ac8a6da399f27a9546d4f73640b7f86c24bac6。恢复主程序、音频、协议、显示、工具、OTA 及 P4/common 必需文件；不更新其他板型功能。
- 本项目首批应用版本为 2.2.7，用于与已有 2.2.6 比较升级，不表示升级到官方另一个版本。SDK 使用开发容器既有 ESP-IDF 5.5.5；依赖固定在 locks/phase1-p4-dependencies.lock。
- 现用 Waveshare ESP32-P4 4.3 寸，480×800，ES8311/ES7210、音频及触摸引脚沿官方同型；保留 revision 3.1+ 编译目标及 partitions/v2/16m.csv。应用分区各 0x3f0000，NVS 0x4000，assets 8MB。没有改分区或整片擦写。
- sdkconfig.defaults.account 是独立第一批 profile，只编译 main/account 三个文件，旧 device_control 和音乐/录音/仪表盘等源文件留存、未参与构建。

| 能力 | 处理与例外 |
| --- | --- |
| 配网、唤醒、监听、音频上传/播放、打断、重连 | 官方原版；登录准入只放在开始交互与连接边界，不在音频帧内检查许可 |
| 设备 MCP | 官方原有发现、调用和用户专用工具边界 |
| 登录 | 后台返回集市 verification_uri/user_code；二维码、核对码、状态和重扫。OAuth device_code、用户令牌、模型 Key 都留在后台 |
| 设备身份 | 只读兼容已有 device_ctrl / identity_v1、device_id 记录；验证硬件标识，不使用可变 Wi-Fi MAC 或配网 UUID 代替已登记设备 ID |
| 在线 | 单独任务以设备凭据每 20 秒心跳，另每 5 秒获取使用状态；语音票据只在内存，不反复写 NVS |
| 解绑 | 本机两次确认，立即结束交互；请求 ID 和预期版本保存在 device_ctrl，网络失败/重启后继续确认。只解绑本台，不撤销账号共享授权 |
| OTA | 同源认证检查原版 /ota/，使用原版 ai_ota 下载、安装、验证和重启。型号上报明确为 waveshare-esp32-p4-wifi6-touch-lcd-4.3，避免与其他屏型混用 |
| 重置 | 配网保持原入口；偏好设置清理保留 device_ctrl 身份及未完成解绑收据，不隐式解绑、不擦 OTA 元数据。损坏 NVS 禁止自动整片清空，需要明确恢复 |

LVGL 与 esp_emote_gfx 各带 QR 生成器，新增二维码用到两者时存在同名符号冲突；只在构建中给 LVGL 私有副本加命名空间，未改 SDK/托管包或使用忽略链接错误的选项。NVS 整片擦除增加链接保护，避免 Wi-Fi 库的错误恢复路径丢失硬件凭据。

## 身份交付与升级

已经交付身份的设备通过应用 OTA 保留原 NVS，无需重新登记、写凭据或擦除。第一批不启动旧复杂交付/修复流程。

新设备在后台“设备管理”登记，设备 ID 使用管理员明确登记的值；真实硬件标识为 ESP32-P4 eFuse 默认 MAC 去掉冒号的小写 12 位字符串，设备未交付时显示该标识。凭据只显示一次。私有 JSON 存 runtime，权限 0600：

```json
{"version":1,"hardware_id":"aabbccddeeff","device_id":"管理员登记的设备ID","credential":"管理员登记返回的独立设备凭据","ota_url":"https://开发环境域名/xiaozhi/ota/","control_origin":"https://开发环境域名"}
```

`scripts/make_account_identity.py --identity <私有JSON路径> --output-dir /opt/xiaozhi-dev/<新的私有目录>` 只准备出厂 NVS 镜像，不连接串口或刷写。镜像包含秘密，不上传到固件管理。它仅用于明确安排的新设备出厂交付；不能覆盖已有设备的 NVS。首次身份写入、刷机或部署另按用户授权执行。

## 构建与证据

服务端 deploy/local-zhou/build-speaker-firmware.sh 在 xiaozhi-dev 内构建显式同步的 /opt/xiaozhi-dev/restoration-firmware。原生协议检查使用 scripts/test_account_protocol.sh，工具和产物都在 /opt/xiaozhi-dev。完整应用构建通过不表示屏幕、声学、C6 网络、OTA Flash 或真实扫码已通过。

首次构建产物与校验值由服务端交付记录固定；不提交二进制或密钥。真实设备连接哪个服务版本、OTA/WS 可达地址、所需提供者凭据和回退镜像须先核对。请先验证配网、扫码、问答、首音、打断、连续对话、解绑重扫、重启/断网恢复、独立心跳、原版设备工具和 OTA，再安排第二、三批。
