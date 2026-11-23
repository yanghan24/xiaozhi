# 小智项目文档

先从[项目 README](../README.md)了解整体架构、语音与智能体链路、模块入口和开发边界。

| 查找内容 | 入口 |
| --- | --- |
| 开发、模块扩展和验证 | [开发指南](development/README.md)、[团队协作规则](../CONTRIBUTING.md) |
| 当前架构决策 | [ADR 索引](adr/README.md) |
| 生产部署和固件发布 | [生产部署规范](../deploy/production/README.md)、[当前生产状态](deployment/current.md) |
| 实机验收记录怎么写 | [验证与证据说明](verification/README.md)、[单设备联调模板](verification/device-integration-template.md) |
| 任务、分流和领域文档 | [任务管理](agents/issue-tracker.md)、[分流标签](agents/triage-labels.md)、[领域文档](agents/domain.md) |
| 已交付记录、被替代方案和上游材料 | [归档索引](archive/README.md) |

配套固件工程的 `README.md`、`docs/speaker-market.md` 和 `docs/ui/README.md` 分别维护固件架构、P4X 产品模块和 UI 规范。

新功能文档从实际代码核对模块、接口和运行边界。当前规则依照根 README、有效的 ADR、协作规则和对应的操作规范维护。归档记载当时的方案、实现与验证范围。
