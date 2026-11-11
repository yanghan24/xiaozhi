# 设备身份保留与授权补发

关联 [#42](https://github.com/yanghan24/xiaozhi/issues/42)，服务端协议见 [PR #63](https://github.com/yanghan24/xiaozhi/pull/63)。本候选从实际 P4 产品固件 `00896abbb266bf5d2dda90a49b71ea7bb95c1a6f` 建立；现场固件仓库 origin 是本地镜像，不向第三方上游推送任务分支。

## 实际存储与维护影响

已核对的 P4 为 Waveshare 4.3、revision 3.2，32 MB 物理 Flash，使用 `partitions/v2/16m.csv`。`device_ctrl` 命名空间位于默认 `nvs` 分区 `0x9000..0xd000`，不是独立身份分区。本候选不修改分区表、bootloader、otadata、assets 或 C6 固件，不需要分区迁移。

| 操作 | 实现行为与边界 |
| --- | --- |
| 应用槽重刷 | 交付适配器只写已确认的当前应用槽，读回核对应用、启动保护区及 assets；NVS 保持原值 |
| OTA | 原 `Ota::Upgrade` 使用 `esp_ota_get_next_update_partition`、`esp_ota_begin/write/end`，仅写应用槽；仍需真实 OTA 验收 |
| 重新配网 | 现有 WifiBoard 进入配网流程，不调用身份写入；仍需在当前 P4 实测 |
| 正常恢复出厂 | 产品 `SystemReset` 使用 `ResetUserSettings`，按命名空间清理，跳过 `device_ctrl`，不退回旧固件；当前 P4 界面的具体恢复入口仍需现场确认 |
| 普通 NVS 初始化失败/修复 | 主程序已有停止启动保护；本候选另外在产品和维护镜像最终链接时包装 `nvs_flash_erase` 与 `nvs_flash_erase_partition`，依赖组件的整分区自动擦除也返回 `ESP_ERR_INVALID_STATE`。损坏时停止接入并进入受控恢复，不自动修复整个分区 |
| 全片擦除或身份损坏 | 身份不可用时关闭产品接入；核对原硬件档案后由管理员补发。整片擦除、原始 Flash 写入和运行其他固件不受上述 NVS 包装保护 |

普通 `write` 只允许初次写入或同一身份重试，不能替换健康记录的凭据或可信地址。`repair` 仍只接受损坏记录及幸存的匹配 device_id anchor。新增 `recover` 允许在原 device_id 上写入管理员刚签发的完整身份；健康记录轮换和异常丢失分别处理，冲突 anchor 始终拒绝。

维护镜像是离线程序，不验证管理员登录或远程证明硬件。授权由联网工具在发送新秘密前通过服务端恢复接口检查；维护工作站和串口连接必须受控。新工具要求维护镜像声明 `credential_recovery` 能力，避免向旧镜像发送补发秘密。

## 管理员补发工具

前提：服务端已部署兼容 PR #63 的版本，管理员通过正常受保护入口登录；保留该设备已完成交付或上次已完成补发的私有 journal。普通烧录工具凭据不授予补发权限。

```bash
python scripts/device_recovery.py \
  --origin https://bridge.hncig.cn:18443 \
  --admin-token-file .private/operator-login.json \
  --previous-journal .private/delivery/device-a \
  --journal .private/recovery/device-a-round-1 \
  --port /dev/ttyACM0 \
  --product-build build/product --maintenance-build build/maintenance
```

每轮使用新的恢复目录，并固定保留前驱 journal 与两份构建。工具先持久保存补发号和写入号，再经管理员路由预检、真实 P4 芯片核对和应用/NVS 备份，最后请求原档案补发。返回的原设备标识、P4/C6 映射、板型和可信 origin 必须匹配。

新凭据在服务端创建补发时立即生效，设备写回前可能离线。工具只写应用槽，将补发身份提交到维护镜像并要求匹配的提交/读回回执，再恢复产品镜像。设备必须自己联网验证新凭据、原 P4/C6 与稳定标识，后台才记录写回完成。补发完成不启用设备、不改变客户归属或本台退出状态。

响应丢失、USB 回执丢失或完成响应丢失时，重跑同一条命令，复用同一恢复目录。每次续作先检查后台当前操作，再接触硬件；新一轮补发已取代本轮时，409 会阻止本轮继续写入。已完成 journal 的复查不会再写身份。服务端凭据代际与物理 Flash 写入不是一个原子事务；竞争中写入了已过期材料的设备会被后台拒绝，须使用当前恢复操作重新写回，不能恢复旧后台秘密。

若前驱 journal 也丢失，当前工具不提供按硬件找回档案的入口，应先通过受控运维途径恢复当前档案/操作元数据；不得通过新登记、删档或直接恢复旧 NVS 秘密绕过补发。

## 验证与保护能力

本机自动化测试只覆盖公开身份维护接口以及 HTTP、串口、esptool 的替身边界，不替代 Ubuntu 与真实 P4 验收。真实测试须分别记录应用更新、OTA、重新配网、普通恢复、异常丢失、补发写回、旧凭据拒绝和重启；后端回归与设备协议检查按主项目要求在 Ubuntu 执行。

实际产品构建配置未开启 Secure Boot、Flash 加密或 NVS 加密。成功的现场适配器核对也要求设备未开启 Secure Boot/Flash 加密。身份是默认 NVS 中的秘密，独立命名空间、访问权限、擦除防护和后台 hash 都不等于设备秘密不可提取；没有验证 eFuse 秘密保护或抗物理攻击能力。

回退应用不撤销后台补发。原始应用/NVS 备份只供受控恢复；凭据轮换后，不得把备份中的旧身份当作有效身份恢复。暂停补发时保留服务端恢复表、当前 hash 与加密密钥，并保留兼容新凭据的 API。
