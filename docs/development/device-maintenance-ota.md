# 设备在线与待机 OTA 协议

设备在线表示近期有本台认证的管理通信；语音会话只在对话时建立。此协议依据 [ADR-0009](../adr/0009-device-presence-and-standby-ota.md)，沿用 [设备控制 v1](device-control-v1.md) 的设备接入凭据、准入校验与数值 JSON envelope。固件来源与管理预览接口见 [OTA 推送](product-ota-push.md)。

## 在线与兼容

旧固件成功发起 `GET /device-control/v1/state` 时记录最近联系。长轮询内部读取不会刷新心跳。新固件约每 20 秒调用下述维护接口，独立保存能力报告时间；90 秒内的有效联系判为在线，能力和系统信息也须在 90 秒内。凭据轮换或撤销准入使旧记录失效。旧固件继续发送 state 不能为过期能力报告续期。

设备库存返回 `online`、`last_seen_at_ms`、`voice_status` 和 `ota_transport`。支持维护协议的在线设备使用 `management`；旧固件语音会话可用时使用 `voice`，否则为 `online_requires_wake`。管理连接与客户绑定、退出及语音许可分离。此次不为发布时离线的设备创建等待任务。

## 维护交换

`POST /device-control/v1/maintenance` 使用已有 `Device-Id` 和 `Authorization: Bearer ...`。请求为有界 JSON，字段为：

```json
{
  "boot_id": "11111111111111111111111111111111",
  "ota_capable": true,
  "idle": true,
  "system_info": {
    "flash_size": 16777216,
    "chip_info": {"model": 18, "revision": 302},
    "application": {"version": "2.2.6", "elf_sha256": "<64 lowercase hex>"},
    "board": {"type": "esp32-p4-wifi6-touch-lcd"},
    "ota": {"label": "ota_0", "state": "valid"},
    "partition_table": []
  },
  "operation": null
}
```

`partition_table` 实际必须包含完整真实布局，示例的空数组不可用于升级预检。启动标识每次启动生成，不能跨重启复用。系统报告只包含硬件、应用、OTA 槽与分区信息，不上传账号凭据、语音令牌、显示内容或录音。服务器拒绝旧启动的延迟报告恢复旧领取资格。

成功响应是 `{"version":1,"code":"OK","data":{...}}`，data 含数值 `server_time_ms`、`command`、`operation_ack`。无任务时后两项为 null。命令包含：

```json
{
  "operation_id": "22222222222222222222222222222222",
  "boot_id": "11111111111111111111111111111111",
  "url": "https://<configured-origin>/xiaozhi/ota-artifacts/<operation>/<sha>.bin?expires=...&signature=...",
  "artifact_sha256": "<64 lowercase hex>",
  "target_elf_sha256": "<64 lowercase hex>",
  "image_size": 4000000,
  "expires_at_ms": 1794560000000,
  "expires_in_ms": 600000
}
```

命令原始有效期最长 10 分钟。固件使用单调时钟，扣除请求耗时判断开始执行期限，不依赖设备墙钟；重复响应不能延长期限。任务已开始写入后，期限到达不会强行打断 Flash 写入。

## 领取、确认与重启

管理员确认后，操作持久处于 `waiting_idle`；只有原启动和原凭据仍匹配、当前硬件及原 ELF 未变化、固件仍已校验、管理员仍有权限时，空闲设备才获得命令。服务器先持久标为 `command_offered` 再返回，因此响应丢失不会错误认定从未发送。

固件在独占空闲升级门禁下，先保存操作 ID、原启动、目标 SHA/ELF/长度及 accepted 状态，再发送：

```json
"operation": {
  "operation_id": "22222222222222222222222222222222",
  "origin_boot_id": "11111111111111111111111111111111",
  "status": "accepted"
}
```

服务器回传 `operation_ack`，包含匹配的 `operation_id`、`status`、数值 `expires_at_ms` 和 `expires_in_ms`。只有 accepted 确认成功、期限有效且本机仍空闲才开始一次写入。相同命令或回执可以重取，但不得重复启动升级。服务器与旧语音路径共用每设备唯一活动操作约束。

固件流式核对下载大小、完整 SHA-256、镜像描述中的 ELF；通过镜像校验后才切换启动分区。重启后旧 accepted 任务只报告 `installed` 或 `interrupted`，绝不重写旧任务。当前运行目标 ELF、不同启动标识且 OTA 状态 VALID 才能形成 `target_running` 观察。明确执行失败报告 `failed`；确认后标记回执已送达，保留最后操作的防重记录。`ota_maint` 维护账本与设备身份一样不属于可清除的用户设置，恢复本机设置时保留。晚到回执不能覆盖终态或释放另一任务。

首次从旧固件经语音路径安装配套固件时，新管理报告没有旧操作的本机回执。新建语音预览也固定设备凭据；认证报告的新运行 ELF 与原 ELF 不同、命中目标且为 VALID，可独立确认目标运行，无需再唤醒。管理操作也可通过原凭据与不同启动的真实目标报告形成观察；缺失凭据关联的历史操作保持原有核验方式。传输结果不会因此改写。

`waiting_idle` 过期且命令从未暴露，可以标 `not_sent`；命令已经暴露却未获得可靠结果，保留 `result_unknown`。传输结果与目标运行观察分别保存，收到命令或下载结束均不代表已安装成功。

## 验证边界

HTTP 回归覆盖认证、90 秒过期、旧固件显示、预览/确认、空闲领取、双通道互斥、凭据/启动/硬件变化、丢包、过期、重启回执和停用/未绑定维护。固件测试验证协议解析、持久去重、期限与下载校验；真实构建不代替实机安装。P4 验收须记录首次安装路径、实际下载写入/自动重启、目标 ELF/VALID、身份/归属/许可保持及另一设备不受影响。
