# #42 固件候选与 B 身份保留验证

2026-11-11，Asia/Shanghai。关联 [#42](https://github.com/yanghan24/xiaozhi/issues/42) / [#13](https://github.com/yanghan24/xiaozhi/issues/13)。本轮交付固件补丁、主机测试和真实 B 的维护保留证据；#42 继续保持打开。

## 固定源码与产物

固件基点为 `00896abbb266bf5d2dda90a49b71ea7bb95c1a6f`，最终受测提交为 `f03d98d67a0262b64ca72db1f97010675310a45c`。完整工程位于现场 `42-firmware-identity-recovery` worktree，增量源码和应用条件见[补丁入口](../../integrations/firmware/README.md)。

| 产物 | SHA-256 |
| --- | --- |
| 产品应用，3,998,384 字节 | `39bee941c4babf31e2c9819509ddfd5e003c4bf8f97bc15f9b9b6a2879997767` |
| 产品 ELF | `89acbe49ad649b4732f85d4a81a6b1a7201dba6fded4998009d3534bc84dbb07` |
| 维护应用 | `fa98219626bee420de3c25674bfb32f42e4aafb2e550589c56c93195c2826bea` |

产品和维护镜像均以 ESP-IDF v5.5.5 构建成功；生成配置与原基线逐项一致，原主程序源文件无缺失，只新增擦除保护源文件。产品镜像 10 项检查通过，应用槽剩余 130,384 字节。维护镜像本轮完成构建和主机验证，未刷入真实 B。详细结果见[构建及测试摘要](evidence/2026-11-11-device-identity-retention/build-and-tests.json)。

## 本机主机验证与审查

最终提交执行 CTest 24/24、Python unittest 43/43，通过且无失败或跳过。红转绿覆盖整分区擦除保护、普通写入不能轮换健康凭据、管理员恢复编排、旧维护镜像能力拒绝，以及损坏 blob 加缺失 anchor 的恢复。规范审查无发现；需求审查的两项发现修复后复核无新发现。以上为本机固件测试，不替代 Ubuntu 服务端回归或真机补发闭环。

在固件工程中运行：

```bash
cmake -S tests/device_control -B build/host -DIDF_PATH=/home/bimcenter/esp-idf-v5.5.5
cmake --build build/host -j 4
ctest --test-dir build/host --output-on-failure
IDF_PATH=/home/bimcenter/esp-idf-v5.5.5 \
  /home/bimcenter/.espressif/python_env/idf5.5_py3.11_env/bin/python \
  -m unittest discover -s tests/device_control -p 'test_*.py'
python scripts/verify_product_build.py build/product
```

## 真实 B 维护结果

用户将连接目标由 A 改为 B，并授权访问 B 串口。本轮没有更新 A。B 的 P4/C6 与已完成交付的 device-02 私有 journal 匹配，P4 revision 3.2、物理 Flash 32 MB，保留原 16 MB 布局及 `ota_0` 启动槽。

B 原应用与归档的 `settings-00896ab/candidate.bin` 有效应用内容逐字节相同，原镜像 SHA-256 为 `753f42d89f2253a999041f41f66fc61d057a7a12f729f97afdf4547b9f9a0aa7`。现场同一源码的另一份重构建产物有不同 ELF，不能据此把 B 误判为未知版本。

| 验证 | 结果及证据边界 |
| --- | --- |
| 写入前备份 | 已保存并核对当前完整应用槽及前 `0x20000` 字节，含原 NVS；检查当前应用仍与备份一致后才写入 |
| 应用更新 | 仅写 `ota_0` 的应用区域，完整应用读回和设备 MD5 均匹配候选；写入前后 NVS 字节相同，保护启动区及 assets 检查一致 |
| 更新后启动 | 设备报告原 P4/C6、原稳定设备 ID、新候选 ELF 和 Wi-Fi 已连接 |
| 独立 USB 复位 | 没有再次写 Flash/身份；复位后上述硬件、设备 ID、ELF 和联网状态仍匹配 |
| 重新配网 | 用户完成「设置 → 更换网络」，确认显示和原账号正常；被动 UART 报告仍匹配原设备 ID、P4/C6、新 ELF，且已联网 |

机器证据：[应用更新](evidence/2026-11-11-device-identity-retention/b-application-update.json)、[独立复位](evidence/2026-11-11-device-identity-retention/b-independent-reset.json)、[重新配网](evidence/2026-11-11-device-identity-retention/b-wifi-reprovision.json)。原始身份、UART、NVS、配置和备份只保留在现场私有 `.runtime/issue42-local/`，共享证据仅含比对结果和产物标识。

## 尚未完成

PR #63 当前为 OPEN/Draft，head `7738647759689d8a345a0f563b103467ae0b3c68`；本轮未合并、未部署、未轮换真实凭据。后续仍须在 Ubuntu 与真实 P4 验证授权补发写回、旧凭据拒绝、迟到/续作竞态、原档案和客户归属/本台退出状态保留，以及恢复后的设备与服务重启。PR #63 既有服务端测试证据不能代替这些现场结果。

真实 OTA、正常恢复出厂入口及其维护保留尚未验证；全片擦除后的布局重建、整个 NVS 损坏和不可读 anchor 的受控恢复工序尚未交付。原记录异常丢失后的完整补发流程尚未实测，没有执行破坏性擦除来制造通过证据。

原分区未迁移，Secure Boot、Flash/NVS 加密未启用；不宣称秘密不可提取。应用回退不撤销后台恢复或凭据代际，详见[操作与恢复边界](../development/device-identity-recovery.md)。
