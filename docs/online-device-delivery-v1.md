# 联网烧录与安全续作

对应 [#41](https://github.com/yanghan24/xiaozhi/issues/41)。通用镜像与逐台身份分离：管理员授权一次工具，
工具通过公网 API 登记并获取独立凭据，通过本机串口写入，然后由设备自身联网验证，最后才启用准入。
完成交付不建立客户归属，手机仍须扫描正式设备二维码并明确确认绑定。

现有 PR #26 `885918d` 已通过 PR #48 归档公网部署，但还没有新 delivery API。新服务端必须先测试并部署；
截至本次实现，没有为本台签发新凭据，没有写入 Flash，没有替代真实手机和扬声器验收。

## 镜像与受支持设备

适用已验证的 Waveshare ESP32-P4 WiFi6 Touch LCD 4.3，P4 revision 3.1–3.99 和 C6 无线协处理器。
实际连接为 CH343 UART，维护镜像使用 `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`。
工具只接受现有 `partitions/v2/16m.csv`，不迁移分区，不替换 bootloader、otadata、assets 或 C6 固件。
实际 Flash 至少 16 MB；当前样机为 32 MB。不同布局、试运行未确认的启动记录、签名/加密设备会停止处理。

在已加载 ESP-IDF 5.5.5 的固件目录配置，私有天气/IoT 设置保留在被 Git 忽略的 sdkconfig 中。
已有 sdkconfig 需要明确启用 `CONFIG_XIAOZHI_ONLINE_DELIVERY=y`，defaults 不覆盖已有值。
新配置可用：

```bash
firmware_dir="$PWD"
idf.py -B "$firmware_dir/build/product" -DIDF_TARGET=esp32p4 \
  -DSDKCONFIG="$firmware_dir/build/online-product.sdkconfig" \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.device-binding.defaults;sdkconfig.online-delivery.defaults' build
idf.py -C tools/device-provisioning -B "$firmware_dir/build/maintenance" -DIDF_TARGET=esp32p4 \
  -DSDKCONFIG="$firmware_dir/build/online-maintenance.sdkconfig" build
python scripts/verify_product_build.py build/product
```

`build_artifacts` 还校验两个应用的 P4 镜像摘要、芯片 revision、相同分区表和产品 ELF 摘要。
产品仍保留原时钟、天气、日历、Logo、音乐、录音、IoT 源码以及产品语音控制闸门。
只通过 `online_delivery.py` 的应用槽写入流程安装；ESP-IDF 构建结束打印的通用 flash 命令包含其他分区，不属于本交付步骤。

## 一次授权与逐台交付

管理员通过**新产品 API 所对应账号库**的现有受保护入口正常登录，保留 SM2/验证码流程。
将登录结果的 `token` 保存在本地 0600 JSON 文件；工具不接收明文密码，也不接受命令行 token。
客户 OIDC 会话不能代替运营授权。工具授权令牌只用于交付，期限一小时。

```bash
umask 077
install -d -m 700 .private/delivery
python scripts/online_delivery.py authorize \
  --origin https://bridge.hncig.cn:18443 \
  --admin-token-file .private/operator-login.json \
  --session .private/delivery/tool-session.json
python scripts/online_delivery.py deliver \
  --session .private/delivery/tool-session.json \
  --journal .private/delivery/device-01 \
  --port /dev/ttyACM0 \
  --product-build build/product --maintenance-build build/maintenance
```

每台实物使用一个独立 journal 目录，批次复用同一 session。端口名只用于连接，每次执行都重新读取 P4 eFuse 身份。
后台稳定 ID 由服务器生成；不将 P4 MAC 当 C6 MAC，也不将 MAC 当秘密。

工具依次执行：

1. 读取真实 Flash、revision、分区表及当前有效启动槽，备份完整启动前缀和应用槽，独立核对设备 MD5。
2. 只写产品应用槽，独立回读镜像和 MD5，核对受保护启动区域及 assets 未变化。
3. 读取产品串口结构化报告中的 P4/C6、实际运行 ELF 摘要和未配置身份状态。
4. 用已持久化的操作号请求后台登记，后台先禁止产品准入。六字段 identity 只写入私有 journal。
5. 刷入 UART 维护应用，以相同 P4 标识和固定写入操作号提交身份；必须收到匹配的已提交/读回回执。
6. 恢复产品应用并再次回读、核对原分区和 assets。产品报告的稳定 ID、P4/C6 和 ELF 必须匹配。
7. 等待设备通过独立凭据调用公网 `device-check`，再由工具提交完成；只有后台返回 delivered 才显示可交付。

设备在 pending、错误凭据、硬件不匹配、TLS 失败或配套接口不可用时保持语音关闭。
实际设备验证使用其独立凭据，不是硬件远程证明；现场工具需在受控连接上执行。

## 中断恢复

断网、程序中止、USB 回执丢失时，保留整个 journal 和原镜像，重新执行同一条 `deliver` 命令。
工具在网络及 USB 变更前原子写入并 fsync 操作号，续作不随机生成新身份、不删除后台档案、不自动擦除 NVS。
失去回执的写入重复提交同一 identity 和写入号。已经 delivered 的正常复查不会再次刷写或启用准入。

工具 session 过期时，同一管理员重新登录和授权后仍使用原 journal。更换管理员、镜像、P4/C6 或已改变的准入状态会停止续作。
不要用新 journal 绕过既有登记冲突。已有身份保留、授权补发或损坏修复属于 #42；新工具不会自动调用低层 `--repair`。

journal 目录必须 0700，文件必须 0600；拒绝符号链接、重复 JSON 字段和并发使用同一 journal。
`backup/` 包含原 NVS，`journal.json` 包含独立设备凭据，`transport.log` 含硬件维护记录，均不得进入 Git 或共享普通日志。
恢复资料保留在现场私有目录；全流程不会要求擦除整个 Flash、重建分区或将备份 NVS 移植到另一台设备。

## 验证边界

主机测试覆盖两设备独立身份、网络和 USB 中断、写入回执重放、错误 P4/C6、缺失真实设备验证、
仅写当前应用槽、独立回读失败和受保护区域变动。命令见 [固件验证](device-binding-v1.md#产品构建与本机验证)。
这些测试使用硬件和 HTTP 边界替身；不能替代真实 Ubuntu 数据库/HTTP、串口写入、第二台音箱及手机确认。
必须完成 [合并前真机验收](device-binding-v1.md#合并前真机验收)，再由管理员决定是否合并 PR #26。
