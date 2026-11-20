# 微雪 P4X 4.3 寸 AI 集市版

1. 固定官方 `v2.6.0`（`4de00aed740595a8713b01f8332b9b2cf30685bc`）和已完成同版本 OTA 对照的 ESP-IDF commit `f0114250e205e33a945e2f3e7ec40085c5a029a2`。
2. 新增设备身份和扫码授权。管理 API 负责 AI 集市设备授权、用户绑定、令牌和模型调用凭据。联网后音箱通过独立设备凭据调用 `/xiaozhi/speaker/v1`，显示二维码与核对码并上报心跳。状态接口提供短期 WebSocket 票据。
3. 保留官方音频、WebSocket 握手与音频帧处理。账号权限变化在主任务关闭会话。软件更新通过经设备认证的官方 OTA 检查入口，沿用原版分区写入、校验和重启。
4. 核对现有后台登记的 4.3 寸产品板型与 P4X 构建变体。跨 SDK 应用升级须先实机核对 Bootloader、资源分区、已有身份和回退。生产发布前验证扫码、断线重连、语音、解绑、OTA 与重启。

## 构建

在 ESP-IDF 6.1 工具链环境中构建。

```sh
cp locks/esp32p4-v2.6.0.lock dependencies.lock
python scripts/build.py waveshare/esp32-p4-wifi6-touch-lcd --name esp32-p4x-wifi6-touch-lcd-4.3-market --language zh-CN --zip
```

构建变体为 `esp32-p4x-wifi6-touch-lcd-4.3-market`。设备心跳和 OTA 继续上报已登记的 `waveshare-esp32-p4-wifi6-touch-lcd-4.3`，语音连接保持后台提供的 WebSocket 协议版本。

## 身份交付

登记设备后可在私有目录准备 NVS 镜像。输入 JSON 包含 `version`、`hardware_id`、`device_id`、`credential`、`control_origin` 和 `ota_url`。设备硬件标识使用 P4 eFuse MAC 的小写无冒号形式。HTTPS 管理 API 源及同源 OTA 地址从独立交付配置提供。凭据文件和生成产物都留在私有运行目录。

```sh
python scripts/prepare_speaker_identity.py DEVICE_IDENTITY_JSON FACTORY_NVS_BIN
```

该工具生成新设备交付所需的 NVS 镜像。已有设备使用其现有的 `device_ctrl/identity_v1`，通过应用 OTA 保留分区。首次 USB 完整安装和恢复时先备份并确认原设备身份。
