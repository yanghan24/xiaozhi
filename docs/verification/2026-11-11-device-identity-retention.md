# #42 固件候选与 B 身份保留验证

2026-11-11，Asia/Shanghai。关联 [#42](https://github.com/yanghan24/xiaozhi/issues/42) / [#13](https://github.com/yanghan24/xiaozhi/issues/13)。本轮交付固件补丁、主机测试、真实 B 的维护保留与管理员补发写回证据；#42 继续保持打开。

## 固定源码与产物

固件基点为 `00896abbb266bf5d2dda90a49b71ea7bb95c1a6f`，最终受测提交为 `f03d98d67a0262b64ca72db1f97010675310a45c`。完整工程位于现场 `42-firmware-identity-recovery` worktree，增量源码和应用条件见[补丁入口](../../integrations/firmware/README.md)。

| 产物 | SHA-256 |
| --- | --- |
| 产品应用，3,998,384 字节 | `39bee941c4babf31e2c9819509ddfd5e003c4bf8f97bc15f9b9b6a2879997767` |
| 产品 ELF | `89acbe49ad649b4732f85d4a81a6b1a7201dba6fded4998009d3534bc84dbb07` |
| 维护应用 | `fa98219626bee420de3c25674bfb32f42e4aafb2e550589c56c93195c2826bea` |

产品和维护镜像均以 ESP-IDF v5.5.5 构建成功；生成配置与原基线逐项一致，原主程序源文件无缺失，只新增擦除保护源文件。产品镜像 10 项检查通过，应用槽剩余 130,384 字节。两份镜像均已用于后述真实 B 的补发流程。[构建及测试摘要](evidence/2026-11-11-device-identity-retention/build-and-tests.json)保留首次构建时的部署前状态，后续部署与补发以新增证据为准。

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

PR #63 仍未合并，固定 head `7738647759689d8a345a0f563b103467ae0b3c68` 已由 Ubuntu 执行者部署，见 [PR #68 部署记录](2026-11-11-pr63-deployment.md)。本机已完成下述真实 B 补发写回；旧凭据独立 HTTP 拒绝、迟到/续作竞态、后台归属/许可状态比对及补发后服务重启仍待 Ubuntu 接续。用户对原账号的确认不代替后台 ownership_version 或已退出状态的核对。

真实 OTA、正常恢复出厂入口及其维护保留尚未验证；全片擦除后的布局重建、整个 NVS 损坏和不可读 anchor 的受控恢复工序尚未交付。原记录异常丢失后的完整补发流程尚未实测，没有执行破坏性擦除来制造通过证据。

原分区未迁移，Secure Boot、Flash/NVS 加密未启用；不宣称秘密不可提取。应用回退不撤销后台恢复或凭据代际，详见[操作与恢复边界](../development/device-identity-recovery.md)。

## 管理员补发与设备写回

Ubuntu 部署后，用户在现场提供正常运营管理员登录；长期烧录凭据没有被当作补发授权。工具在创建补发前核对已完成的 B 交付 journal、管理员权限、P4 芯片及完整应用/NVS 备份。2026-11-11 16:59（+08:00），后台为原档案签发新凭据，维护镜像执行 `recover` 并提交/读回，恢复上述 `f03d98d` 产品镜像；设备自身验证新凭据及原硬件映射后，后台完成记录为 `delivered`。

| 检查 | 实际结果 |
| --- | --- |
| 原档案和硬件 | 稳定设备 ID、P4/C6 对和可信 origin 与首次交付相同 |
| 凭据轮换 | 新凭据与原凭据不同；没有新登记第二档案 |
| USB 写回 | `stored` 回执匹配预先持久化的同一写入操作号 |
| 设备验证与后台完成 | 产品硬件/ELF 报告验证通过，后台设备验证及完成时间均非空，状态为 `delivered` |
| 私有续作材料 | 原交付 journal 保留；后续新一轮补发以前一轮已完成 recovery journal 为前驱 |

脱敏结果见 [b-credential-recovery.json](evidence/2026-11-11-device-identity-retention/b-credential-recovery.json)。本轮是在健康身份上授权轮换，没有人为清空 NVS 或模拟全片擦除；不把它记为异常丢失后的完整恢复验收。

## 当前镜像与补发后复位

补发完成后的重跑检查遇到两次串口打开失败；随后读到另一构建的 ELF，工具按固定产物校验拒绝继续。用户确认此前另有工具操作 B，现已停止。当前镜像确定来自现场 `41-firmware-online-delivery` worktree 的 `860e045ab7d9599ab2b0b9135a6cef6f4222069b`，包含本机退出解绑改动，已经合入 `f03d98d` 的身份保护。身份存储、维护写回和恢复工具相关源码与受测提交逐字节相同。

当前产品 SHA-256 为 `0947b2a879a5ed3e5390796f55997706b189707a8593383049957fbc3514db4f`，ELF 为 `a04fda6db5e0feb4ea5c81d174fd44ecb606ae6a6cd3af6c1d40d4ee67683419`。只读备份确认实际应用与该产物完整匹配，启动保护区及 assets 与本轮恢复基线一致，镜像 10 项检查通过。备份后的 USB 复位报告保持原设备 ID/P4/C6、当前 ELF 和联网，后台本轮恢复仍为当前 `delivered`。本次核对没有再次补发或写入 Flash/身份。

用户随后确认「原账号正常，能听到完整回答」。见[当前镜像检查](evidence/2026-11-11-device-identity-retention/b-current-image.json)和[现场确认](evidence/2026-11-11-device-identity-retention/b-post-recovery-user-confirmation.json)。以上是当前 `860e045` 的补发后运行/复位结果；原 `f03d98d` 完成记录在镜像不变条件下的正常重跑尚未获得完整实机通过结果，不把固件失配拒绝算成该路径通过。

## Ubuntu 接续

现场保留 0600 的 `ubuntu-verification-material.json`，包含 B 的旧/新凭据及原硬件对，不含管理员登录，仅在物理写回完成后导出。通过安全通道传到 Ubuntu 私有目录；不要粘贴到聊天或加入 Git。使用[独立检查脚本](../../scripts/check-recovery-writeback.py)：

```bash
python3 scripts/check-recovery-writeback.py \
  --material private/ubuntu-verification-material.json \
  --output private/b-recovery-credential-check
```

脚本要求 Ubuntu、输入 0600、输出目录 0700，保持正常 HTTPS 证书验证并拒绝重定向；可沿用 PR #68 的已验证代理通道。只在物理设备验证及后台完成之后，分别检查旧凭据 401 / `INVALID_DEVICE_CREDENTIAL`、新凭据 200 / `delivered`，原始响应留在私有目录。仅分享脱敏 `summary.json`。本机只做语法及非 Ubuntu 拒绝检查，没有执行这两项 HTTP 检查；原账号/许可状态和服务重启还需另外核对。
