# 架构决策索引

当前模块、运行链路和开发边界见[项目 README](../../README.md)。ADR 保留编号和决策经过，每份开头说明当前适用范围。生产操作见[生产部署规范](../../deploy/production/README.md)。

| 记录 | 当前适用范围 |
| --- | --- |
| [0001 集市身份与权限](0001-use-ai-market-identity-and-permissions.md) | 集市负责身份与权限。扫码入口及接入范围以后续决定为准。 |
| [0002 设备绑定由集市管理](0002-market-owns-device-bindings.md) | 已替代。现行设备与归属职责见 0005。 |
| [0003 网页 OIDC 绑定](0003-oidc-web-login-for-device-binding.md) | 已替代。现行集市设备授权见 0010。 |
| [0004 采用上游服务端](0004-extend-upstream-xiaozhi-server.md) | 沿用上游工程、协议与版权，稳定运行链路见 0011。 |
| [0005 后台维护设备与归属](0005-backend-owns-device-management.md) | 职责有效，现行扫码和本机解绑分别见 0010 和 0008。 |
| [0006 对话正文备份边界](0006-exclude-conversation-content-from-backups.md) | 现行首版备份排除对话正文。 |
| [0007 日程数据与备份](0007-back-up-schedules-as-business-data.md) | 归属隔离及访问要求和历史备份设计。当前记事由音箱本机保存。 |
| [0008 本机确认解绑](0008-device-local-unbind.md) | 本机确认解绑及权限失效规则有效。 |
| [0009 独立设备在线](0009-device-presence-and-standby-ota.md) | 独立认证心跳有效，旧推送 OTA 设计已由 0011 的现行链路替代。 |
| [0010 集市设备授权](0010-speaker-device-authorization.md) | 集市设备授权、运营后台权限和默认智能体规则有效。 |
| [0011 稳定上游运行链路](0011-restore-upstream-runtime-and-separate-identity.md) | 上游语音与 OTA 边界有效。实施阶段文字按历史记录阅读。 |
