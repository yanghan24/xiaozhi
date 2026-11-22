# 开发与验证指南

先读[项目架构与开发边界](../../README.md)和[团队协作规则](../../CONTRIBUTING.md)。按工作区适用的环境说明准备工具、运行目录、数据库和缓存。个人的容器、地址及命令位于 [`deploy/`](../../deploy/) 中各自的开发环境目录。开发数据、凭据和生产隔离。

## 按模块接手

| 工作 | 入口 |
| --- | --- |
| 管理页面 | [管理 Web](../../main/manager-web/)，复用现有路由、API 模块及组件。 |
| 用户、设备、诊断、MCP 与第三方配置 | [Java 管理 API](../../main/manager-api/)中的 `speaker`、`device`、`sys` 等模块。 |
| 智能体与模型 | Java 的 `agent`、`model` 模块负责管理及运行配置，Python 的 [`core/providers/`](../../main/xiaozhi-server/core/providers/) 承担提供者调用。 |
| 语音 | [Python 语音服务](../../main/xiaozhi-server/)，按现有 WebSocket、会话与音频链路接入。 |
| 固件和 UI | 配套固件工程的 `README.md`、P4X 产品文档和 UI 规范。 |

功能在职责所属模块内开发。独立职责可新增模块，跨端协议、认证、语音和智能体主流程或整体架构的变更先单独评审。外部业务和共享密钥按现有服务端模块接入。

## 验证

- 按改动范围运行对应的构建、静态检查和定向测试。使用开发数据库与模拟器时，记录实际验证的接口和调用范围。
- 有真机任务时，先按协作规则明确授权、版本、设备和可达的业务端点，在[单设备联调模板](../verification/device-integration-template.md)中记录实测结果。
- 需要发布时使用[生产部署规范](../../deploy/production/README.md)。已实现功能的过程和历史验收见[归档索引](../archive/README.md)。
