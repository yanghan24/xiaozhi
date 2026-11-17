# #13 服务器接续签收（2026-11-13，Asia/Hong_Kong）

按本 issue 当前限定的单台登录/许可范围，服务器侧检查通过，结合 PR #90 的设备证据与用户本会话新增固件确认，五项关闭条件齐备。以下明确区分本次只读核验、历史复用及用户现场确认。

### 当前版本和 B 身份（10:05:53）

- API：`xiaozhi-product-api:1b50709`，源码 `1b50709fd1cadfad966b12d4afedf78b1f279aa6`；镜像 `sha256:429f8577b4559b27e7dacb558db0e4f53321faed2e2eb4f70798bef1f47ecb63`。运行 `/app/app.jar` SHA-256 为 `24eedf38b764c60a94d90392651f76db502333faac5c51f91dc8040fb506b24a`，与 362 项回归的运行产物一致。
- 语音：`xiaozhi-product-voice:pr79-f830a8c`，镜像 `sha256:19070d27ddd2332d43799c3caff669a6a131f180f98d500ef37824c3cc1bd7ca`。这是 PR #87 的镜像；不把历史 `pr81-75290c4-pr79-f830a8c` 结果说成同版本重测。
- API/语音均运行，启动时间分别为 2026-11-12 11:13:39 / 14:50:32（香港时间）。本次没有部署或重启。
- B：`admitted=1 / local_enabled=1 / reason=ready / permission_version=24 / ownership_version=7`。当前凭据对应唯一 delivered 恢复记录，已有真实设备验证时间；原设备、归属账号与硬件/交付映射保持。脱敏账号标记 `B-original-owner-unchanged-from-PR87`。历史许可22不冒充当前值，也不推断22→24全部操作原因。
- 在 Ubuntu 经 LAN443、正式域名且验证 TLS，当前凭据 GET state 为 `200/OK`；前驱旧凭据为 `401/INVALID_DEVICE_CREDENTIAL`。本次不是新的公网18443探测。检查前后设备、恢复和交付数据库快照完全相同。
- **B 当前固件：用户本会话明确回复“已刷入 12f2ff1”**，固定提交 `12f2ff1b3e224ff012bdce95799ebf19740dcd11`。这是用户安装确认，不冒充今天读取 Flash。昨晚原产品 `a30f6a8` / ELF `364a1d6f…` 的读回仅作为历史证据。已检查 `a30f6a8..12f2ff1`：生产代码仅 `main/application.cc` 的默认 AutoStop 和唤醒词打断后清队列/静默重入逻辑；设备身份、扫码、许可控制、退出/解绑及其停播处理未变。普通问答与关闭手机页后的完整播放依据 PR #90 的用户确认，不由版本差异推导实机成功。

### PR #76 专项结果已取回

原任务 `/home/jt/issue13-acceptance-b448aa0`，原脚本 `/home/jt/issue13-acceptance-run.sh`；原隔离 Compose 项目现无运行容器，未重复启动测试。

- 构建基线 `b448aa0`，测试镜像 `sha256:aaa966851a1a7a16f5d6a953a0f8b37c80080454cc2cb6f7d579f213eeb1e251`。
- 退出码 **0**；JUnit **tests=1, failures=0, errors=0, skipped=0**。
- `CustomerSecretExposureHttpTest.bindingPagesDeviceResponsesAndLogsDoNotExposeSecrets(CapturedOutput)` 实际存在且未跳过，无 failure/error。
- 报告：`.runtime/pr52/surefire-reports/TEST-xiaozhi.modules.customer.CustomerSecretExposureHttpTest.xml`。
- XML SHA-256：`2c667632afed202c36f60a40b2c49f98b3fa1a1cc8147fc701d50ebbf955d3bc`。
- 测试源码 LF SHA-256：`312d40b54a6e734292afb767f170655d74d22aa24f79954a14dea85a58932285`，与 PR #76 固定 head `f61d4234c5042a2e6dd1ab5ea0af8206c1b8c67c` 的目标用例完全一致。
- Ubuntu 对比专项目录与已部署组合版本回归目录：516 个 `src/main` 文件及 pom 一致。本地另确认 `b448aa0` 与 `1b50709` 的 manager-api/manager-web 树一致。该专项对应当前 API 的运行源码。

该结果验证真实 Spring HTTP 页面、回调、二维码所在状态响应、确认响应、非法请求响应和捕获应用输出不泄露测试秘密；不宣称审计了全部生产历史日志。PR #76 的第二个并发用例及旧 A01–A24 全矩阵不在此次通过声明内，按 #88 后续范围处理；不因此把整个 PR #76 标成已全面验证。

### 五项签收及交付 #14 / #18

| #13 条件 | 证据与结论 |
| --- | --- |
| 1. 固定 B/账号、有效准入、bind/resume 分支 | 本次身份/归属/delivered/新旧凭据检查通过；绑定与原账号恢复沿用 PR #90 固定交接所列 D1/D2/D4 及既有 HTTP 分支用例 |
| 2. 手机 OIDC、明确确认、关闭页面仍可使用 | #11 身份验收 + PR #90 的历史 B 手机证据；2026-11-13 用户明确确认 B 可用且关闭手机登录页后仍可完整问答 |
| 3. 拒绝与权限/秘密边界 | 当前 API 运行 JAR 与既有362项回归相同，复用 PR #67 的69项公网检查及 PR #76 的165项相关用例索引：错误/无效请求、客户与运营权限隔离、手机页面；本次另核对新旧凭据并收回上述1项秘密保护专项 |
| 4. 本台停用/停播、原账号仅恢复本台 | PR #90 D1/D3/D4/D5/D6 中按设备/日期/镜像分别标注的实机、ACK、重启和恢复证据；当前 API 不变，当前固件的许可/退出实现不变。手机 sign-out 保留归属，本机 unbind 按 ADR-0008；未重新执行整套物理流程 |
| 5. 版本和许可交付 | 本评论交付 #14/#18：B、用户确认固件12f2ff1、当前 API/语音镜像、原归属脱敏关联、许可24/归属7、delivered 身份及证据。后续沿用当前凭据，不能恢复旧凭据；正式资源调用/OTA 前应重新读取实时许可 |

固定设备侧入口：[PR #90 交接](https://github.com/yanghan24/xiaozhi/blob/5c25b4c4a26feed7473b038394c995aeb2f56c4d/docs/handoff/2026-11-13-issue13-device-to-server.md)。权限与历史证据：[PR #67 报告](https://github.com/yanghan24/xiaozhi/blob/6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80/docs/verification/2026-11-11-pr67-deployment.md)、[PR #76 索引](https://github.com/yanghan24/xiaozhi/blob/f61d4234c5042a2e6dd1ab5ea0af8206c1b8c67c/docs/verification/2026-11-11-issue13-server-acceptance.md)。

脱敏原始摘要保留于 Ubuntu `/home/jt/issue13-server-audit-20261113.json`，专项 XML 与原日志仍在隔离任务目录；不发布凭据、账号标识或原始日志。本次没有重新补发、刷机、业务写入、服务重启或模型调用。

**结论：按当前五项范围完成 #13，关闭为 completed。** #14 自动 MCP、#18/#19 OTA整链、#42异常恢复及#88扩展矩阵仍独立；不扩大为这些事项通过；验收时尚未合并 PR #90/#76，也不改写 PR #87 当时的失败结论或声称已定位根因。

[原始签收评论](https://github.com/yanghan24/xiaozhi/issues/13#issuecomment-6841702127)；[本次脱敏服务器摘要](evidence/2026-11-13-issue13-server-audit.json)。
