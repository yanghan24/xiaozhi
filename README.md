# 小智智能音箱

基于上游小智服务端和音箱固件开发，接入元筑 AI 集市。管理后台仅供管理员使用，用户通过音箱扫码完成集市授权，由小智维护用户与设备的绑定关系；语音对话沿用上游运行链路。

## 项目结构与职责

| 部分 | 源码位置 | 职责 |
| --- | --- | --- |
| 管理前端 | `main/manager-web/` | 用户、设备、固件、智能体、模型、MCP 及系统设置的管理页面 |
| Java 管理 API | `main/manager-api/` | 管理配置、集市授权、用户设备关系、设备身份及运行配置下发 |
| Python 语音服务 | `main/xiaozhi-server/` | 音箱语音连接、ASR、模型对话、TTS、意图、记忆和工具调用 |
| 音箱固件 | 固件源码分支，单独工作目录维护 | 配网、扫码显示、设备身份、独立心跳、语音交互、设备工具及 OTA |

服务端与固件各自维护源码；服务端工作目录不包含完整固件工程。架构边界见[领域词汇](GLOSSARY.md)和[架构决策记录](docs/adr/)，上游来源见[源码导入记录](docs/development/upstream-baseline.md)。

## 当前实现

- **用户与设备**：集市设备授权登录、用户资料及组织和积分信息、设备绑定与解绑、独立认证心跳。设备在线与语音会话在线分开判断。
- **智能体**：保留完整配置编辑，支持空白新建、复制和管理员选择全局默认智能体。默认角色切换在当前回答完成后的下一轮生效，并清空旧角色上下文。
- **语音与模型**：沿用上游提供者和对话处理机制。模型配置分为“AI 集市”和“小智系统”，集市模型按协议、能力及已验证条件筛选，由管理员选择，不全量导入远端模型。集市 ASR 当前仅适配 `spark_zh_iat_new / xfyun_asr_v2`；TTS、VAD、RAG 保留小智系统配置。
- **身份与调用凭据**：设备扫码授权、后台模型目录访问和用户模型调用分别管理。登录绑定不等待用户模型 Key 领取；使用集市模型时按设备归属解析用户应用专用 Key。
- **MCP 与固件**：MCP 服务目录与智能体关联管理，工具发现和调用沿用上游机制。固件管理与升级使用原版固件目录及 OTA 流程，设备 MCP 工具保留官方机制。

这些是当前开发实现的范围。开发检查、真实服务联调和真机验收各有边界，不据此认定已部署到生产或完成全部实机验收。历史文档中的阶段安排及验证结论以其记录日期和源码版本为准。

## 当前文档入口

| 内容 | 入口 |
| --- | --- |
| 协作、开发环境与验证原则 | [团队协作与安全回退](CONTRIBUTING.md) |
| 概念与职责 | [领域词汇](GLOSSARY.md)、[架构决策记录](docs/adr/) |
| 生产部署 | [当前部署入口、公网地址与运行方式](docs/deployment/current.md) |
| 扫码授权、用户及设备关系 | [音箱身份接入实施记录](docs/development/speaker-phase1-implementation.md) |
| 上游链路恢复与第一批固件 | [恢复实施记录](docs/development/upstream-restoration-progress.md)、[第一批交付与验收边界](docs/development/upstream-restoration-batch1-delivery.md) |
| 集市模型目录、适配与用户 Key | [模型接入交付](docs/development/ai-market-model-integration-delivery.md) |
| 智能体创建、复制与默认排序 | [智能体管理交付](docs/development/agent-drafts-and-copy-delivery-2026-11-17.md) |
| 管理界面布局 | [管理页面 Tab](docs/development/management-tab-layout-delivery-2026-11-17.md)、[弹窗布局](docs/development/management-dialog-layout-delivery-2026-11-17.md) |
| 模型兼容性与语音服务地址 | [一致性改造交付](docs/development/model-and-service-consistency-delivery-2026-11-17.md) |

开发者使用各自适用的环境说明。个人容器、数据库地址、目录和启动步骤放在对应的开发环境目录，不作为团队统一配置。生产操作前按部署入口核对当前版本和证据；生产登录方式及认证资料不在仓库记录。

## 历史资料

以下资料用于需求与实现溯源，不作为当前完整接口契约、开发启动步骤或生产部署依据。

- **早期需求和任务**：[需求记录](docs/planning/voice-backend-discovery.md)、[产品规则](docs/planning/product-rules-2026-11-09.md)、[MCP 知识库范围](docs/planning/mcp-knowledge-scope-2026-11-10.md)、[任务索引](docs/planning/task-spec-index.md)；[任务 #3](https://github.com/yanghan24/xiaozhi/issues/3)、[任务 #4](https://github.com/yanghan24/xiaozhi/issues/4)、[父规格 #2](https://github.com/yanghan24/xiaozhi/issues/2)。
- **早期开发基线**：[管理端](docs/development/manager-baseline.md)、[语音协议](docs/development/voice-baseline.md)、[2026-11-06 集市契约核对](docs/development/ai-market-contract-review-2026-11-06.md)、[规范通与 TTS 资料](docs/development/ai-market-api.md)。
- **2026-11-06 部署与验收**：[独立测试部署](docs/deployment/ubuntu-isolated-test.md)、[A 层验收](docs/verification/issue-5-acceptance.md)、[管理端检查](docs/verification/issue-3-manager-baseline.md)、[语音协议检查](docs/verification/issue-4-voice-protocol.md)。
- **2026-11-10 身份与固件记录**：[集市身份验收](docs/verification/2026-11-10-identity-acceptance-summary.md)、[注销通知](docs/handoff/2026-11-10-market-logout-notification.md)、[P4 固件交接](docs/handoff/2026-11-10-device-firmware.md)、[UI 恢复](docs/verification/2026-11-10-p4-ui-recovery.md)。
- **其他历史记录与调研**：[设备身份恢复](docs/development/device-identity-recovery.md)、[2026-11-11 身份保留验证](docs/verification/2026-11-11-device-identity-retention.md)、[服务器交接](docs/handoff/2026-11-05-ubuntu-server.md)、[文档核查](docs/planning/document-sync-audit-2026-11-06.md)、[日程与环境需求](docs/planning/reminders-weather-discovery-2026-11-10.md)、[天气调研](docs/development/weather-location-replacement-research.md)。

根目录 `compose.yaml` 保留为独立测试基线。`docs/Deployment_all.md` 和 `main/xiaozhi-server/docker-compose_all.yml` 是来源资料，其中的预构建镜像、全局容器名和清理步骤不能直接作为当前开发或生产操作流程。
