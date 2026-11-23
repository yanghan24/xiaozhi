---
status: accepted
---

# 基于上游小智服务端工程二次开发

**现行范围**。以固定的小智上游服务端二次开发，保留上游版权和 MIT 许可。现行语音、智能体、模型和 OTA 边界见 [ADR-0011](0011-restore-upstream-runtime-and-separate-identity.md)及[项目 README](../../README.md)。下文的 OIDC、规范通和 Edge TTS 属于当时的实施范围。

## 历史决策与实施

采用 xinnan-tech/xiaozhi-esp32-server 作为代码基础，复用其管理页面、Java 管理 API、Python 语音服务和现有 OTA 检查流程，接入集市 OIDC、规范通和计费接口，并改造小智侧设备绑定与固件发布流程。这样可以沿用已有设备协议与运行链路，将新增工作集中在集市集成和产品行为上；修改时保留上游版权及 MIT 许可声明，并记录开发基线对应的上游版本。决策时仓库尚未导入上游工程，要求在导入前结合基础运行验证确定开发基线，参考提交不自动成为发布基线。

## 实施进展（2026-11-06）

已导入固定提交 `c8e60d1325650c8a31d00e36d8528f8bb74aeb11`，详见[上游源码基线](../archive/upstream/upstream-baseline.md)。管理、设备协议与 Ubuntu 重启持久化已分别验收；真机兼容性和正式产品能力仍由对应任务验证。该进展不改变本 ADR 的二次开发选择。

## 2026-11-10 接入范围调整

本期将规范通智能体接入替换为 [MCP 知识库问答](../archive/superseded/mcp/planning/mcp-knowledge-scope-2026-11-10.md)，沿用默认 Edge TTS；智能体接入留待后续需求。本 ADR 采用上游工程二次开发的决定、许可要求及设备管理职责不变。

2026-11-17：第一阶段范围以 [ADR-0010](0010-speaker-device-authorization.md) 为准，先完成设备授权与默认角色，不携带历史固定知识库提示词/工具。沿用 Vue/Java/Python 工程及组件是本次实现选择，不要求保留有问题的旧功能设计。
