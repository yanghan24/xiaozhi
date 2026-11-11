# 设备身份保留与授权补发

关联 [#42](https://github.com/yanghan24/xiaozhi/issues/42)，服务端实现见 [Draft PR #63](https://github.com/yanghan24/xiaozhi/pull/63)，固件增量见[固定基点及补丁](../../integrations/firmware/README.md)。本轮[真实 B 验证](../verification/2026-11-11-device-identity-retention.md)已覆盖应用更新、独立 USB 复位、重新配网和管理员补发写回；旧凭据独立拒绝、后台状态比对及其余恢复场景仍待验收。

## 维护操作与存储边界

实际 P4 为 Waveshare WiFi6 Touch LCD 4.3、revision 3.2，物理 Flash 为 32 MB，继续使用原 `partitions/v2/16m.csv` 的 16 MB 布局。身份位于默认 NVS 分区 `0x9000..0xd000` 的 `device_ctrl` 命名空间，没有启用独立身份分区，也没有迁移分区。

| 操作 | 实现及验证范围 |
| --- | --- |
| 应用更新 | 只写已核对的当前应用槽，保留 NVS、bootloader、分区表、otadata、assets。B 已通过全量应用读回及写入前后 NVS 字节一致性核对 |
| OTA | 既有实现使用 `esp_ota_get_next_update_partition` 和 `esp_ota_begin/write/end` 写应用槽；真实 OTA 尚未验证，当前公网发现接口不分发固件 |
| 重新配网 | 通过既有 Wi-Fi 配网流程修改网络设置。B 已重新配网，机器报告保持原 P4/C6、稳定设备 ID 和新 ELF，用户确认显示及原账号正常 |
| 正常恢复出厂 | 通用 `SystemReset` 调用按命名空间清理的 `ResetUserSettings`，跳过 `device_ctrl`；当前 P4 的实际恢复入口及真机结果待验证 |
| 普通 NVS 初始化失败 | 主程序停止启动，不自动整分区擦除。产品和维护镜像均在最终链接包装两个 NVS 整分区擦除 API，依赖组件同样受保护 |
| 全片擦除或严重损坏 | 失去身份时拒绝产品接入。布局、otadata、可用 NVS 或 anchor 不可读时，当前工具会停止，另需受控修复；没有交付自动重建布局或整分区修复工序 |

普通 `write` 仅允许初次写入或完全相同身份的重试，不能替换健康凭据或可信地址。`repair` 要求损坏身份及幸存、匹配的 device_id anchor。新增 `recover` 允许在原设备标识上写入后台已授权的凭据，支持损坏 blob 且 anchor 缺失；完整但冲突的 anchor 仍拒绝，不可读 anchor 不自动修复。

整分区擦除保护包装 `nvs_flash_erase` 与 `nvs_flash_erase_partition`，返回 `ESP_ERR_INVALID_STATE`。它不阻止外部全片擦除、原始 Flash 写入或其他固件，也不将损坏 NVS 自动变为可用分区。

## 管理员补发与续作

先部署兼容 PR #63 的服务，再由运营管理员通过正常受保护入口登录。普通烧录工具凭据、客户 OIDC 身份及匿名请求不能补发。当前服务已按 [PR #68 记录](../verification/2026-11-11-pr63-deployment.md)部署，B 已完成一轮真实补发；当前有效身份和续作材料位于该轮完成后的私有 journal，不能继续以首次交付操作号发起下一轮补发。

固件 `scripts/device_recovery.py` 要求保留已完成交付或上次已完成补发的私有 journal，以及配套产品/维护构建：

```bash
python scripts/device_recovery.py \
  --origin https://bridge.hncig.cn:18443 \
  --admin-token-file .private/operator-login.json \
  --previous-journal .private/delivery/device-b \
  --journal .private/recovery/device-b-round-1 \
  --port /dev/ttyACM0 \
  --product-build build/product --maintenance-build build/maintenance
```

每轮使用新的恢复目录。工具先持久化补发号和写入号，经管理员接口预检、P4 芯片核对及应用/NVS 备份，再请求原档案补发。原设备标识、P4/C6 映射、板型、可信 origin 和前驱操作必须一致。维护镜像须声明 `credential_recovery` 能力，否则工具不发送新秘密。维护固件是离线程序，管理员授权由联网工具和后台校验，不能把 UART 接入当作管理员身份验证。

服务端签发时立即使旧凭据失效，随后工具只更新应用槽并向维护镜像写入新身份，核对提交回执，再恢复产品镜像。设备自行联网验证当前凭据及硬件后，后台才记录写回完成。补发不启用本台、不改变客户归属，也不清除本台退出状态。

响应或 USB 回执丢失时，以同一命令、同一恢复目录续作。每次续作在硬件操作前确认后台当前恢复记录；本轮已被新补发取代时，409 阻止继续。已完成 journal 的复查不重写身份。Flash 写入与后台凭据代际不是原子事务，竞争中写入的过期材料仍被后台拒绝，须按当前恢复记录再次写回。

同一恢复目录固定产品和维护构建。完成后如经另一维护任务更新了产品镜像，旧记录的重跑会因实际 ELF 不同而拒绝继续；保留原完成记录，用新应用更新任务的镜像证据核对当前安装，不改写旧 journal 或重新补发来绕过校验。实机串口在同一时间由一个工具操作。

前驱 journal 丢失时，当前工具没有按硬件找回档案的入口；应先恢复受控操作元数据，不能新登记、删档或恢复旧秘密来绕过补发。工具还要求原布局、有效 otadata、支持的应用槽及可初始化 NVS；裸片、整分区损坏和不可读 anchor 的完整恢复工序仍待交付。

## 秘密保护与回退

实际构建没有启用 Secure Boot、Flash 加密或 NVS 加密，设备秘密可从未加密 Flash 提取。命名空间、擦除保护和服务端 hash 不构成 eFuse 或抗物理提取保障。

应用回退使用该设备已核对的应用备份；后台补发不随应用回退撤销。凭据已轮换后，旧 NVS 备份不能恢复后台有效性。保留恢复表、当前凭据 hash、加密密钥和私有 journal；暂停补发时保留已经交付设备所需接口。服务端回归及协议检查仍按项目规则在 Ubuntu 执行。
