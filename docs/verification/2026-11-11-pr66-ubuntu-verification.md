# PR #66 Ubuntu 补发后验证

2026-11-11 19:26–19:30（Asia/Hong_Kong），执行主机 `jt@192.168.168.114`。接续 [PR #66](https://github.com/yanghan24/xiaozhi/pull/66) 固定提交 `1d029f524377de926d363fb8400f1399cf11a9e2` 的 B 设备补发写回证据，关联 #42 / #13。本轮补充服务器状态及旧、新凭据独立 HTTP 检查，不代表全部恢复场景验收完成。

## 环境与前置证据

现场 API 为 `xiaozhi-product-api:1b50709`，源码 `1b50709fd1cadfad966b12d4afedf78b1f279aa6`，是 PR #63 与 PR #67 的组合版本，见[部署记录](2026-11-11-pr67-deployment.md)。语音为 `xiaozhi-product-voice:prompt-7f10cbd`。本轮未部署、重启服务或写入固件。

PR #66 的[固定现场报告](https://github.com/yanghan24/xiaozhi/blob/1d029f524377de926d363fb8400f1399cf11a9e2/docs/verification/2026-11-11-device-identity-retention.md)记录 B 已物理写回、由设备验证并完成补发，当前固件为 `860e045`；本轮没有通过串口重新核对固件。服务器在发送验证请求前确认补发记录为 `delivered`，设备验证与完成时间齐全。因此这些 HTTP 请求不是代替设备完成首次写回验证。

## 本轮结果

| 检查 | 结果 |
| --- | --- |
| 原交付与当前补发 | 唯一补发记录已完成，前驱对应原交付操作，关联设备只有一条交付硬件映射 |
| 当前凭据 hash | 与补发记录一致，与首次交付记录不同 |
| 旧凭据独立请求 | HTTP 401，v1 `INVALID_DEVICE_CREDENTIAL` |
| 新凭据独立请求 | HTTP 200，v1 `OK`，`verified=true`、`status=delivered`，设备标识匹配 |
| 两个凭据响应 | JSON 类型及 `Cache-Control: no-store` 均通过 |
| 当前设备状态 | `admitted=true`、`local_enabled=true`、`reason=ready`，存在归属账号，`permission_version=14`、`ownership_version=3` |
| 公网合成凭据预检 | 24/24 通过，含 OTA、控制鉴权、管理/内部接口隔离，以及 WSS 匿名/无效凭据 1008 拒绝 |

数据库查询使用只读事务。用户明确授权直接使用 Ubuntu 管理权限取得材料后，在服务器进程内存中使用原密钥解密已存的交付及补发密文，按各自操作号、设备与管理员上下文进行认证解密，并核对 SHA-256。没有新建补发或轮换凭据，没有将秘密输出或复制到本机。新凭据请求发生于已有 `device_verified_at_ms` 和 `completed_at_ms` 之后，不更新首次验证时间。

HTTP 客户端在 Ubuntu，通过临时 SSH loopback HTTP CONNECT 代理访问 `https://bridge.hncig.cn:18443`，保留 TLS 证书验证并拒绝重定向。代理检查结束后关闭；不宣称 Ubuntu 直连公网已经恢复。

## 证据与未完成项

- [数据库状态摘要](evidence/2026-11-11-pr66-ubuntu-verification/state-summary.json)：19:27 的原始脱敏观察，内含当时“等待材料”的阶段状态；该项随后由下列 19:30 凭据证据完成，保留原文件不改写历史。
- [旧、新凭据检查摘要](evidence/2026-11-11-pr66-ubuntu-verification/credential-summary.json)：两个独立请求均通过。
- [公网预检](evidence/2026-11-11-pr66-ubuntu-verification/public-preflight.json)：24 项检查均通过。

服务器私有只读快照位于 `/home/jt/pr66-verification/private/before.tsv`，包含敏感设备状态，不入库。公开证据只保留比对结果、状态、版本和时间，不含设备秘密、管理员登录、真实账号标识或密文。

B 在补发后另行完成了解绑和换账号绑定，见[当前部署记录中的现场结果](../deployment/current.md)。当前归属版本 3 和许可版本 14 不能证明补发瞬间归属或本地调用许可不变；缺少紧邻补发的前后快照，历史归属保持仍不记为通过。单条映射证明当前原档案关联，不扩大为完整的历史硬件前后比对。

备份及 API 重启执行请求被自动审批拒绝，返回仅为 `blocked by policy`；该次请求未执行，本轮未完成补发后服务重启验收。真实 OTA、正常恢复出厂、异常身份丢失后的完整补发及迟到/续作竞态仍按 PR #66 的待验收范围处理。本报告不关闭 #42 / #13，也不表示 PR #63 / #66 已合并。

## 19:54 补充：本机可完成的检查

用户要求暂缓重启问题，硬件留给硬件方。本轮继续在 Ubuntu 经相同 TLS 公网入口完成 [8 项拒绝检查](evidence/2026-11-11-pr66-ubuntu-verification/rejection-summary.json)：旧凭据访问 state、account、requests、state-acks 均为 401 / INVALID_DEVICE_CREDENTIAL；当前设备凭据创建、查询及完成 recovery 均为 401 / OPERATOR_AUTH_REQUIRED；新凭据携带互换的 P4/C6 标识为 403 / HARDWARE_MISMATCH。全部通过。

这些请求前后，在内存中比对全部 dc_device 的凭据 hash、准入、归属、本地许可与许可/归属版本，以及全部恢复记录，结果完全一致。没有创建补发、改变归属或重启服务。该比对证明本轮拒绝请求无这些持久副作用，不替代历史补发前后保持性证据。

同时重新读取现网组合提交 `1b50709` 之前已执行的隔离测试 XML，[摘录](evidence/2026-11-11-pr66-ubuntu-verification/existing-regression.json)确认 DeviceDeliveryHttpTest 20 项与 CustomerVoicePermissionHttpTest 24 项均无失败/错误/跳过。已有恢复用例覆盖原档案保持、旧交付重放拒绝、已退出设备回执续作、并发补发唯一后继、迟到恢复不能还原旧秘密、跨设备前驱拒绝、原归属/第二设备保持及旧语音/迟到扫码拒绝。这是既有服务端自动化证据复核，本轮未重跑，不能记为 P4 实际写回竞态已通过。

本机本轮累计完成 24 项公网预检、2 项新旧凭据检查、8 项拒绝检查。下一步实机验收见[硬件方检查清单](../handoff/2026-11-11-pr66-hardware-acceptance.md)；API 重启验收单独暂缓。