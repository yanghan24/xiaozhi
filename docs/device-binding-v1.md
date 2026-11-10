# 设备绑定与控制 v1 固件接入

本实现对应 [#13](https://github.com/yanghan24/xiaozhi/issues/13)，配套服务端为
[PR #26](https://github.com/yanghan24/xiaozhi/pull/26) 的 `885918d`。
[协议](https://github.com/yanghan24/xiaozhi/blob/885918d24d69f0b2ed87114f1937292727111d4e/docs/contracts/device-binding-v1.md)
及该 PR 的 Schema 是接口依据。固件控制切片基于 `16f6fd5`，同时保留已在设备恢复的
`f639dd3` P4 定制功能：时钟、天气、日历、公司 Logo、音乐、录音和 IoT 遥测。
上游固件基础仍为 `9bb2785`。本地构建与主机测试通过后，还须完成手机和音箱验收，才能合并服务端 PR。

## 产品行为

`CONFIG_XIAOZHI_DEVICE_CONTROL=y` 启用产品模式；通用上游模式默认关闭。产品模式同时开启
LVGL 二维码与 CA 证书包，适用微雪 ESP32-P4 WiFi6 Touch LCD 4.3。

- 受信任 HTTPS OTA 请求声明 `device_control_capabilities: [1]`，发现控制服务。无有效配置或发现失败时保持语音关闭。
- 未绑定显示动态绑定码；退出、需要重新授权或 `market_auth_pending` 显示原账号恢复码。允许使用时显示原 P4 界面，触摸“账号”打开本台账号二维码。
- 待机时“账号”位于右下角，对话时位于右上角，避开网络/电量和“结束对话”。账号页可“返回”；仅收起本机页面，手机已扫描的流程仍按服务端期限处理，不改变许可。
- 手机完成 OIDC 登录并明确确认绑定，音箱无需再次确认。P4 对话页“结束对话”仅结束当前对话；设备账号退出从手机账号页操作。
- 单一后台任务串行同步状态、申请二维码、发送 ACK。整体请求截止 25 秒；网络故障不续租，独立单调计时器在许可到期时关闭语音。
- 许可关闭时立即拒绝新收发及播放，停止音乐和录音，清除录音、编解码、生成、播放队列及待显示字幕。旧许可代次、旧连接回调不能恢复对话。
- 停播 ACK 等待已进入驱动的受控 PCM 写入返回，并关闭驱动输出。二维码引起的 revision 冲突会重新同步并重试同一许可的 ACK；新许可必须重新实际应用，不能沿用旧停播证据。
- 正常退出不销毁独立设备凭据。恢复只使用服务端返回的当前许可和设备语音令牌；启动按键、唤醒、旧 NVS token、数字激活和 MQTT 回落均不能绕过许可。
- 本地配网和错误提示音保留。产品模式只通知唤醒词，不上传跨许可保存的历史唤醒 PCM。原有字幕仍在对应音频实际进入输出时显示。

## 独立设备身份

先由小智管理后台预登记设备并签发逐台独立、可撤销的接入凭据，再通过离线 USB 维护镜像写入。
固件不生成匿名替代凭据。集市 Secret、Token、Key 不进入设备。

私有交付文件使用以下完整结构；下例全部是测试值，不能直接用于真实设备：

```json
{
  "version": 1,
  "hardware_id": "020000000001",
  "device_id": "speaker-a",
  "credential": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
  "ota_url": "https://speaker.example:18443/xiaozhi/ota/",
  "control_origin": ""
}
```

| 字段 | 约束 |
| --- | --- |
| `hardware_id` | 主芯片 eFuse 默认 MAC 的 12 位小写十六进制，用于写入前核对实物；不能代替认证 |
| `device_id` | 后台预登记的稳定标识，1–128 位 `[A-Za-z0-9_.:-]`；写入后不可被工具更换 |
| `credential` | 后台签发的 43–128 位 URL-safe 接入秘密 `[A-Za-z0-9_-]` |
| `ota_url` | 已授权的完整 HTTPS 地址，路径固定为 `/xiaozhi/ota/`，保留实际公网端口 |
| `control_origin` | 可选的 HTTPS 控制服务 origin，留空则必须与 OTA 同 origin；无路径、query 或 fragment |

不接受缺失、重复、额外字段及嵌入 NUL。配置原始 JSON 不超过 2048 字节。域名/IPv4 支持与
控制传输一致，IPv6 保持关闭。文件放在 Git 忽略的 `.private/` 下，POSIX 权限为 `600`。
普通日志、二维码和工具输出不回显秘密。

产品模式从此身份读取 `ota_url`，不再使用历史 `wifi/ota_url` 或编译默认值。OTA origin
由完整 URL 导出，跨 origin 控制服务必须在 `control_origin` 中授权；合法 TLS 证书不能自行授权新目标。
控制请求禁止跟随重定向。

NVS `device_ctrl/identity_v1` 将整份配置作为单个 blob 提交并读回，凭据轮换只替换这份完整记录。
首次写入先持久化不可变的 `device_ctrl/device_id` 锚点：即使此后断电，也只能为原设备重试写入。
兼容早期四键开发格式中的稳定 `device_id` 约束，但产品运行必须重新交付完整新格式；旧语音 token 不迁移。
NVS 持久化不等于已启用 Flash Encryption 或 Secure Boot。

## 离线 USB 交付与恢复

维护镜像是独立的最小 ESP-IDF 项目 `tools/device-provisioning/`，只包含 USB、NVS 和配置校验，
不启动网络、UI 或音频。产品镜像不包含维护入口，维护镜像不进入 OTA 发布。

在已经加载 ESP-IDF 5.5.5 环境的固件根目录构建：

```bash
firmware_dir="$PWD"
cd tools/device-provisioning
idf.py -B "$firmware_dir/build/bench" \
  -DSDKCONFIG="$firmware_dir/build/bench.sdkconfig" \
  -DIDF_TARGET=esp32p4 build
cd "$firmware_dir"
```

物理维护前记录当前产品镜像、校验值、实物芯片 revision、启动槽和分区表。沿用既有 16 MB
布局，不擦除整个 Flash/NVS，不修改 assets 分区。维护镜像实际启动并输出
`mode: device_provisioning` 后，先验证私有文件，再显式写入：

```bash
chmod 600 .private/speaker.json
python scripts/provision_device.py --config .private/speaker.json
python scripts/provision_device.py --config .private/speaker.json --write --port /dev/ttyACM0
```

端口仅为示例。工具在收到维护镜像的硬件标识并核对相符之后才发送凭据；每次写入带随机
`operation_id`，只接受匹配本次操作且已提交、读回成功的 `stored` 回执。连接中断或超时不算成功；
重新进入维护模式后可重试相同配置。交付完成必须恢复产品镜像到已核对的启动槽并重启。

普通写入可为同一 `device_id` 补发/轮换有效凭据。记录损坏时普通写入会拒绝；只有身份锚点完好，
才能从该设备私有备份显式修复：

```bash
python scripts/provision_device.py --config .private/speaker.json \
  --write --repair --port /dev/ttyACM0
```

`--repair` 只接受损坏记录，不能覆盖健康记录或更改设备 ID。若 NVS 本身无法初始化，或者锚点也已丢失/损坏，
工具停止并交由维护人员核对预登记档案与备份，不能通过自动擦除或重新随机生成身份恢复运行。

产品模式下，正常恢复出厂逐个擦除用户命名空间，始终保留 `device_ctrl`，不使用“先擦整个 NVS 再恢复”的断电窗口。
该模式也不通过擦除 `otadata` 选择历史旧镜像；重启继续运行已安装的产品镜像。
NVS 损坏/版本不兼容时产品启动停止，不会自动清空设备身份。

## 产品构建与本机验证

使用 ESP-IDF 5.5.5、RISC-V GCC 14.2。已有 `sdkconfig` 必须显式启用产品模式；defaults 不覆盖已有配置。
私有天气/IoT 参数继续留在本机忽略的 sdkconfig 中。新构建目录可使用：

```bash
idf.py -B build/product -DIDF_TARGET=esp32p4 \
  -DSDKCONFIG=build/product.sdkconfig \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.device-binding.defaults' \
  build
python scripts/verify_product_build.py build/product
```

验收音箱先前已记录为 P4 revision 3.2、实际 Flash 32 MB；本版本保持已用的 16 MB 布局及 3.1+ 目标，
不做分区迁移。产品模式对 `main` 应用组件使用 `-Os`，音频 DSP/编解码依赖保留原优化设置。
`verify_product_build.py` 检查实际分区二进制、镜像容量、板型/产品配置、原界面和控制入口的
编译及链接结果，并确认产品未带入 USB 维护入口。

主机测试：

```bash
cmake -S tests/device_control -B build/host-control -DIDF_PATH=/path/to/esp-idf
cmake --build build/host-control
ctest --test-dir build/host-control --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests/device_control -p 'test_*.py' -v
```

覆盖协议/时间边界、许可代次、停止屏障、二维码 revision 的 ACK 重试、原账号恢复、身份写入/轮换/
损坏修复、提交失败、恢复出厂保留身份，以及 USB 目标核对和回执关联。NVS 主机替身不能证明实际断电恢复，
驱动屏障测试不能证明真实音箱已静音。

## 合并前真机验收

在 Ubuntu 部署 PR #26 配套版本，由可直连服务器的执行者完成服务验证；再使用已预登记并写入独立凭据的
手机与音箱执行以下验收，记录服务端提交、固件提交、镜像 SHA-256 和设备日志。

1. 重启保留时钟、天气、日历、Logo 与原有功能；未绑定展示可扫码二维码，超时隐藏旧码并更新。账号入口不遮挡电量/网络和结束对话，可进入账号页再返回待机。
2. 手机登录后确认首次绑定，音箱收到新许可才可唤醒、上传及播放；不能通过数字激活、旧 token 或重启绕过。
3. 对话播放中从手机退出目标设备，核对驱动静音、旧音频/字幕丢弃及匹配许可版本的 `audio_stopped: true` ACK。
4. ACK 遇到二维码创建/过期导致的 revision 冲突或响应丢失后仍能重试；手机不能提前显示已确认停播。
5. 原账号扫码只恢复目标设备，同账号其他音箱状态独立；换账号不能覆盖原归属。
6. 授权待确认/需要重新授权均关闭许可并显示原账号恢复入口；普通资源失败、无余额不能被当成解绑。
7. 断网不续租，到期停止；重连先同步，旧连接/旧代次不再播放。重启不从持久化数据恢复旧租约。
8. 正常恢复出厂、同 ID 凭据补发、维护写入中断及恢复产品镜像后仍保持同一设备身份和完整 P4 界面。

本机验证记录见 [2026-11-10 构建记录](verification/2026-11-10-product-firmware.md)。以上真实手机、
音箱、断电及 Ubuntu 配套部署未由本次本机工作代替；全部验收通过后再合并。
