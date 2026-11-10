# 小智智能音箱

基于上游小智服务端工程开发，接入元筑 AI 集市。已完成管理/设备协议基线与 Ubuntu 部署、重启验证；真实集市与硬件联调继续分别验收，业务范围和职责以 [需求记录](docs/planning/voice-backend-discovery.md)、[领域词汇](GLOSSARY.md)及 [ADR](docs/adr/) 为准。

- [当前部署、公网地址与运行方式](docs/deployment/current.md)
- [本机管理端运行与验证](docs/development/manager-baseline.md)
- [本期 MCP 知识库接入范围](docs/planning/mcp-knowledge-scope-2026-11-10.md)
- [首版产品规则收口](docs/planning/product-rules-2026-11-09.md)
- [日程、室外环境与烧录登记新增需求](docs/planning/reminders-weather-discovery-2026-11-10.md)（已纳入 2026-12-07 目标）、[天气替代调研](docs/development/weather-location-replacement-research.md)
- [实施规格、任务边界与验收导航](docs/planning/task-spec-index.md)
- [真实集市身份联调结果](docs/verification/2026-11-09-real-market-identity.md)、[注销通知管理员检查清单](docs/handoff/2026-11-10-market-logout-notification.md)
- [本机语音服务与设备协议验证](docs/development/voice-baseline.md)
- [上游源码来源与导入范围](docs/development/upstream-baseline.md)
- 实际验证记录：[管理端](docs/verification/issue-3-manager-baseline.md)、[语音与设备协议](docs/verification/issue-4-voice-protocol.md)
- [任务 #3](https://github.com/yanghan24/xiaozhi/issues/3)、[任务 #4](https://github.com/yanghan24/xiaozhi/issues/4)、[父规格 #2](https://github.com/yanghan24/xiaozhi/issues/2)
- [2026-11-06 Ubuntu 独立测试部署（历史）](docs/deployment/ubuntu-isolated-test.md)、[A 层验收结果](docs/verification/issue-5-acceptance.md)
- [P4 样机语音与固件交接](docs/handoff/2026-11-10-device-firmware.md)、[原有 UI 恢复记录](docs/verification/2026-11-10-p4-ui-recovery.md)
- [换机/服务器交接](docs/handoff/2026-11-05-ubuntu-server.md)、[文档同步核查](docs/planning/document-sync-audit-2026-11-06.md)
- [团队协作与回退](CONTRIBUTING.md)
- [筑音硬件接入资料](筑音设备接入资料_核查稿_2026-11-06.md)
- [集市当前接入基线](docs/planning/voice-backend-discovery.md#当前集市接入基线2026-11-06)、[契约核对](docs/development/ai-market-contract-review-2026-11-06.md)
- [新版集市接入原文](<元筑AI集市应用接入接口文档(1).md>)、[旧版对照](元筑AI集市应用接入接口文档.md)
- [历史规范通与 TTS 资料](docs/development/ai-market-api.md)、[原始问卷及更新说明](to-questionnaire-ai-market-integration.md)

根目录 `compose.yaml` 用于独立测试基线；现有服务器的运行方式见[当前部署入口](docs/deployment/current.md)。保留的 [上游 README](README.upstream.md)、`docs/Deployment_all.md` 和 `main/xiaozhi-server/docker-compose_all.yml` 是来源资料；其中的预构建镜像、全局容器名和清理步骤不是本项目本机运行流程。
