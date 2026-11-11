# 音箱设置配套服务交接

在 PR #52 的在线交付版本上增加设备侧账号查询及本机退出并解绑。2026-11-11 用户确认新固件本机退出应释放归属，供任何账号扫码接手，见 [ADR-0007](../adr/0007-device-local-unbind.md)。继续使用现有数据库、设备凭据、集市客户端和 OIDC 密钥；本次不增加数据库迁移。

## 接口

以下路径都位于公网 `/xiaozhi/device-control/v1/` 下，使用已准入设备的 `Device-Id` 和设备 Bearer 凭据，响应为 v1 信封并禁用缓存。

| 请求 | 成功的 `data` |
| --- | --- |
| `GET account` | `device_id`、`permission_version`、`username`、`balance`。后两者可分别为 null；积分有值时为十进制字符串，可为负数。 |
| `POST sign-out` | 传 `{"expected_permission_version": N}`，成功返回 `{"permission_version": N+1}`。同一次退出重试返回相同版本；恢复后旧请求返回 409。 |
| `POST unbind` | 传 `{"expected_permission_version": N}`，成功返回 `{"permission_version": N+1,"binding_state":"unbound"}`。只释放本台归属；仅当前仍未绑定且版本为 N+1 时可重放成功，重新绑定后旧请求返回 409。 |

账号接口只读取本台设备当前归属账号的 UserInfo 及实时积分。`username` 用于设备显示，与集市页面的名称一致：依次取有效的 `nickname`、OIDC `name`、`preferred_username`；不是 `sub` 或 OpenID。候选值必须非空、无控制字符且 UTF-8 不超过 256 字节。读取前验证 UserInfo `sub` 与归属账号一致，集市查询结束后再次检查归属及许可；账号授权令牌只留在服务端。

旧 `sign-out` 保留归属并关闭本台许可；新固件明确确认后使用 `unbind`，同时停用本台并释放归属。解绑递增归属代际，使旧手机页面、旧扫码确认、旧语音及迟到账号结果失效。已退出或需要原账号重新授权的已绑定设备同样支持本机解绑。固件从 `state` 确认结果，再取得正式新绑定二维码，并用 `state-acks` 回报停播；不修改原账号其他设备或共享集市授权。

## Ubuntu 验证与部署

1. 在独立 checkout 固定本次服务端提交，按[在线交付部署说明](../development/device-delivery-v1.md#ubuntu-配套部署)运行隔离的 Java 测试。确认 `CustomerVoicePermissionHttpTest` 中的跨账号重新绑定、同账号新归属、旧页面/退出重放围栏、并发账号读取、停播 ACK 及第二设备隔离用例实际通过。
2. 用同一提交构建 API，按既有方式备份并保留现场私有配置。由集市管理员为现有 OIDC 客户端开通 `balance:read`，再为产品 API 设置 `CUSTOMER_OIDC_BALANCE_ENABLED=true`。未开通时可保持默认关闭，继续使用语音和本机退出。仅以前未授予积分查询的账号需要在设置中用「扫码授权」补一次授权。
3. 在当前 TLS 入口合入 [Nginx 模板](../../deploy/device-entry/nginx-tls.conf)的 `account`、旧 `sign-out` 及新 `unbind` 精确路径，指向新 API。核对 `nginx -t`，API 启动后重载。运行 `python3 scripts/check-public-device-preflight.py`：新解绑路径无凭据/未签发凭据均为 v1 401，GET 被入口拒绝，旧 OTA、语音及扫码路径正常。
4. 服务端回归及公网检查通过后安装新固件。用真实账号核对用户名和当前积分；在音箱上确认退出并解绑，确认后台归属清空、新许可版本及停播 ACK，再用另一账号扫码接手。第二台设备应仍能回答，原账号旧页面及旧退出请求不得影响新绑定。保持音箱通电，在设置断开再连接 Wi-Fi，确认无需重新绑定即可继续语音。

回退时恢复原 API 和入口版本，保留已有数据及配置。已经完成的解绑不会随 Git/API 回退撤销，也不能恢复旧归属覆盖新账号；需保留当前业务数据。新固件在旧 API 不支持解绑时显示失败并保持本机停播，不能将旧 `sign-out` 作为降级替代。

## 已部署设置 API 的名称修正

初始设置 API 源提交 `31db6f8` 仅使用 `preferred_username`。现场反馈集市页面显示中文名称，而音箱显示英文数字；只读核对集市公开前端资源，确认其“用户名”位置使用 `nickname || username`。本次将设备显示名称对齐到昵称／显示名，保留原身份、积分、退出及鉴权契约。

这是 API 源码修正，不需要数据库迁移、路由调整、重新烧录或再次扫码。部署前在隔离 Ubuntu 测试环境运行 `CustomerProfileDisplayTest` 和 `CustomerVoicePermissionHttpTest`，按既有方式保留私有配置、构建并更新 API。随后在音箱设置点「刷新」，核对名称与当前集市页面一致，积分继续为实时值。

本机已用 Java 25 编译器的 Java 21 source/target 模式执行 6 项独立 JUnit 名称选择回归：旧逻辑 4 项失败，修正后 6 项通过。完整 HTTP/MySQL/Redis 回归仍由 Ubuntu 执行方验证；不能将独立测试作为整套服务端验收。
