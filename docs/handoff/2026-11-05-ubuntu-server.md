# 换机接续：Ubuntu 测试与部署

记录日期：2026-11-05。用户准备换一台开发电脑，并已决定将后续测试及部署环境改为一台 Ubuntu 服务器。下一会话从服务器接入与部署边界确认继续。

## 在新电脑取得内容

```bash
git clone --branch docs/ai-market-integration-questionnaire https://github.com/yanghan24/xiaozhi.git xiaozhi
cd xiaozhi
git config --local pull.ff only
git config --local push.default simple
git config --local core.logAllRefUpdates true
git status --short --branch
```

此分支汇合了已推送的 `feat/3-upstream-manager-baseline`、接口问卷，以及当时最新 `origin/main` 的团队协作文档。汇合用于换机接续，不等于 PR #9 已合并到主线；后续交付继续遵循 [CONTRIBUTING.md](../../CONTRIBUTING.md) 的任务分支和管理员合并流程。

新会话可以直接使用以下指令：

> 请先阅读 AGENTS.md、CONTRIBUTING.md 和 docs/handoff/2026-11-05-ubuntu-server.md，接着处理将测试及后续部署迁到 Ubuntu 服务器的工作。先核对 GitHub 上的最新任务和 PR 状态，再确认服务器接入方式及部署边界。

## 已有成果与权威资料

| 内容 | 阅读入口 |
| --- | --- |
| 仓库工作规则 | [AGENTS.md](../../AGENTS.md)、[CONTRIBUTING.md](../../CONTRIBUTING.md) |
| 领域语言和职责 | [GLOSSARY.md](../../GLOSSARY.md)、[ADR-0004](../adr/0004-extend-upstream-xiaozhi-server.md)、[ADR-0005](../adr/0005-backend-owns-device-management.md) |
| 产品需求及外部依赖 | [需求记录](../planning/voice-backend-discovery.md) |
| 集市负责人待答问卷 | [接口确认问卷](../../to-questionnaire-ai-market-integration.md)；当前是未填写的问卷，不能当作已确认接口契约 |
| 已实现的管理端基线 | [PR #9](https://github.com/yanghan24/xiaozhi/pull/9)，本次查询时为 OPEN，尚未合并 |
| 上游来源和导入范围 | [源码来源记录](../development/upstream-baseline.md) |
| 运行与检查方法 | [现有本机运行说明](../development/manager-baseline.md) |
| 实际构建、浏览器验收、测试与审阅 | [#3 验证报告](../verification/issue-3-manager-baseline.md)，含源码提交、镜像标识、截图和测试结果 |
| 当前规格与后续任务 | [#2](https://github.com/yanghan24/xiaozhi/issues/2)、[#3](https://github.com/yanghan24/xiaozhi/issues/3)、[#4](https://github.com/yanghan24/xiaozhi/issues/4)、[#5](https://github.com/yanghan24/xiaozhi/issues/5)、[#6](https://github.com/yanghan24/xiaozhi/issues/6) |

已有管理端验证是在 Fedora 开发电脑完成的。它可以作为迁移起点，Ubuntu 服务器上的运行和验收尚未执行。语音服务目前只有导入的源码；设备激活、绑定、WebSocket、真机对话、集市接入及最终并发目标仍待验证，具体边界以原规格和验证报告为准。

## 已确认的环境变更

用户已确定：后续测试及部署改到 Ubuntu 服务器，后续开发操作从另一台电脑继续。

尚未确认：服务器地址或 SSH 别名、登录账号和端口、服务器是否可访问、测试与正式环境是否同机、内网/公网访问方式、域名、音箱所在网络，以及是否需要迁移旧测试数据。Ubuntu 版本、CPU 架构、资源余量、Docker 和现有服务应在获得访问入口后实际检查。

此前提出的“开发电脑编辑代码，Ubuntu 运行服务”“测试与正式环境分开配置和数据卷”“Ubuntu 从新测试数据库开始”均为建议，尚未得到用户逐项确认。

这次选择改变原规格 #2 的 Fedora 本机测试环境约定，不改变 ADR-0004 的上游选型或 ADR-0005 的设备管理职责。规格及 #4–#6 的环境、依赖和验收位置尚未修订；取得部署信息后在现有任务体系中更新，保留已经完成的 Fedora 验证证据。

## 下一会话按顺序推进

1. 阅读工作规则并检查当前工作区、远端分支、PR #9 和相关 Issue 状态。完成条件：明确哪些变更已进入主线，避免重复实现 #3。
2. 获取用户希望使用的 SSH 连接入口和服务器用途，实际核查系统、架构、磁盘、内存、已有服务及网络。完成条件：能判断服务器是否具备运行条件，并明确管理人员和音箱从哪里访问。
3. 确定测试与正式环境边界、访问入口及数据安排，修订现有规格与任务依赖。完成条件：Ubuntu 管理端部署与验证有可执行的任务和验收条件。
4. 在 Ubuntu 部署已确定版本，重新验证管理登录、错误凭据拒绝、配置保存回读及重启恢复，再继续语音服务与真机任务。现有 `compose.yaml` 只发布回环地址；远程管理和音箱接入需按确定的网络方案适配。

## Git 能带走的内容与本地状态

本分支保存源码、构建和部署定义、领域文档、接口问卷、接续记录及脱敏验证证据。`.env.example` 和初始化脚本可用于生成新环境配置。

真实 `.env`、测试账号凭据、`.runtime/` 原始日志、Docker 镜像、运行容器和数据库卷属于旧电脑的本地状态，没有提交到 Git。已有镜像可按源码和固定依赖重建；如需保留旧测试账号和配置，应单独迁移数据库及匹配的配置。仅克隆仓库不会恢复这些运行数据。旧电脑的工作目录或 `/tmp` 文件不是后续流程的依赖。

## 建议使用的技能

- `ask-matt`：需要重新判断当前工作应进入哪一步时使用。
- `grill-with-docs`：确认 Ubuntu 部署范围、访问方式及测试/正式环境边界。
- `implement`：执行确定后的 Ubuntu 部署和验证任务，随后继续语音服务任务。
- `wizard`：只在云控制台、凭据开通等必须由用户完成的步骤确实出现时使用。

以上技能是建议入口；工作事实、验收要求和仓库规则以所链接的文件、Issue 和 PR 为准。
