# #11 身份验收汇总与剩余边界

> 2026-11-11 最终验收已汇总于[收尾报告](2026-11-11-identity-closeout.md)，包含用户确认的契约/分层验收口径及新增真实撤销恢复证据。下文保留历史状态。


> 2026-11-11 状态补充：[#28](https://github.com/yanghan24/xiaozhi/issues/28) 已按环境与登记准备范围关闭。实际登记与授权范围、通知失败策略及其他剩余事项由 [#11](https://github.com/yanghan24/xiaozhi/issues/11) 跟踪；重试间隔和长期刷新规则以 #11 的后续确认记录为准。下文保留汇总时的证据、状态及当时未决事项。

更新：2026-11-10，香港时间（UTC+08:00）。本次仅整理既有真实证据及核对测试源码，没有重跑 Java 套件、制造集市故障或修改服务。#11/#28 保持打开；不调整依赖和 triage 标签。

## 真实验收结论

| 场景 | 证据与边界 |
| --- | --- |
| 登录、拒绝授权、刷新轮换 | 11 月 9 日真实验证通过，见[原报告](2026-11-09-real-market-identity.md) |
| 共享撤销、失效识别、重新授权 | 真实模型列表 200 → 撤销成功 → 同一 Key 401；自然刷新后 REAUTH_REQUIRED，再授权恢复 ACTIVE；未进行模型推理 |
| 真实退出通知 | 09:03:30 POST 204，收据匹配本轮 issuer + sid，SESSION_ENDED / ACTIVE；09:13:31 第二独立账号重复通过，见[通知记录](2026-11-10-real-market-logout.md) |
| 集市侧时间线 | 用户提供：09:03:28 浏览器进入 OIDC 注销，09:03:30 确认、完成退出并跳转。与小智收据同秒对齐；非代理直接读取集市日志，不解释 09:03:45 的 400 |
| 双账号退出隔离 | 09:31:36 两个不同账号均 IDENTITY_VERIFIED / ACTIVE；仅退出 B，09:32:01 POST 204，09:32:01.114 新收据匹配 B；09:32:16 B 为 SESSION_ENDED，A 仍 IDENTITY_VERIFIED，两授权 ACTIVE。真实收据 2 → 3。未检查 A 浏览器后续受保护操作或设备许可 |
| 网页 30 分钟闲置 | 新一轮最后活动 10:17:02.611，截止 10:47:02.611；指导用户保留原结果标签页并直接刷新，反馈 IDENTITY_REQUIRED。10:48:45.042 SSH 核查已闲置 31.71 分钟，活动时间未变化、授权 ACTIVE。历史 flow 仍 IDENTITY_VERIFIED，不代表浏览器仍可访问 |

闲置响应由用户提供，未直接捕获 HTTP 状态；不是秒级临界点测量。此前重新打开链接得到 FLOW_NOT_FOUND 的一轮保留为未完成，不用后续成功覆盖历史。

证据评论：[集市时间线](https://github.com/yanghan24/xiaozhi/issues/11#issuecomment-6756292428)、[隔离测试](https://github.com/yanghan24/xiaozhi/issues/11#issuecomment-6756340176)、[闲置复测](https://github.com/yanghan24/xiaozhi/issues/11#issuecomment-6757560453)。只读脚本保留在服务器 `/home/jt/`：`xiaozhi-two-account-status-20261110.sh`、`xiaozhi-two-account-b-match-20261110.sh`、`xiaozhi-idle-retest-status-20261110.sh`。私有身份、Cookie、sid、令牌不进入仓库。

## 自动回归与真实事实的逐项对照

以下方法位于 [CustomerIdentityHttpTest.java](../../main/manager-api/src/test/java/xiaozhi/modules/customer/CustomerIdentityHttpTest.java)，重启项位于 [CustomerIdentityRestartTest.java](../../main/manager-api/src/test/java/xiaozhi/modules/customer/CustomerIdentityRestartTest.java)。本轮核对具体断言；执行依据为 [#29 历史报告](2026-11-09-customer-identity.md) 的 239 项通过记录，不声称本轮重跑。

| 边界 | 已有可控回归 | 真实证据 / 剩余工作 |
| --- | --- | --- |
| 网络/5xx 与授权失效区分 | `uncertainRefreshNeverReplaysAndOnlyConfirmedInvalidGrantRequiresReauthorization`：lost/server → REFRESH_UNCERTAIN；invalid → REAUTH_REQUIRED；旧令牌仅尝试一次 | 真实撤销后失效已通过；真实响应丢失/5xx 尚未注入。后续须隔离专用账号链路，不停止公共入口或修改共享网络 |
| 旧刷新失败不覆盖新授权 | `oldRefreshFailureCannotInvalidateNewAuthorization`：阻塞旧刷新、先新授权、释放旧 invalid，仍 ACTIVE | 真实顺序撤销/重登通过；真实并发竞态未复现。可控证据不能写成集市竞态实测 |
| 旧回调不覆盖新授权 | `olderCallbackCannotReplaceNewerGrantEvenWhenFlowsStartInSameMillisecond`：只有新刷新令牌被使用 | 可控回归覆盖；真实延迟回调竞态未执行 |
| 保存失败与重放保护 | `failedRefreshPersistenceNeverReplaysTheConsumedRefreshToken`：MySQL trigger 故障后 REAUTH_REQUIRED、旧令牌仅一次 | 仅独立回归库验证；不对真实授权数据库注入 trigger |
| 无效身份与上游失败 | `invalidIdentityOrUpstreamFailureCannotCreateIdentity`：nonce/issuer/audience/expired/signature/userinfo/network 均无授权 | 真实用户拒绝已通过；真实服务临时错误页面表现未验证 |
| 通知幂等与范围 | `signedBackchannelNeedsNoCookieInvalidatesOnlyMatchingIdentityAndPreservesGrant`：重复 204、另一 flow 保留、授权 ACTIVE；`invalidLogoutDoesNotChangeAnyIdentity` 检查拒绝无副作用 | 两账号真实 sid 退出及隔离已通过；真实重复 jti、无 sid 按 sub 的通知范围未验证。09:03:45 400 不可直接视作幂等缺陷 |
| 通知/回调竞态 | `logoutReceivedDuringLoginCannotBeUndoneByTheCallback` | 可控回归覆盖；真实通知先到再回调的竞态未验证 |
| 闲置边界与轮询 | `transactionExpiresAtFiveMinutesAndPollingCannotExtendThirtyMinuteIdle`：1799 秒 200、1801 秒 401；OIDC 301 秒 410 | 真实超过 30 分钟及授权保持通过；秒级边界来自可控 Clock，不能混称真实测量 |
| 重启与多实例 | `identityRefreshChainAndLogoutDedupSurviveRestartAndConcurrentInstances`：去重/状态保留、旧令牌仅兑换一次、重启后闲置 401 | #29 独立应用真实重启 + 可控集市；本轮未重启真实身份服务 |
| 无本应用权限 | 用户取消 consent 与权限拒绝不是同一条件 | 用户于 2026-11-10 明确暂不考虑，移出当前阶段收尾条件，保留未验证；账号密码错误不计通过。后续恢复范围时再准备账号 |
| 撤销的过期/失败恢复 | 当前真实撤销用有效 Access Token；通用刷新错误测试不等于撤销端点失败测试 | 过期 Access Token、撤销 5xx/超时后的恢复行为仍需明确及专项验证；不重做共享撤销作为替代 |
| 真实集市能力与配置 | 可控提供方不证明集市配置 | 实际获准 scope/配置字段、通知发送重试次数/间隔/失败限制、长期刷新闲置期限仍需配置或官方证据，不用数次刷新冒充 30 天验收 |

## 剩余工作与执行条件

1. 集市侧：核实 09:03:45 的请求用途、关联发送记录、响应错误，以及触发范围与重试策略；同一来源 IP 不能证明是同一发送进程。当前请求体未保存，没有针对这条实际请求的可重放证据。
2. #28：提供实际登记/批准范围及长期刷新期限的脱敏配置证据。已有两名独立测试账号；无本应用权限测试按用户决定暂缓，不作为当前阶段阻塞，也不标为通过。
3. #11：在隔离环境设计真实临时失败及撤销过期/失败恢复验证。明确将哪些竞态保留为可控回归证据、哪些仍要求真实验证；未作出验收范围决定前不自动勾选或删除原要求。
4. 已知边界：本次只验证 OIDC 浏览器退出；集市网站退出、集团 4A 全局退出、管理员停用应用及共享授权撤销的通知覆盖不能泛化。端点成功不等于看到了全部登记字段。

#13 设备绑定/许可传播/真机和 #12/#14 资源调用保持各自范围。网页直接展示错误 JSON 是可单列的体验改进，不是本轮已证实的闲置逻辑缺陷。
