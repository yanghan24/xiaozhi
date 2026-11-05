# 小智智能音箱

基于上游小智服务端工程开发，接入元筑 AI 集市。当前阶段先建立可复现的管理端源码与运行环境，业务范围和职责以 [需求记录](docs/planning/voice-backend-discovery.md)、[领域词汇](GLOSSARY.md)及 [ADR](docs/adr/) 为准。

- [本机管理端运行与验证](docs/development/manager-baseline.md)
- [上游源码来源与导入范围](docs/development/upstream-baseline.md)
- [实际验证记录](docs/verification/issue-3-manager-baseline.md)
- [任务 #3](https://github.com/yanghan24/xiaozhi/issues/3)、[父规格 #2](https://github.com/yanghan24/xiaozhi/issues/2)
- [团队协作与回退](CONTRIBUTING.md)

本项目使用根目录 `compose.yaml` 启动独立测试环境。保留的 [上游 README](README.upstream.md)、`docs/Deployment_all.md` 和 `main/xiaozhi-server/docker-compose_all.yml` 是来源资料；其中的预构建镜像、全局容器名和清理步骤不是本项目本机运行流程。
