# 接续安排：本机测试与后续 Ubuntu 部署

首次记录：2026-11-05；当前安排更新于 2026-11-06。用户最新确认：测试先在开发电脑本机跑，需要服务器测试时再安排。当前从本机语音服务与设备协议任务 #4 继续，服务器 SSH、堡垒机和公网端口映射不作为本机测试的前置条件。后续 Ubuntu 部署与容量验收仍需单独执行。

## 在新电脑取得内容

```bash
git clone --branch docs/ai-market-integration-questionnaire https://github.com/yanghan24/xiaozhi.git xiaozhi
cd xiaozhi
git config --local pull.ff only
git config --local push.default simple
git config --local core.logAllRefUpdates true
git status --short --branch
```

此分支汇合了已推送的 `feat/3-upstream-manager-baseline`、接口问卷，以及当时最新 `origin/main` 的团队协作文档。2026-11-06 已核对管理端基线 PR #9 已合并、任务 #3 已关闭；本资料分支不等同于所有资料已进入主线。后续交付继续遵循 [CONTRIBUTING.md](../../CONTRIBUTING.md) 的任务分支和管理员合并流程。

新会话可以直接使用以下指令：

> 请先阅读 AGENTS.md、CONTRIBUTING.md 和 docs/handoff/2026-11-05-ubuntu-server.md，核对最新任务与本机环境，然后在本机推进 #4 的语音服务、激活绑定和 WebSocket 协议验证。需要服务器测试时再安排；缺少模型凭据或样机时如实记录覆盖边界。

## 已有成果与权威资料

| 内容 | 阅读入口 |
| --- | --- |
| 仓库工作规则 | [AGENTS.md](../../AGENTS.md)、[CONTRIBUTING.md](../../CONTRIBUTING.md) |
| 领域语言和职责 | [GLOSSARY.md](../../GLOSSARY.md)、[ADR-0004](../adr/0004-extend-upstream-xiaozhi-server.md)、[ADR-0005](../adr/0005-backend-owns-device-management.md) |
| 产品需求及外部依赖 | [需求记录](../planning/voice-backend-discovery.md) |
| 集市负责人待答问卷 | [接口确认问卷](../../to-questionnaire-ai-market-integration.md)；当前是未填写的问卷，不能当作已确认接口契约 |
| 已实现的管理端基线 | [PR #9](https://github.com/yanghan24/xiaozhi/pull/9) 已合并，[#3](https://github.com/yanghan24/xiaozhi/issues/3) 已关闭 |
| 上游来源和导入范围 | [源码来源记录](../development/upstream-baseline.md) |
| 运行与检查方法 | [现有本机运行说明](../development/manager-baseline.md) |
| 实际构建、浏览器验收、测试与审阅 | [#3 验证报告](../verification/issue-3-manager-baseline.md)，含源码提交、镜像标识、截图和测试结果 |
| 当前规格与后续任务 | [#2](https://github.com/yanghan24/xiaozhi/issues/2)、[#3](https://github.com/yanghan24/xiaozhi/issues/3)、[#4](https://github.com/yanghan24/xiaozhi/issues/4)、[#5](https://github.com/yanghan24/xiaozhi/issues/5)、[#6](https://github.com/yanghan24/xiaozhi/issues/6) |

已有管理端验证是在 Fedora 开发电脑完成的，当前本机也是 Fedora；Ubuntu 服务器上的运行和验收尚未执行。语音服务目前只有导入的源码；设备激活、绑定、WebSocket、真机对话、集市接入及最终并发目标仍待验证，具体边界以原规格和验证报告为准。

本机复核（2026-11-06）：现有管理端 Compose 项目仍在运行，其工作目录为 `.worktrees/3-upstream-manager-baseline`，该目录保留匹配的私有 `.env`。根目录没有 `.env`，不要在运行项目的数据卷上改用新生成的密码。对 `http://127.0.0.1:18002` 执行 `python3 scripts/check-manager.py` 已通过页面、API 初始化和匿名访问拒绝检查；本次未重复浏览器登录、配置保存或语音协议验收。

## 已确认的环境变更

2026-11-05 曾计划将后续测试及部署改到 Ubuntu 服务器；2026-11-06 用户进一步明确当前先在本机测试，需要服务器时再安排。当前本机测试优先的决定取代“先落实服务器入口再继续测试”的推进顺序，后续部署方向仍保留。

尚未确认：服务器地址或 SSH 别名、登录账号和端口、服务器是否可访问、测试与正式环境是否同机、内网/公网访问方式、域名、音箱所在网络，以及是否需要迁移旧测试数据。Ubuntu 版本、CPU 架构、资源余量、Docker 和现有服务应在获得访问入口后实际检查。

此前提出的“开发电脑编辑代码，Ubuntu 运行服务”“测试与正式环境分开配置和数据卷”“Ubuntu 从新测试数据库开始”均为建议，尚未得到用户逐项确认。

当前沿用原规格 #2 的本机测试方式，先推进 #4，再按依赖推进 #5/#6。不改变 ADR-0004 的上游选型或 ADR-0005 的设备管理职责。后续服务器部署及容量验收由 #11/#19 等任务跟踪；保留已经完成的 Fedora 验证证据，不能以本机结果替代服务器验收。

## 下一会话按顺序推进

1. 阅读工作规则并检查当前工作区、远端分支和相关 Issue 状态。管理端 #3 已完成，#4 的该项阻塞已解除。
2. 检查本机 Docker、资源、现有容器及私有配置，优先复用本项目的隔离测试环境；不要覆盖已有凭据或测试数据。管理端检查按[运行说明](../development/manager-baseline.md)执行。
3. 按 #4 在本机构建并运行真实语音服务，以测试客户端验证激活、绑定、配置读取、服务间认证及 WebSocket。模型初始化需要的配置与凭据单独核实，协议模拟不代替真机收音与播放验收。
4. 按依赖推进 #5 的全套重启和 #6 的真机验证；同时补齐 #11/#12 的集市外部输入。本机测试不因服务器访问条件未落实而等待；真实集市联调仍须满足其正式契约、凭据和网络要求。
5. 需要服务器测试时，再核实 Ubuntu 访问入口、资源、测试/正式数据边界与 HTTPS/WSS 网络，在固定版本上重跑适用验收并执行容量验证。现有 `compose.yaml` 只发布回环地址，届时按实际访问方案适配。

## Git 能带走的内容与本地状态

本分支保存源码、构建和部署定义、领域文档、接口问卷、接续记录及脱敏验证证据。`.env.example` 和初始化脚本可用于生成新环境配置。

真实 `.env`、测试账号凭据、`.runtime/` 原始日志、Docker 镜像、运行容器和数据库卷属于各电脑的本地状态，没有提交到 Git。开始本机测试时先检查已有 Compose 项目的工作目录和配置来源；已有容器或数据卷时，保留匹配的私有配置，不重新生成密码覆盖。已有镜像可按源码和固定依赖重建；如需迁移账号和配置，应单独迁移数据库及匹配的配置。仅克隆仓库不会恢复这些运行数据。

## 建议使用的技能

- `ask-matt`：需要重新判断当前工作应进入哪一步时使用。
- `grill-with-docs`：后续需要服务器时确认部署范围、访问方式及测试/正式环境边界。
- `implement`：当前先执行 #4 的本机语音服务与设备协议任务，再按依赖继续验证。
- `wizard`：只在云控制台、凭据开通等必须由用户完成的步骤确实出现时使用。

以上技能是建议入口；工作事实、验收要求和仓库规则以所链接的文件、Issue 和 PR 为准。
