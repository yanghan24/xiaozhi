# #13 无硬件服务器验收整理

2026-11-11，Asia/Hong_Kong。服务器为 `jt@192.168.168.114`。本轮完成现有证据索引及 A01–A24 缺口划分；新增两项测试尚未取得执行结果，不能宣称服务器缺口全部完成，也不关闭 #13 / #42。

## 版本与证据口径

- 工作基线 `b448aa0`；新增内容仅为测试和验收文档。该基线的 `main/manager-api`、`main/manager-web` 与现网受测组合 `1b50709fd1cadfad966b12d4afedf78b1f279aa6` 相同。
- [既有组合回归](2026-11-11-pr67-deployment.md)共 362 项通过。本轮从 Ubuntu `/home/jt/pr67-regression-1b50709/.runtime/pr52/surefire-reports` 提取其中 10 个相关测试套件、165 项用例，0 失败/错误/跳过。[索引](evidence/2026-11-11-issue13-server-acceptance/existing-cases.json)保留每个用例名及源 XML SHA-256。这是证据复核，不是本轮重跑，也不是新增 165 项。
- 身份前置 #11 已关闭，沿用[最终验收与明确边界](2026-11-11-identity-closeout.md)，不再列为未满足依赖。
- 补发后 API 重启已于 20:31–20:32 完成，重启后 34 项公网/凭据检查通过，设备状态、补发记录与配置密钥一致。见 [PR #74 固定报告](https://github.com/yanghan24/xiaozhi/blob/3d7ac78092559e8d7e857a8fee5e4e28fb0b07c6/docs/verification/2026-11-11-pr66-ubuntu-verification.md)。该记录不代表 Ubuntu 主机重启或本轮 SSH 异常已恢复。

## A01–A24 证据矩阵

下表的“已有覆盖”只指所列证据范围，不等于对应整项验收打勾。用例名可在上述 JSON 索引中检索；参数化用例的分支以源码为准。

| 条目 | 已有服务器覆盖/证据 | 剩余边界 |
| --- | --- | --- |
| A01 准入与凭据 | `preregisteredDeviceReadsBlockedStateButCannotImpersonateAnotherDevice`、`selfReportedDeviceIdentityCannotReadControlState`；PR #74 旧凭据 401 | 实物改地址接入不能由 HTTP 模拟替代 |
| A02 独立身份与重复登记 | `twoDevicesHaveDistinctPendingIdentitiesAndCannotUseEachOthersCredentials`、`concurrentCreateHasOneDurableOperationAndNoOrphanDevice` | 结合 A23 实际交付证据核对 |
| A03 状态与二维码 | `admittedDeviceQrUsesCurrentAdmissionAndRequestLifetime`；控制状态 HTTP 测试 | 实物屏幕、离线提示与重连 |
| A04 10 分钟与旧码 | `creatingAndRetryingBindingRequestPreservesItsIdentityAndDeadline`、`expiredRequestsCannotBeResurrectedByIdempotentRetry`、`validRequestOpensPhonePageAndExpiredRequestCannotBeUsed` | 屏幕自动换码、临界时刻操作 |
| A05 登录中请求到期 | `accountEntryRechecksTheRequestAtFinalSubmission`、`confirmationRejectsInvalidIdentityOrChangedDeviceWithoutBinding` | 真实手机重扫及正式 OIDC 复用登录路径 |
| A06 回调与最终确认 | 身份 HTTP 43 项；`callbackCannotCrossBrowsersOrReplayAndRefusalIsClear`、`invalidIdentityOrUpstreamFailureCannotCreateIdentity`；绑定拒绝分支 | 按 #11 已验收契约使用真实身份证据；实际设备联动仍独立验收 |
| A07 并发唯一归属 | `twoAccountsConfirmingConcurrentlyCannotOverwriteTheWinner` | 已有服务器并发证据；多手机现场体验未新增 |
| A08 幂等与重启 | `failedReceiptSaveRollsBackOwnershipAndTheSameRequestCanRetry`、`crossInstanceWinnerAndRetryReceiptSurviveApplicationRestart`、`independentInstancesShareRequestsAndObserveChangesAfterRestart` | 服务器自动化已有覆盖；现网 API 重启另见 PR #74 |
| A09 多设备与授权不足 | `trustedIdentityCanBindWithoutPretendingBusinessAuthorizationSucceeded`、`anotherLoginWithoutBusinessCredentialsDoesNotDiscardTheSharedGrant`；语音双机用例 | 无应用权限真实账号按 #11 既定范围暂缓；不从模拟扩大真实资源权限结论 |
| A10 权限边界 | `onlyCurrentAdministratorsCanAuthorizeOrUseTools`、`toolCredentialCannotAdministerCredentialsOrBecomeAnAdministratorOrDevice`、设备会话越权拒绝；PR #74 recovery 越权检查 | 本索引未专门归档运营用户名/密码登录回归，需补对应证据，不能只凭注入管理员 token 认定登录通过 |
| A11 单台退出 | `ownerCanSignOutOnlyThisDeviceAndObserveItsStopAcknowledgement`、`signOutWakesOtherInstanceAndReceiptSurvivesRestart`、旧 ACK 围栏 | 物理停播/迟到音频与设备重启需结合现场报告；本轮未操作硬件 |
| A12 退出与共享撤销 | `signedBackchannelNeedsNoCookieInvalidatesOnlyMatchingIdentityAndPreservesGrant`、`rejectedKeyStopsSharedDevicesAndOldGenerationCannotStopReplacement`；#11 真实撤销记录 | 集市不可达时本地退出的专门故障证据仍需补；不扩大外部通知保证 |
| A13 显式恢复范围 | `explicitOwnerResumeEnablesOnlyTargetAndIssuesBoundedVoiceCredentials`、`resumeRequiresOriginalOwnerAndCurrentGrantGeneration`、`delayedRejectionAfterReloginStopsOldPermissionsButNotNewExplicitResume` | 服务器已有覆盖；实际设备按各自许可恢复仍需现场核对 |
| A14 退出/解绑与共享刷新 | 既有解绑隔离、共享失效及 `accessRefreshDoesNotSignOutAnOtherwiseValidModelGrant` | 本轮新增退出与刷新并发测试待运行；真实 B 继续播放不能由 HTTP 证明 |
| A15 期限与故障区分 | `refreshRotatesOnceBeforeAccessExpiryWhileBusinessKeyWithNoExpiryRemainsUsable`、`uncertainRefreshNeverReplaysAndOnlyConfirmedInvalidGrantRequiresReauthorization`；#11 契约边界 | 30 天长期实测未做，遵循 #11 已确认范围 |
| A16 多实例刷新与不确定结果 | `identityRefreshChainAndLogoutDedupSurviveRestartAndConcurrentInstances`、`failedRefreshPersistenceNeverReplaysTheConsumedRefreshToken`、`oldRefreshFailureCannotInvalidateNewAuthorization` | 既有可控故障证据，不宣称真实集市发生相同故障 |
| A17 后通道通知 | `invalidLogoutDoesNotChangeAnyIdentity`、`signedBackchannelNeedsNoCookieInvalidatesOnlyMatchingIdentityAndPreservesGrant`、跨重启去重、`logoutReceivedDuringLoginCannotBeUndoneByTheCallback`；#11 真实送达 | 外部覆盖及重试保证按 #11 边界，不重复要求已豁免范围 |
| A18 补发 | `administratorRecoveryKeepsTheDeviceAndRejectsOldCredentialAndDeliveryReplay`、`credentialRecoveryKeepsOwnerAndOtherDeviceButRejectsOldVoiceAndLateResume`；PR #66 写回和 PR #74 独立凭据检查 | B 后续换绑不能倒推补发瞬间归属保持；缺紧邻历史快照，不补造 |
| A19 秘密不泄露 | 既有脱敏工具 9 项、普通交付清单不含秘密；本轮新增真实 HTTP/应用日志检查 | 新测试未完成；实际二维码屏幕和生产历史日志未全面审计 |
| A20 仅手机确认 | `phoneConfirmationBindsOnceAndSafeRetryReturnsTheOriginalResult` 与回调、最终提交拒绝用例 | 实际手机首次绑定且无需音箱确认仍须现场闭环 |
| A21 菜单/重置/异常身份 | [#42 身份保留报告](2026-11-11-device-identity-retention.md)已有部分实物证据 | 其余硬件关卡交硬件方，不能用模拟打勾 |
| A22 最终整合 | #11 真实身份、公网协议、[单台设备现场结果](2026-11-11-pr26-main-sync.md)及 #67 联动证据 | 交付版本完整手机/设备流程及未覆盖实际集市项目仍逐项验收 |
| A23 双机交付 | [双机交付及进程中断续作](2026-11-11-two-device-delivery.md)；交付 HTTP 20 项 | 写入瞬间断电不在既有证据内；不混同进程中断 |
| A24 身份维护/恢复 | #42 应用更新、USB 复位、重新配网、补发写回；PR #74 新旧凭据及 API 重启 | 实际 OTA、正常恢复出厂、异常丢失与完整恢复场景仍按硬件清单执行 |

## 本轮新增测试与中断状态

1. `CustomerSecretExposureHttpTest.bindingPagesDeviceResponsesAndLogsDoNotExposeSecrets`：真实 Spring HTTP、数据库及 Redis，仅替换集市；检查绑定页面、回调、状态、确认结果、含秘密的非法请求错误响应和捕获的应用输出。禁止设备凭据、授权码、Client Secret、Key、access/refresh token 出现，失败信息只给索引。不是生产历史日志审计。
2. `CustomerVoicePermissionHttpTest.signingOutOneDeviceDuringSharedRefreshKeepsPeerAndRotatedGrant`：双机共享账号，阻塞正在进行的刷新时退出 A，再释放刷新，检查 A 保持退出、B 许可不变、刷新轮换一次且 B 可获取新调用权限。不是物理音频测试。

第一项已上传 `/home/jt/issue13-acceptance-b448aa0`，由 `/home/jt/issue13-acceptance-run.sh` 启动独立构建/测试。Compose 项目名为 `xiaozhi-issue13-acceptance-b448aa0`，使用专属 MySQL/Redis 和卷，不连接现网库。随后原 SSH 连接被远端关闭，多次新连接在 SSH banner 握手超时。无法确认构建或测试是否完成，也无法确认隔离资源是否已自动停止。第二项仅在本地准备，未上传运行。两项均记为**待验证**，不增加通过数。

连接异常原因尚未诊断；不能据此声称主机崩溃、测试导致异常或生产 API 已故障，也不能将之前的 API 重启成功当作当前健康证明。本轮未重启主机或重复启动隔离任务。

## 无硬件接续顺序

1. 恢复 SSH 后先只读确认主机、现网服务以及隔离项目状态，检查 `.runtime/pr52/build.log`、`tests.log`、`exit-code.txt` 和报告是否存在；只提取脱敏错误与统计，不输出秘密。
2. 原脚本会拒绝覆盖已存在的 `.env`；不得盲目重跑或并行启动第二份。确认原任务结束后补传第二项测试，在隔离环境运行这两项，必要时修正测试并记录真实结果。完成后停止该隔离项目，保留报告。
3. 补 A10 运营登录、A12 集市不可达本地退出的明确证据；按 A19 范围补秘密泄露检查。以上均为服务器可执行缺口，不转交硬件方。
4. 测试通过后再将本 PR 由草稿改为可审阅。硬件方使用 [PR #74 固定交接清单](https://github.com/yanghan24/xiaozhi/blob/3d7ac78092559e8d7e857a8fee5e4e28fb0b07c6/docs/handoff/2026-11-11-pr66-hardware-acceptance.md)补实际场景；本矩阵不自动关闭任何验收项。
