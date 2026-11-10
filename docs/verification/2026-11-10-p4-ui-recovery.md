# P4 原有 UI 恢复与真实语音验证

日期：2026-11-10，Asia/Shanghai；设备脱敏编号 `P4-01`。当前应用 SHA256 为 `fdf422a87cde8d2962d33f0b7189711d1eb23d9c2c849fb8e6766efab958a077`。

恢复镜像已构建、刷写并独立回读，现场用户确认「回复语音完整」「已恢复」。本记录证明原首页和真实语音在当前固件上恢复；服务器侧接续事项及旧版重启记录见 [设备与固件交接](../handoff/2026-11-10-device-firmware.md)。

## 原因与对照依据

首次修改测试 OTA 地址时，从固件工程检出的较早 `main` 基点 `9bb2785aaec77346f473d1c932eca25743ed2d42` 构建。原设备镜像含有的定制待机仪表盘、公司 Logo、日历、番茄钟和音乐页面不在该源码基点，因此刷入后只保留基础文字界面。

固件仓库 reflog 显示前一天从 `ESP32P4` 切到较早 `main`，首次测试分支 `fix/6-test-ota-origin` 随后从该基点创建。完整定制代码仍保留在 `ESP32P4` 的 `f639dd332dd603e3b9ec7c00bd888a97b9770baa`。首次刷写只核对了芯片、分区、构建和地址，遗漏了原镜像定制功能的比较。

| 对照 | 原定制构建 | 首次基础测试构建 | 当前恢复构建 |
| --- | --- | --- | --- |
| 仪表盘 `display/standby_dashboard.cc` | 编入 | 缺失 | 编入 |
| Logo `display/assets/hncig_logo_i2.c` | 编入 | 缺失 | 编入 |
| `services/iot_telemetry_service.cc` | 编入 | 缺失 | 编入 |
| 仪表盘、时钟及 Logo 最终 ELF 符号 | 存在 | 缺失 | 存在 |
| 原有 UI 检查脚本退出码 | 0 | 1 | 0 |

原定制构建和首次基础测试构建的 UI 配置开关一致，说明回退来自源码选择。当前恢复构建的整个 `main/` 编译源码列表与原定制构建一致，生成配置相比原定制构建仅 `OTA_URL` 不同。该结论不表示逐字节复现旧 Windows 产物，也不表示逐一实测了音乐、番茄钟或 telemetry 功能。

## 恢复源码与构建

使用独立 checkout 的 `fix/6-restore-p4-ui`，基点为完整 P4 提交 `f639dd332dd603e3b9ec7c00bd888a97b9770baa`，保留原工程工作区及旧构建。复用修改前备份的 `sdkconfig`，在 `sdkconfig` 和 `sdkconfig.defaults` 设置测试 OTA 默认地址，配置变更尚未提交到固件仓库。

使用 ESP-IDF v5.5.5 和已有 Python 3.11 环境，以 2 个任务并行构建。首次干净构建因缺少生成的 `main/assets/lang_config.h` 停止，现场构建包装脚本改为先调用原有 `scripts/gen_lang.py --language zh-CN --output main/assets/lang_config.h` 再编译，随后成功退出 0。脚本还检查编译列表与最终 ELF 中的仪表盘、Logo 符号，避免源文件存在却未链接的遗漏。

| 当前产物 | 值 |
| --- | --- |
| 版本与构建时间 | 2.2.6；Nov 10 2026 11:39:23；ESP-IDF v5.5.5 |
| 应用 SHA256 | `fdf422a87cde8d2962d33f0b7189711d1eb23d9c2c849fb8e6766efab958a077` |
| ELF SHA256 | `5ca22431212c46cadf02f77ac7b20f16c68f1d979d8b21596f97518c64b104c1` |
| 应用/分区大小 | 4,081,632 / 4,128,768 字节，剩余 47,136 字节（约 1.1%） |
| 芯片兼容范围 | revision 3.1–3.99，现场主控为 3.2 |
| esptool 镜像检查 | Checksum valid、Validation Hash valid |
| OTA 默认地址 | `https://bridge.hncig.cn:18443/xiaozhi/ota/` |
| 当前缓存 WSS | `wss://bridge.hncig.cn:18443/xiaozhi/v1/` |

## 写入与独立回读

写入前重新备份已通过语音验证的基础测试应用、NVS、otadata 和分区表。esptool 仅写入 `ota_0` 应用地址 `0x20000`，保持 Flash 参数，输出 `Hash of data verified`，退出码 0。

| 独立核对 | 结果 |
| --- | --- |
| 整个应用 Flash MD5 及 ELF 标识 | 与当前恢复镜像一致 |
| Wi-Fi 设置、设备 UUID | 保留 |
| 分区表、OTA 启动记录 | 与写入前一致 |
| 8 MiB assets 分区 | 前后 MD5 一致：`ee621fc1fc848162bfead6c99aafdb41` |
| bootloader | 本次未写入 |
| 正常启动 | 运行当前新 ELF，800×480 逻辑分辨率、270° 旋转；联网、完成激活流程、进入 `idle` |
| WSS 配置 | 正确地址，token 存在；凭据保留在现场 |

这是 USB 应用刷写与回读验证，不能替代网络 OTA 发布、升级回执或自动回滚验收。

## 实际问答与用户确认

串口观察于 12:24:09 +08:00 开始，在请现场测试之前已建立。用户使用唤醒词「你好小智」并提问，设备在 12:25:00 输出识别文本「请用一句话介绍你自己」，12:25:02 和 12:25:05 分别输出回答「我是你的声音助手」「负责把想法变成简洁、温暖、靠谱的回答」，12:25:09 从 `speaking` 返回 `listening`。

用户随后确认「回复语音完整」，并对原时钟、天气、日历首页回复「已恢复」。当前测试未单独重新确认错误提示页面。时间均为串口观察时间，不记录为服务器 ASR/LLM/TTS 的独立耗时。

旧基础镜像 `e22af020b6373f1fddde3bc9282ba4671f2f422d21cdebd57aa59400a26eea7b` 的设备/服务重启和 ASR/LLM 各 2 成功、0 失败为历史结果。当前恢复版本未重复服务重启，也未取得对应服务器调用计数；没有把旧统计移到当前镜像。

## 证据来源与范围

[脱敏结果摘要](evidence/2026-11-10-p4-ui-recovery/result.json)从现场清单、独立回读、UI 检查、串口观察及用户反馈中按字段提取，分别注明直接观察和用户转述。当前镜像校验值与本地应用文件一致。

原始材料保存在现场项目的 `.runtime/firmware-check/2026-11-10/`：当前恢复记录在 `ui-recovery/evidence/`，旧基础测试记录在上层 `evidence/`。其中 `recovery-manifest.json`、`ui-regression-diagnosis.json`、`device-after.json`、`observation-20261110T042409Z.json` 和 `user-confirmation.json` 为本次摘要来源；旧服务重启统计来自 `evidence/voice-after-service-restart.json`。

GitHub 仅同步这份报告、交接和脱敏摘要。固件二进制、设备配置、原始日志、完整身份及备份保留在现场；当前记录尚缺服务器运行版本、会话/音频关联、实际等待时间和资源观测。此同步不修改服务器，也不关闭 #6 或 #2。
