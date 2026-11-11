# #11 真实撤销与受控丢弃响应

2026-11-11（香港时间），Ubuntu `jt@192.168.168.114`。沿用用户指定的同账号双机测试账号。用户要求继续完成 #11；本轮已说明真实撤销会使两台设备需要重新授权。

## 故障层级

先使用真实有效 Access Token 验证账号、应用与 Key 基线。专项适配层向真实集市仅发送一次 `DELETE /api/oidc/authorization`，保存脱敏成功见证，再主动将该响应从工具调用者处丢弃。调用者只得到 `http=null / unconfirmed`，不得据此自动重试。

这是“真实撤销 + 运维传输适配层受控丢弃响应”，不是物理网络丢包、真实服务器超时或真实集市 5xx。真实成功见证用于最终核查，不回填或篡改调用者未知结果。现网 API、数据库、代理和网络没有注入故障。

## 已观察结果

- 15:39:55.696：真实集市 HTTP 200、`code=20000`、`revoked=true`，仅一次上游调用；适配层不向工具返回该成功响应。
- 调用者结果为 `unconfirmed / automatic_retry=false`。已有持久化尝试记录阻止重复执行；核验使用拒绝传输的守卫，不实际重发 DELETE。
- 旧 Access Token UserInfo 401，旧 Key 模型列表 401。成功见证与这两项事实共同确认旧授权已撤销；没有推理调用。
- 15:40:50.251：后台自然刷新检测到失效，当前授权变为 `REAUTH_REQUIRED`；没有修改到期时间或手动刷新旧令牌。两台设备保持同一归属，A/B 均变为 `market_auth_required`，许可版本9→10。
- 初次停用快照 A revision18、B revision14；后续 A 出现恢复二维码，revision19，且收到许可10的停播ACK。未将服务端ACK当作用户现场听感。

## 恢复与验收状态

恢复通过原账号正常扫码并明确恢复目标设备，没有用数据库写入模拟恢复。用户先确认 A 已恢复、B 尚未操作，再确认 B 已恢复；服务器分别核对：

| 时刻（香港时间） | 授权与设备状态 |
| --- | --- |
| 15:45:06.033 | 新授权 ACTIVE，代际已变；A ready/许可11/revision20，B仍market_auth_required/许可10/revision14；只恢复目标设备通过 |
| 15:46:16.757 | A保持ready/许可11/revision20；B恢复ready/许可11/revision16；两台归属账号仍一致 |

两次核对均为新令牌 UserInfo 200且主体匹配、新 Key 模型列表200、旧 Key仍401。积分接口200/业务码20000，UserInfo含name/nickname/preferred_username；只保留字段存在性，不导出资料或余额。这是实际能力可用证据，不声称保存了原始token响应的完整scope列表。

本轮真实撤销、受控丢弃成功响应、未知结果不重试、自然识别失效、原账号逐台恢复闭环通过。没有声称真实集市发生5xx或网络层丢包。用户已明确采用分层验收，5xx/超时/刷新临时故障由Ubuntu可控回归验收，真实集市这些故障保留未验证。

[脱敏机器证据](evidence/2026-11-11-identity-closeout/response-loss.json)保留基线、真实成功见证、调用者未知结果与各次状态观察。本轮最终删除临时明文state及主体基线，保留无凭据的尝试、结果与证据；现网服务继续正常运行。

Ubuntu 执行脚本：`/home/jt/issue11-loss-prepare.sh`、`/home/jt/issue11-loss-execute.sh`、`/home/jt/issue11-loss-observe.sh`。私有轮次位于 `/home/jt/issue11-revocation-probe-20261111/private-response-loss-round`。公开证据只记录布尔值、状态、时间和设备别名。
