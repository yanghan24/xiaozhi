# Waveshare P4X 4.3 寸集市版

本文说明 P4X 产品的模块、边界和验证入口。系统分层与构建命令见[固件 README](../README.md)，界面与定稿见 [UI 规范](ui/README.md)。

## 板型和产物

| 项目 | 当前实现 |
| --- | --- |
| 硬件 | Waveshare ESP32-P4X 4.3 寸，搭配 ESP32-C6 网络协处理器，800×480 横屏 |
| 构建目录 | `waveshare/esp32-p4-wifi6-touch-lcd` |
| 中文构建变体 | `esp32-p4x-wifi6-touch-lcd-4.3-market` |
| 心跳与 OTA 上报板型 | `waveshare-esp32-p4-wifi6-touch-lcd-4.3` |
| 分区表 | [16 MiB 分区表](../partitions/v2/16m.csv)，两个各 `0x3f0000` 的应用 OTA 槽和独立资源分区 |
| 应用 OTA | `build/xiaozhi.bin`，沿用原版应用升级和启动流程 |
| USB 整包 | 构建时加 `--zip`。整包烧录或擦除须单独确认设备数据保留要求 |

保持构建变体、硬件和后台 OTA 匹配。设备管理上报板型按现有协议保持稳定。调整芯片修订、分区、bootloader 或资源格式时，先单独核对 OTA 兼容性和恢复流程。

## 功能与源码入口

以下 `speaker_*` 文件位于 [P4X 板型目录](../main/boards/waveshare/esp32-p4-wifi6-touch-lcd/)。

| 功能 | 模块与边界 |
| --- | --- |
| 配网与 Wi-Fi | `speaker_wifi_view` 管理触屏界面，`speaker_wifi_setup` 后台任务调用 Wi-Fi 组件。[组件补丁](../cmake/p4x-touch-wifi.patch)支持运行中扫描、试连和热点。首次触屏与手机热点共用提交逻辑。浏览列表或打开热点时保留已连网络。确认后试连，成功取得 IP 后保存，失败时恢复原网络。 |
| 账号、积分、解绑 | [`speaker_account`](../main/speaker_account/) 管理身份和认证连接，账号页显示扫码、绑定资料及积分。首次配网完成后自动登记。用户经手机完成 AI 集市授权，设备更新状态并取得语音票据。解绑按本台设备的既有流程确认。 |
| 开机、首页、屏保与导航 | `speaker_home_display` 协调系统画面和应用优先级，`speaker_boot_view`、`speaker_launcher_view`、`speaker_screensaver_view`、`speaker_background` 管理各自的 UI 与资源。`speaker_navigation` 管理应用入口、返回和边缘手势。 |
| 全屏对话 | `speaker_conversation_view` 显示语音状态与字幕，`speaker_wave` 控制波带绘制。收音和实际音频播放从现有应用和音频服务接收状态。 |
| 设置 | `speaker_settings_view` 维持设置首页及声音和显示、屏保、关于设备页面。账号和 Wi-Fi 路由进入各自模块。滑块与语音控制复用音量、背光服务，按现有规则更新与保存。 |
| 待机天气 | `speaker_standby_weather` 使用设备认证请求小智管理 API，在空闲阶段刷新。供应商和认证由后台配置。 |
| 日历、记事与提醒 | `speaker_calendar_view` 显示可连续横滑的日期条和选中日期的事项。`speaker_memos` 提供 `self.memo.create`、`self.memo.list`、`self.memo.cancel`，按设备当前绑定关系隔离本地记事，支持到期时提示和响铃。 |
| 番茄钟 | `speaker_pomodoro_view` 管理计时、暂停、重置和提示，复用现有提示音。 |
| 计算器 | `speaker_calculator` 处理本地四则运算与百分比，`speaker_calculator_view` 管理触屏键盘、实时结果及最近 6 条历史。历史仅在本次开机期间保留。 |
| SD 卡、本地音乐与录音 | `speaker_sd_storage` 管理挂载与文件，`speaker_music` 播放 MP3/WAV，`speaker_recorder` 保存 WAV。两种应用各有页面，复用同一音频服务。 |
| 设备诊断 | [`speaker_account`](../main/speaker_account/) 下的语音耗时、播放和显示诊断沿用现有通用上报接口，后台按设备限时开启上传。 |

## 系统设置和音频边界

- 保存的 Wi-Fi、设备身份、用户设备归属所需标识、屏保配置和记事沿用现有存储。应用 OTA 更新应用槽，现有 NVS 和 SD 数据按原流程保留。整包刷写或恢复需按目标场景单独处理。
- 本地音乐在语音对话需要使用扬声器时让出。主动录音期间独占麦克风，暂停语音唤醒。录音停止后恢复现有唤醒流程，保存收尾由工作任务负责。
- 功能模块通过现有线程调度、音频接口和页面注册接入。需要系统切换、长期阻塞或跨设备持久化的功能先明确负责模块和边界。
- 新增外部业务通过小智管理 API 或现有服务端能力提供。设备向小智发送经认证的业务请求，供应商 Key 留在服务端。

## 验证入口

| 改动范围 | 开发检查和实机确认 |
| --- | --- |
| 界面与设置 | 800×480 横屏、触摸、返回手势、状态优先级和设置持久化。实机核对字形、闪屏、刷新与触摸。 |
| Wi-Fi 和身份 | 开发检查首次触屏与手机配网、取消、失败恢复及授权状态。实机核对 DHCP、换网、扫码、解绑与重连。 |
| 音频、本地应用、语音 | 开发检查应用状态、文件与队列交互。实机核对唤醒、收音、播报、打断、音乐让出、录音停止及提醒。 |
| 固件升级 | 核对版本、板型、镜像校验、分区余量与发布范围。实机区分 OTA 下载、安装、重启和版本心跳。 |

开发检查与生产发布及真机结果分别记录。具体发布步骤使用服务端工程的 `deploy/production/README.md`。
