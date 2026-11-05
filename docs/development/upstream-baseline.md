# 上游源码基线

- 来源：<https://github.com/xinnan-tech/xiaozhi-esp32-server>
- 完整提交：`c8e60d1325650c8a31d00e36d8528f8bb74aeb11`
- 对应任务：[#3](https://github.com/yanghan24/xiaozhi/issues/3)
- 许可证：根目录 [LICENSE](../../LICENSE)，保留上游 MIT 许可、版权和源码声明。

采用该候选提交，是因为已有需求调查以此版本为依据，而且它包含可直接复用的管理端、设备协议和既有测试。实际构建及管理端验证结果记录在 [验证报告](../verification/issue-3-manager-baseline.md)。硬件资料未齐，固件兼容性仍待真机确认；这不是已通过真机验收的发布版本。

导入使用该提交的 Git archive，仅导入受版本管理的内容，不复制参考工作目录的运行数据或凭据。范围包括 `main/manager-api`、`main/manager-web`、`main/xiaozhi-server`、`main/README*`、`docs/`、MIT 许可和上游三个 Dockerfile；上游根 README 保存为 `README.upstream.md`。移动端、数字人、上游 CI 和自动安装脚本未导入，本任务没有这些运行依赖。本项目原有领域文档、协作配置和 GitHub ruleset 均保留。

本项目新增 `deploy/manager/` 和根 `compose.yaml`，从上述源码分别构建管理 API 和页面，使用独立 MySQL、Redis 和上传文件卷。上游默认将 Nginx 和 Java 放在同一容器中，本地验证拆成两个进程容器，便于分别观察退出状态和资源。构建采用 Java 21、Maven 3.9.9、Node 20.19.0；Node 20 与该提交的前端 CI 一致（上游 Dockerfile 仍使用 Node 18）。

镜像引用固定版本及摘要。Java 依赖沿用上游 POM，前端使用已导入的 package-lock 和 `npm ci`。项目镜像使用 `SOURCE_REVISION` 标签记录本项目源码版本，同时记录上游提交；实际产物的 image ID 见验证报告。依赖仓库的可用性仍是首次构建的外部条件，记录镜像摘要不代表第三方依赖都可离线获取。

保留的 Python 语音服务源码尚未在 #3 中构建或执行。语音接入、激活绑定及 WebSocket 由 #4 验证；全套服务重启复现由 #5 验证；真实音箱问答由 #6 验证。当前使用上游本地测试账号，并不实现集市 OIDC、规范通权限计费或最终客户绑定流程。
