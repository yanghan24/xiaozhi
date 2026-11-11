# 音箱设置配套服务交接

在 PR #52 的在线交付版本上增加设备侧账号查询和本机退出。继续使用现有数据库、设备凭据、集市客户端和 OIDC 密钥；本次不增加数据库迁移。

## 接口

以下路径都位于公网 `/xiaozhi/device-control/v1/` 下，使用已准入设备的 `Device-Id` 和设备 Bearer 凭据，响应为 v1 信封并禁用缓存。

| 请求 | 成功的 `data` |
| --- | --- |
| `GET account` | `device_id`、`permission_version`、`username`、`balance`。后两者可分别为 null；积分有值时为十进制字符串，可为负数。 |
| `POST sign-out` | 传 `{"expected_permission_version": N}`，成功返回 `{"permission_version": N+1}`。同一次退出重试返回相同版本；恢复后旧请求返回 409。 |

账号接口只读取本台设备当前归属账号的 UserInfo `preferred_username` 和实时积分。集市查询结束后再次检查归属及许可；账号授权令牌只留在服务端。退出保留归属及集市授权，立即关闭本台设备许可。固件从原有 `state` 确认结果，用 `state-acks` 回报停播；其他设备不受影响。

## Ubuntu 验证与部署

1. 在独立 checkout 固定本次服务端提交，按[在线交付部署说明](../development/device-delivery-v1.md#ubuntu-配套部署)运行隔离的 Java 测试。确认 `CustomerVoicePermissionHttpTest` 中的账号归属、缺积分授权、并发退出、停播 ACK、恢复后拒绝重放以及第二设备隔离用例实际通过。
2. 用同一提交构建 API，按既有方式备份并保留现场私有配置。由集市管理员为现有 OIDC 客户端开通 `balance:read`，再为产品 API 设置 `CUSTOMER_OIDC_BALANCE_ENABLED=true`。未开通时可保持默认关闭，继续使用语音和本机退出。仅以前未授予积分查询的账号需要在设置中用「扫码授权」补一次授权。
3. 在当前 TLS 入口合入 [Nginx 模板](../../deploy/device-entry/nginx-tls.conf)的两个精确路径，指向新 API。核对 `nginx -t`，API 启动后重载。通过公网确认两个路径无设备凭据时返回 401，旧 OTA、语音及扫码路径正常。
4. 服务端回归及公网检查通过后安装设置固件。用真实账号核对用户名和当前积分；在音箱上退出，确认后台收到新许可版本及停播 ACK；扫码恢复。保持音箱通电，在设置断开再连接 Wi-Fi，确认无需重新绑定即可继续语音。第二台设备应仍能回答。

回退时恢复原 API 和入口版本，保留已有数据及配置。已发生的本机退出仍需用原归属账号扫码恢复。
