# 第一阶段设备授权适配

本分支基于 dev/firmware-zhou（69ea28d）适配小智管理后台 speaker/v1（响应 protocol_version=2）。这是本地开发代码，未部署 114、未刷机、未改变既有 OTA 交付。后台方案见服务端分支 feat/speaker-phase1 的 docs/development/speaker-phase1-implementation.md。

开启 CONFIG_XIAOZHI_DEVICE_CONTROL 与 CONFIG_XIAOZHI_SPEAKER_PROTOCOL_V2；sdkconfig.defaults.speaker 提供显式选择。原协议仍由原配置选择，新旧响应不自动兼容。使用原设备身份私有格式，必须提供明确的 control_origin HTTPS 信任源；新登录不接受 OTA 响应替换控制来源，也不回退到普通设备/公网服务。

固件只调用认证心跳、设备状态、登录事务及本台解绑，显示集市 verification_uri 和 user_code（屏幕 Code），不接收 device_code、用户令牌或模型 Key。集市轮询和身份校验全部由管理后台执行。20 秒心跳独立于绑定、语音；状态约 5 秒同步，声音许可最多 60 秒，断网和认证失败关闭本地声音许可。登录拒绝/过期停止旧二维码，通过已有账号入口重新扫码。

复用原账号面板、二维码及倒计时、单台“退出并解除绑定”确认、音频代际失效和维护 API。解绑请求在网络超时后重试同一 request_key 和原 permission_version，不将结果不确定的请求改为新归属下的操作。账号显示缺失余额为“未知”；客户详细资料和余额刷新由管理后台负责。

开发链路检查后补充：新协议在可选 OTA 版本检查之前启动 DeviceControl，登录和心跳不依赖版本检查成功；OTA 能力声明为 2。二维码过期/拒绝后，已有账号入口可重新扫码，倒计时不再提示自动更新。此处仅调整启动衔接和显示，没有新增 OTA 发布能力。

开发检查包括新协议解析器及实际 DeviceControl worker 的 RTOS/HTTP 受控夹具：扫码核对码、二维码超时/拒绝、错误版本/重复字段、旧权限拒绝、声音许可超时、普通状态更新不打断回答、解绑的本地立即停止/超时重试，以及解绑后仍有心跳。命令入口 scripts/test_speaker_protocol.sh；编译、依赖和生成产物均放在 xiaozhi-dev 的 /opt/xiaozhi-dev 命名卷，未安装宿主依赖。新显示文案的中文字符集合未超出原绑定字体覆盖集合；尚无 LVGL 或实机视觉验收。

## P4 目标构建（2026-11-17）

在同一个 xiaozhi-dev 内安装独立 ESP-IDF 5.5.5/P4 工具链并完成交叉编译、链接和镜像/分区容量检查，没有创建或重建容器。GitHub 克隆失败后，改用[官方完整归档](https://github.com/espressif/esp-idf/releases/tag/v5.5.5)，下载 SHA-256 为 48fffe90304573ef366bda06e38c92fd65c6f3636d785ce103ff82e4647964c8，已校验。工具位于 /opt/xiaozhi-dev/tools，独立 SDK Python 环境与语音服务环境分离。

首次目标构建停在原 SD 服务已有的长文件名/UTF-8 编译检查；只在新构建 profile 补充 FATFS_LFN_HEAP、MAX_LFN=255 和 API_ENCODING_UTF_8，未改 SD 实现。按同一 P4 板型重建通过。源码配置为微雪 4.3 寸、P4 revision 3.1+、原 16 MB 分区，启用 speaker v2。产物 /opt/xiaozhi-dev/run/speaker-p4/xiaozhi.bin，3,925,760 字节，SHA-256：2e7110c2094fccbeef9efd04cfa1aecc95bf7d7a9d46d225160cd4a1d3b40126。最小应用分区 0x3f0000，余量 0x31900（约 5%），本轮未扩分区。

构建入口为服务端 deploy/local-zhou/build-speaker-firmware.sh，使用 runtime 的同步源码副本和独立 build/sdkconfig，限制 Ninja 为 4 个任务；日志 /opt/xiaozhi-dev/logs/speaker-s6-p4-build-final.log。保留基础开发版本号 2.2.6，此文件仅为开发构建产物，未发布、未刷机。目标构建不替代 Ubuntu 环境的真实扫码、声音、断网重连及 OTA 验收。
