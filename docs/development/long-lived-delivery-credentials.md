# 长期烧录工具凭据

用于无法进入管理登录页的外网烧录工作站。管理员通过既有正常登录签发专用凭据；工作站直接访问公网 HTTPS 的交付接口。无需给工作站管理员密码，也不修改音箱自身的独立设备凭据。

凭据不自动到期，响应 `expires_at_ms: null`；它可以单独撤销。每次工具调用仍核对签发管理员当前启用且为超级管理员。管理员停用或降权期间全部关联凭据不可用，重新启用后未撤销凭据可继续使用；已撤销凭据不能恢复。重新签发产生不同秘密。

权限仅限现有 operations 创建/查询/完成接口，不可管理用户、创建其他凭据、调用 authorize 换取授权或冒充音箱 device-check。原一小时授权保持原行为。交付记录继续按管理员隔离，同一管理员签发的多个工作站凭据可续作该管理员已有操作；若需不同工作站互相隔离，应使用不同运营管理员。撤销凭据不删除设备或改变客户归属，已经在执行的请求可能完成，后续认证被拒绝。

## 管理接口

前缀 `/xiaozhi/device-delivery/v1`，要求正常超级管理员登录 Bearer、HTTPS、无 query、严格 v1 JSON 及 no-store。短期或长期工具凭据均不能访问这些管理接口。

| 方法 | 路径 | 请求 | 结果 |
| --- | --- | --- | --- |
| POST | `/credentials` | `{"version":1,"name":"external-flashing-pc"}` | 元数据及仅此一次返回的 `tool_token` |
| GET | `/credentials` | 无 | 当前管理员最新 200 条凭据元数据，含撤销项，不含秘密或哈希 |
| POST | `/credentials/{credential_id}/revoke` | `{"version":1}` | 撤销元数据；重复撤销保持首次时间 |

名字为 1–64 个字母、数字、空格、点、下划线或连字符，无首尾空白。`credential_id` 为 32 位小写十六进制。只保存 SHA-256，不提供秘密读回；签发响应丢失时从列表找到并撤销该次凭据，再签发新的，不删除记录。

## 管理命令

`admin-token.json` 来自正常登录，私有 JSON 中为 `token` 字段。不得伪造登录或绕过验证码。Unix 私有文件使用 0600；Windows 使用仅本人和 SYSTEM 可读的 ACL。

```bash
python3 scripts/manage-delivery-credentials.py --origin https://bridge.hncig.cn:18443 \
  --admin-token /private/admin-token.json issue --name external-flashing-pc \
  --output /private/flashing-credential.json
python3 scripts/manage-delivery-credentials.py --origin https://bridge.hncig.cn:18443 \
  --admin-token /private/admin-token.json list
python3 scripts/manage-delivery-credentials.py --origin https://bridge.hncig.cn:18443 \
  --admin-token /private/admin-token.json revoke --id CREDENTIAL_ID
```

命令不会覆盖已有文件，也不将秘密打印到终端。传给工作站的 JSON 含 `tool_token`、`credential_id`、`expires_at_ms: null`、`scope: device_delivery` 和可信 `origin`。烧录客户端从文件读取 `tool_token`，作为原交付接口的 Bearer；遇到 401/403 必须在串口/Flash 操作前停止。客户端如果强制要求数值到期时间，必须增加 null 表示无自动到期的支持，不能填写伪造到期值。

## 部署与恢复

新增表 `dc_delivery_credential`，不改写 `dc_delivery_tool` 的既有到期时间。将 Nginx 模板的两个精确 credentials 路由合入现场入口，保持既有管理页面和内部 API 不对公网开放。正常 TLS 和管理 Bearer 共同保护签发/查询/撤销接口。

先执行 Ubuntu 独立数据库完整 Java 测试和集成守卫，再备份并更新 API/入口。备份现有 `dc_` 范围包含新表；保留交付密钥及账号，排除对话正文/音频。回退旧 API 会拒绝长期凭据，须提前停止新烧录；不能把凭据转换为管理员 token 或删除设备来恢复。重新部署兼容 API 后，原未撤销凭据继续可用。
