# 长期烧录凭据 Ubuntu 部署与验证

时间：2026-11-10 21:34–21:36（Asia/Hong_Kong）。Ubuntu：`jt@192.168.168.114`。

## 部署结果

产品 API 部署固定源码 `3f963b6d5ce6110b3d68ff473f722f726d3c659e`。它在当前主线整合 PR #52 `87839aa` 后，增加长期、可单独撤销的烧录工具凭据；没有合并 PR #26/#52 或本任务到 main。

镜像 `xiaozhi-product-api:3f963b6`，ID `sha256:8e49c55c7c4f23f2f61b24223fbdc477ceadba4bdb0822c1e84c993bb9156666`。
运行 JAR 与本次 Ubuntu 测试镜像的 JAR 完全相同，SHA-256 为 `972ec8e5750b712b3225c9c32bdd6041deeb69ba435d07897a3a6e89a5b3bb05`。

仅替换并随后重启产品 API；语音 `885918d`、数据库、Redis、管理前端及公网入口容器未重建。Nginx 在验证配置后热重载，增补两个凭据管理精确路由块；TLS、既有设备/客户路由及原管理员账号保留。JetLinks 保持停止。

新增 Liquibase `202611102200` / `dc_delivery_credential`，仅保存工具秘密哈希及管理员、名称、签发/撤销/最近使用时间。一小时授权表不变。正式长期凭据已通过原管理员的正常登录会话签发，文件只保存在私有目录；本报告不包含凭据或管理员令牌。

## 验证

| 范围 | 结果 |
| --- | --- |
| Ubuntu 独立 MySQL/Redis 完整 Java 回归 | 337 tests，0 failures/errors/skipped |
| DeviceDeliveryHttpTest | 16 项通过，含模拟十年后使用、旧一小时到期、单凭据撤销、权限/管理员隔离及停用/降权 |
| 集成测试实际运行守卫 | 通过 |
| 公网媒体类型及拒绝协议 | 20/20 通过 |
| 公网 OTA、设备控制、WSS、方法/大小限制与管理/内部隔离 | 24/24 通过，正常验证证书 |
| 正常管理员签发两份独立凭据、撤销测试凭据 | 成功；只保留正式工作站凭据有效 |
| 重启前真实公网凭据检查 | 10/10 通过 |
| API 重启后真实公网凭据检查 | 8/8 通过；正式凭据仍可用、撤销项仍返回 401 |
| 工具越权访问签发、列表、撤销、authorize | 均返回 401 |
| 元数据列表 | 不含工具秘密和哈希；正式凭据 expires_at_ms 为 null |

公网有效凭据验证使用不存在的专用 operation ID；预期通过工具认证后返回 `404 DELIVERY_NOT_FOUND`，无效或撤销凭据为 `401 TOOL_AUTH_REQUIRED`。这一步不生成设备记录。真实交付全流程的覆盖来自独立数据库 HTTP 测试，本次未用真实音箱代测，未刷写固件或变更客户归属。

客户端使用约定见[操作说明](../development/long-lived-delivery-credentials.md)。工具应从私有 JSON 读取 `tool_token`，识别 `expires_at_ms: null`；尚未在外网实物烧录电脑验证其现有客户端解析器，不能将服务端通过等同于该电脑完成实物烧录。

## 备份、证据与恢复

- 部署前备份：`/home/jt/pr26-deployment-885918d/private/backups/20261110T133425Z`。
- 签发及撤销、重启验证后的备份：同目录 `20261110T133603Z`，确认包含新凭据表、既有账号和交付密钥，排除对话正文/音频。
- 回归源码与完整日志：`/home/jt/pr52-regression-3f963b6/.runtime/pr52/`。
- 部署配置快照：`/home/jt/pr52-deployment-3f963b6/`，包括前后 Compose/入口配置、镜像构建、备份及服务证据。
- 公网凭据检查的脱敏结果：`/home/jt/pr52-long-lived-private/checks-before.json` 与 `checks-after.json`。

本次使用的完整范围备份命令为：

```bash
sudo python3 /home/jt/pr52-deployment-3f963b6/backup.py 3f963b6d5ce6110b3d68ff473f722f726d3c659e
```

备份同服务器保存，未完成异地备份或恢复演练。回退 API 时须保留所有凭据、设备表和加密密钥；旧 API 不接受新长期凭据，先停止新烧录，不能把长期凭据改为管理员令牌。长期凭据丢失或泄露时从管理员列表按 ID 撤销并重新签发，不覆盖或恢复已撤销的秘密。
