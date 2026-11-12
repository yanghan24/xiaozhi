# #42 P4 受控 OTA 的服务器交接

用户明确服务器恢复后的服务器操作由服务器端完成，硬件侧继续负责 B 的备份、串口、应用槽和身份保持核对。关联 [PR #77](https://github.com/yanghan24/xiaozhi/pull/77)、[候选报告](../verification/2026-11-12-p4-maintenance-preparation.md)和 [PR #74](https://github.com/yanghan24/xiaozhi/pull/74)。此交接没有执行 OTA 下发或恢复出厂。

## 当前 B 与候选

2026-11-12 10:35（Asia/Shanghai），硬件侧已重新读取当前完整应用槽和前 `0x20000` 字节，并完成复位启动核对。B 仍是 `860e045`、活动槽 `ota_0`，原稳定身份/P4/C6、补发后凭据和整个 `device_ctrl` 与前一快照一致，设备报告已联网。用户随后确认当前二维码扫码和完整回答均正常。原始 Flash/UART 和标识留在私有目录，公开[证据仅包含布尔比对](../verification/evidence/2026-11-12-p4-maintenance-preparation/b-after-server-restored.json)。

| 项目 | 固定值 |
| --- | --- |
| 候选源码 | `a30f6a8d3e47068004af0685d5c229cb7648cdae` |
| 板型 | Waveshare P4 WiFi6 Touch LCD 4.3，P4 rev 3.2，32 MB 物理 Flash，保留原 16 MB 布局 |
| 当前 ELF | `a04fda6db5e0feb4ea5c81d174fd44ecb606ae6a6cd3af6c1d40d4ee67683419` |
| 目标 ELF | `364a1d6f2d33a9fa0d1312956aa22daae0076c758a8d5ff4e433b0e7d52022a3` |
| 产品 bin SHA-256 | `b34b7ccf64ca50d2ae6052e8bdd24a5abfc6bacb50d7fca03b1c8495a36a84b7` |
| 产品 bin 大小 | 4,003,072 字节 |
| 应用版本 | `2.2.6`；没有增加自动发现的版本号 |

候选新增设置页恢复出厂确认入口，保留已安装的身份保护与本机解绑能力。当前 B 还没有该入口，须先真正安装候选，之后才能验证界面取消、确认恢复和重新配网。原恢复 journal 固定的 `f03d98d` 保留，不为重跑它刷回旧应用或还原旧 NVS。

## 产品传输包

硬件电脑上的文件为：

```text
/home/bimcenter/Projects/xiaozhi/.runtime/issue42-local/p4-maintenance-candidate/issue42-p4-a30f6a8-ota.tar.gz
```

归档大小 2,253,945 字节，SHA-256：

```text
b1b5509c0727e0c25c8346e517904ed0e3d45c674bf4657f8ab8811468a30760
```

内容仅为 `product.bin`、`candidate.json`、`README.txt`、`SHA256SUMS`。没有 sdkconfig、设备标识、凭据、交付 journal、Flash/NVS、UART 或管理员登录。`candidate.json` 是产物元数据，下载地址为 null，不是可直接部署的发现响应。归档及其中每个文件已解包读回核对，尚未传到 Ubuntu；由用户通过既有受控文件通道交付服务器端。

服务器收到归档后，在独立私有目录解包，再执行：

```bash
sha256sum issue42-p4-a30f6a8-ota.tar.gz
install -d -m 700 p4-a30f6a8
tar -xzf issue42-p4-a30f6a8-ota.tar.gz -C p4-a30f6a8
cd p4-a30f6a8
sha256sum -c SHA256SUMS
```

使用未占用的 `p4-a30f6a8` 目录，核对上表固定大小与产品 SHA 后再准备分发。二进制放在受控产物存储，不加入 Git。

## 服务器端先准备

1. 按[当前部署入口](../deployment/current.md)核对实际 API/语音版本、持久配置和公网入口；保留 PR #63/67/74 的现有状态，不重新轮换凭据、解绑或换绑来准备 OTA。使用 PR #74 已有的受控档案/硬件映射定位 B，不根据显示昵称猜测目标。
2. 就近保存 B 的归属账号、`ownership_version`、准入状态、凭据 hash、恢复操作代际/状态、本地调用许可、许可版本及停用状态；保存 A 的相同隔离对照。真实标识和原始快照仅在服务器私有目录，公开材料只报告前后是否相同。此次服务器快照须明确对应当前换绑后的状态，不能补证历史第一次补发瞬间的归属保持。
3. 将固定产品 bin 放在 B 可达、正常 TLS 验证的受控 HTTPS 地址，返回准确的 `Content-Length: 4003072`。从 Ubuntu 核对实际下载 SHA 与上表相同，记录最终 URL、访问范围和停用方法；保留旧版本产物和现有服务。暂不下发，先回传「快照和下载已就绪」及非秘密下载信息，硬件侧准备串口观察。
4. 检查实际 B 语音会话是否支持受控用户工具下发。设备已有 `self.upgrade_firmware(url)`；默认工具列表隐藏用户专用工具，受控调用者需以 `tools/list` 的 `params.withUserTools=true` 查询，使用该会话自己的递增请求 ID。当前源码未证明存在可直接使用的管理员 HTTP 下发入口，不编造此类地址。不要将升级工具暴露给普通模型自动选择。

## 下发与结果判定

首选通过实际 B 的有效会话，由受控调用者显式执行 `self.upgrade_firmware` 并传入已核对的 URL。对应设备 JSON-RPC 载荷的 `params` 为：

```json
{
  "name": "self.upgrade_firmware",
  "arguments": {"url": "<服务器端已核对的 HTTPS 产品 bin 地址>"}
}
```

请求必须属于当前 B、当前许可和当前语音连接；设备会复核这些代际。工具返回 true 只表示已排入主任务，不能记为 OTA 成功。成功由真实下载/写入/重启日志，以及硬件侧读回目标 ELF 和 `ota_0 → ota_1`、原身份与凭据保持共同判定。

若实际服务没有受控会话下发入口，先报告缺口。备选的定向发现必须由服务器端单独审查，只对既有 B 硬件映射及当前旧 ELF 提供候选，并在目标 ELF 出现后停止提供；保留原发现响应中的设备控制等字段，不做全局强制分发。当前固件解析的 `firmware.force` 是数字 `1`，而不是 JSON 布尔值。此备选尚未实现或部署，也未授权扩大到 A 或所有设备。

服务器端和硬件侧均就绪后再协调一次下发。OTA 后服务器侧再次采集即时状态并比对；硬件侧采集当前应用/NVS、目标 ELF、活动槽与受保护身份，然后由现场确认扫码和完整语音。随后在候选设置页验证恢复出厂；执行前再次保存服务器状态，恢复/配网后按相同字段比对。后台停用保持和实际停播另列验收，不能仅凭本机 UI 或 HTTP 状态宣称通过。

本轮健康 B 不需要再次补发。异常丢失、各边界中断续作及新轮次补发的即时归属/许可证据仍按 #42 原清单安排，不能将本次扫码恢复或资料准备记为这些项目通过。
