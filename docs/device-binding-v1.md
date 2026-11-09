# 设备二维码绑定固件 v1

本固件按小智项目 [设备绑定与控制协议 v1](https://github.com/yanghan24/xiaozhi/blob/d376d21c9684fcbd63667a7127a936c1091777ee/docs/contracts/device-binding-v1.md) 实现设备侧，对应 [#13](https://github.com/yanghan24/xiaozhi/issues/13)。服务器接口和手机 OIDC 页面由服务器侧实现，[交接清单](https://github.com/yanghan24/xiaozhi/blob/d376d21c9684fcbd63667a7127a936c1091777ee/docs/handoff/device-binding-v1-server.md)说明双方边界。源码基线为用户提供版本 `9bb2785aaec77346f473d1c932eca25743ed2d42`，未混入另一个上游版本。

## 启用与行为

产品模式配置项为 `CONFIG_XIAOZHI_DEVICE_CONTROL=y`。新增 `sdkconfig.device-binding.defaults` 提供微雪 ESP32-P4 WiFi6 Touch LCD 4.3 的启用配置；通用固件默认关闭该模式。产品模式同时开启 LVGL 二维码和 CA 证书包。

- OTA 请求声明 `device_control_capabilities: [1]`，从受信任 HTTPS 响应取得控制基址。产品模式不使用旧数字激活、MQTT 回落或 NVS 内旧语音 token。
- 未绑定显示绑定码，退出或需要授权显示原账号恢复码；正常使用时收起二维码，触摸“账号”可进入本台账号页面。手机确认绑定，无音箱确认按钮。
- 设备以独立凭据同步状态、申请请求和确认已停播。二维码及语音许可使用保守单调期限；请求重试不延长旧码，网络故障不续租。
- 许可失效即拒绝新的收发和播放，旧连接、旧音频及排队回调按本地代次丢弃。停播 ACK 要等待软件队列及正在执行的驱动输出停止。配网/错误等本地系统提示音保留。
- 产品模式保留本地唤醒识别及唤醒词文本通知，禁用历史唤醒 PCM 上传，避免跨许可保留的录音被当成新会话音频。
- 控制 HTTPS 使用单个网络拥有者、异步 DNS/TLS 和整体 25 秒截止，禁止重定向及向未知目标传凭据。WSS 握手保留外部非默认端口。

## 私有交付配置

以下值由受控交付或维修流程写入 NVS `device_ctrl` 命名空间。固件只读取，不生成匿名替代凭据，不在二维码或普通日志中显示它们。

| key | 含义 |
| --- | --- |
| `device_id` | 小智预登记档案中的设备标识 |
| `credential` | 此设备独立且可撤销的接入秘密 |
| `ota_origin` | 明确授权的 HTTPS OTA origin，含实际外网端口 |
| `control_origin` | 可选，授权跨 origin 控制服务；留空则必须与 OTA 同 origin |

Origin 由 scheme、域名及端口组成，例如 `https://speaker.example.com:8443`，不带业务路径。实际 OTA URL 仍读取 `wifi/ota_url`，为空才使用 `CONFIG_OTA_URL`；已有明文 HTTP 地址需要受控迁移。首版 WSS 与控制服务使用同一域名、端口。当前 origin 解析支持域名和 IPv4，IPv6 输入保持关闭。

该读取约定不代表正式量产烧录、正常恢复出厂保留及异常补发工艺已验收。缺少有效配置、TLS 校验失败或未知状态时保持产品语音关闭。不要通过修改 MAC、自报 UUID、关闭认证或关闭证书验证替代交付流程。实际秘密和私有配置不提交 Git。

## 构建与开发预检

使用 ESP-IDF 5.5.2 或更新的兼容版本；本地验证工具链为 ESP-IDF 5.5.5。已有 `sdkconfig` 的构建须显式打开产品模式，新增 defaults 文件不会覆盖已有配置。新目录可使用：

```bash
idf.py -B build/device-binding -DIDF_TARGET=esp32p4 \
  -DSDKCONFIG=build/device-binding.sdkconfig \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.device-binding.defaults' \
  build
```

发布前核实实物板型、芯片 revision 与 Flash，再选择对应配置。本改动沿用已有 16 MB 分区，未进行 32 MB 迁移；每次构建必须通过镜像大小检查，不能只看 Flash 总容量。

在仓库根目录运行主机协议及音频停止屏障测试：

```bash
cmake -S tests/device_control -B build/host-control -DIDF_PATH=/path/to/esp-idf
cmake --build build/host-control
ctest --test-dir build/host-control --output-on-failure
```

主机测试覆盖协议和时间边界，不能代替真实网络、屏幕、驱动与音箱验收。公网入口、OIDC 客户端和逐设备配置就绪后，按主协议第 9 节在 Ubuntu 服务环境验证扫码、首次绑定、单台退出/恢复、断网到期及重启。真实规范通测试继续暂停，正式集市 TTS 尚待外部条件。

## 本次开发验证

2026-11-09 在隔离固件副本完成以下检查，未连接或烧写音箱，未请求真实集市接口：

| 检查 | 结果 |
| --- | --- |
| 主机协议、受控音频停止屏障、处理器缓冲代次 | 3 个测试程序全部通过 |
| ESP32-P4 编译 | ESP-IDF 5.5.5、RISC-V GCC 14.2，微雪 4.3 板型、产品模式开启，编译及链接通过 |
| 分区容量 | 保持 `partitions/v2/16m.csv`；镜像 3,678,384 字节，最小应用槽 4,128,768 字节，剩余 450,384 字节（约 11%） |
| 格式与差异 | 新增 C++ 文件及既有文件修改行执行 clang-format，`git diff --check` 通过 |
| 规范审查 | 无硬违规；重复权限分支及消息来源参数封装为可选后续整理 |
| 规格审查 | 无遗留发现；正式交付配置、真机与外部服务验证仍待验收 |

本地开发镜像为 `build/xiaozhi.bin`，SHA-256：

```text
3debd29d52e3e4579bdd762f1a0401cabd609c5b06ce66dff296391fab8eff46
```

该镜像用于开发验证，构建目录与产物未纳入 Git。当前本地配置的 P4 最低 revision 为 3.1，刷写前仍须核实实物 revision、板型和私有配置，并执行真机验收。
