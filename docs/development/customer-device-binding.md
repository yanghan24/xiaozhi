# 手机最终确认绑定（#13）

> 2026-11-10 后续实现：恢复许可、语音闸门和直连集市模型见[产品语音说明](customer-product-voice.md)。本文保留前一切片的范围和证据。

本切片实现首次 bind 的最终确认。设备正式发码、手机 OIDC 验证后，结果页显示“确认绑定设备”；点击后才原子保存归属并显示“绑定成功”。不要求音箱端确认。只有正式 `dc_request` 关联的流程可完成绑定，#29 的身份测试预置请求仍不能作为产品绑定依据。

## 接口与事务

`POST /xiaozhi/customer/v1/binding-flows/{flow_id}/confirm` 接受 JSON `{}`，要求原浏览器 Cookie、精确 Origin、X-CSRF-Token 与 Idempotency-Key。设备、账号及用途从服务端流程读取，不接受客户端自报目标。成功为既定 v1 封装，data 包含 device_session_id、revision、permission_version，版本是 JSON 整数。

手机无脚本表单使用同一路径及 CSRF、固定操作键，成功后 303 返回结果页；刷新结果页只读。Nginx/Shiro 仅增加精确 confirm 路径。结果页明确 UTF-8，保留严格 CSP，允许同源表单并保留同源 Origin。

短事务与已验证注销使用相同身份屏障，然后锁定浏览器、流程和设备。最终重新检查 30 分钟闲置、身份仍有效、请求用途/600 秒期限、准入与凭据快照、当前许可版本及无现有归属。数据库设备主键、行锁和单事务确保只有一个归属；多人扫描不会占用设备。

事务一并写入归属、ownership_version、permission_version、revision，消费请求并保存设备页面会话和操作结果。任何一步失败整体回滚。同浏览器、流程、幂等键的成功重试返回原结果，即使原二维码随后过期；身份/准入/凭据/归属代际仍重新校验。新键不能重新消费；历史重试不回退后来退出产生的许可版本，也不能跨转交后的归属代际使用。

## 身份与业务授权

签名、issuer/audience/nonce、state/PKCE 及 UserInfo.sub 仍必须验证。验证失败或拒绝授权不能绑定。业务 Key 领取失败、缺刷新能力或业务凭据无效时，保留可信身份，流程不伪造 ACTIVE，也不覆盖同账号已有共享授权。

流程记录本次取得的授权 generation。首次绑定检查当前代际：

| 条件 | 绑定后的设备状态 |
| --- | --- |
| 无本次有效业务授权，授权过期或代际已变 | bound、blocked、market_auth_required |
| 本次当前授权刷新结果未知 | bound、blocked、market_auth_pending |
| 本次当前业务授权有效 | bound、blocked、device_disabled |

最后一行是当前切片的阶段性语音禁用策略：尚未交付语音准入和输出保护，不能开放 allowed、lease 或 voice，也不能将 device_disabled 描述为集市授权失败。绑定成功只表示归属已保存。页面显示“设备已归属当前集市账号。语音使用暂未启用。”账号页与单台退出控制已由[设备页面切片](customer-device-session.md)补齐；已有设备的授权失效传播、恢复及产品语音许可仍待后续切片完成。resume/account 调用 confirm 仍返回 PURPOSE_MISMATCH，account 使用独立 open-account。

## 数据与回退

新增 `customer_device_session`、`customer_binding_operation`、设备归属代际及流程授权代际；保留现有字段、数据和原迁移校验和。操作记录暂不清理，超过协议要求的 24 小时保留期。历史流程只在能够匹配实际授权起始顺序时回填 generation，避免把其他登录的授权视为本次取得。

回退应用时关闭 device-control，保留表、设备归属、凭据和加密密钥。回退代码不撤销已建立归属或重新激活已消费二维码；解除归属须使用另行授权的业务操作，不能删表代替。PR #26 早期实验库的迁移编号限制仍见[控制开发说明](device-control-v1.md)。

## 验证

在已有 Ubuntu 独立集成环境运行 CustomerBindingHttpTest、CustomerBindingRestartTest；实际 MySQL、Redis、HTTP/Tomcat/Shiro 参与，可控集市仅替代外部身份服务。数据库写入用于预登记、管理员状态变化及真实保存故障夹具，归属和行为通过 HTTP 观察。

完整 Java 回归与浏览器按钮验证见[本轮证据](../verification/2026-11-10-phone-binding.md)。这不是公网部署、真实手机或真机验收，#11/#13 继续保留未完成关卡。
