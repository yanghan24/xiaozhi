# 换电脑测试与服务器部署交接

> 2026-11-10 设备续报：P4 样机已完成测试智能体绑定、真实语音及原首页恢复，见 [设备与固件交接](2026-11-10-device-firmware.md)和 [UI 恢复记录](../verification/2026-11-10-p4-ui-recovery.md)。旧基础镜像的设备/服务重启结果与当前恢复镜像分别记录；本轮服务器版本和逐轮关联仍需归档。下文保留 2026-11-06 的部署快照与范围。

> 当前入口（2026-11-06 同步复核）：Ubuntu 已部署并完成 #4/#5 验证，见[当前运行手册](../deployment/ubuntu-isolated-test.md)与[验收记录](../verification/issue-5-acceptance.md)。复用服务器现有配置、账号和数据卷；开发者可在本机构建、运行自动化测试和协议模拟作为开发预检；正式后端集成、回归、协议及实机验收仍在 Ubuntu 进行。以下早期基线结果保留原有测试范围，全新环境步骤不适用于重新初始化现有数据库。真实 OIDC 等公网映射，规范通真实测试暂缓，测试 TTS 使用 Edge TTS。


首次记录：2026-11-05；更新：2026-11-06。早期安排为将资料保存在任务分支供换机使用；当前已完成 Ubuntu 部署与复验，新环境从包含本次文档同步的主线取得完整资料。历史 Fedora 结果保留原有范围，真实设备仍需单独验收。

## 取得当前主线

本次文档同步 PR #23 合并后使用以下命令；合并前审阅使用该 PR 的任务分支。不要用旧资料分支代替当前主线。

```bash
git clone --branch main https://github.com/yanghan24/xiaozhi.git xiaozhi
cd xiaozhi
git config --local pull.ff only
git config --local push.default simple
git config --local core.logAllRefUpdates true
git status --short --branch
```

已有主线 checkout 且工作区干净时，使用 `git pull --ff-only` 获取更新。若仍在旧资料或其他任务分支，先按协作规则保存工作，再另建主线 checkout；不要把在旧分支执行 pull 当作同步主线。当前主线已包含管理端、语音服务及 Ubuntu 部署/验收脚本；本次文档同步补齐集市与硬件资料，无需重建旧开发机的 worktree 布局。

[PR #20](https://github.com/yanghan24/xiaozhi/pull/20) 与 [PR #22](https://github.com/yanghan24/xiaozhi/pull/22) 已合并；旧资料分支 `docs/ai-market-integration-questionnaire` 仅作历史追溯，不含后续完整部署验收更新。后续任务遵循 [CONTRIBUTING.md](../../CONTRIBUTING.md)，按最新远端主线创建独立任务分支。

## 已完成与资料入口

| 内容 | 当前事实和阅读入口 |
| --- | --- |
| 仓库规则与领域职责 | [AGENTS.md](../../AGENTS.md)、[CONTRIBUTING.md](../../CONTRIBUTING.md)、[GLOSSARY.md](../../GLOSSARY.md)、[ADR-0005](../adr/0005-backend-owns-device-management.md) |
| 管理端基线 | #3 / PR #9 已合并；[运行说明](../development/manager-baseline.md)、[实际验证记录](../verification/issue-3-manager-baseline.md) |
| 语音与设备协议基线 | #4 / PR #20 已合并，#5 / PR #22 的 Ubuntu 重启持久化复验已通过；[运行说明](../development/voice-baseline.md)、[验证报告](../verification/issue-4-voice-protocol.md)、[复审记录](../verification/issue-4-review.md) |
| 硬件接入资料 | [硬件负责人提供的核查稿](../../筑音设备接入资料_核查稿_2026-11-06.md)；用户的后续确认见下一节 |
| 集市接口及规范通 | [资料索引](../development/ai-market-api.md)、[新版原文](<../../元筑AI集市应用接入接口文档(1).md>)、[契约变更核对](../development/ai-market-contract-review-2026-11-06.md) |
| 语音合成调研 | [千问 TTS 官方资料调研](../development/qwen-tts-research.md)；集市实际代理接口和可用凭据仍待取得 |
| 产品与任务 | [需求记录](../planning/voice-backend-discovery.md)、[基线规格 #2](https://github.com/yanghan24/xiaozhi/issues/2)、[集市首版规格 #10](https://github.com/yanghan24/xiaozhi/issues/10)；当前状态和依赖以 GitHub 为准 |

#4 在真实管理 API、MySQL、Redis 和 Python 语音服务上完成两轮独立身份检查，每轮 19 项；覆盖激活、绑定、配置、认证、WebSocket 握手、会话初始化就绪和测试数据清理。错误配置及恢复检查通过；上游 Python 套件为 129 passed / 5 skipped。产物版本与验证限制以报告为准，本次资料汇总没有重新执行完整套件。

真实 ASR／LLM 调用仍缺有效配置和凭据；独立 Edge TTS 用例通过不代表设备音频传输、播放或音质已验证。服务器部署已另行验收；真机对话、集市 OIDC／规范通／计费和最终容量仍需各自验收。

## 用户新确认的硬件条件

2026-11-06，用户在核查稿之后明确确认：**固件版本仍在修改，设备可以修改 OTA 地址。** 这两项更新此前资料中的待确认状态；固件最终版本尚未锁定，首轮联调记录当时的可运行测试版本或构建标识即可。

硬件资料的用户提供板型为 `ESP32-P4-Touch-LCD-4.3`，官网匹配候选为微雪 `ESP32-P4-WIFI6-Touch-LCD-4.3`、主控 `ESP32-P4`、无线协处理器 `ESP32-C6-MINI-1`、32 MB Flash 和 32 MB PSRAM。继续沿用这些已有资料，不重复要求提供完整板型和芯片信息。实物对应关系、芯片 revision 和固件包适配在实际选择烧录包时核实，不作为仅修改现有固件 OTA 地址的额外资料前提。

设备自定义 OTA 地址用于取得激活信息、WebSocket 地址和设备认证信息；支持该流程的现有固件可先改地址接入。样机的位置、可用时间和现场播放确认人员尚未确定。动态二维码显示、实际分区、可用 OTA 应用镜像、升级回执和回滚仍未完成实测，分别由 #13／#18 继续落实。

## 新电脑或新服务器的隔离基线

开始前检查操作系统、CPU 架构、Docker Engine／Compose、磁盘和内存、现有容器与配置。历史基线覆盖 x86_64 Fedora，后续 Ubuntu 部署和复验已通过；当前统一在 Ubuntu 运行后端验证。构建需要访问镜像及依赖仓库，浏览器脚本另需 Node 和 Chrome。

1. **全新测试环境：** 按[管理端运行说明](../development/manager-baseline.md)生成当前根目录 `.env`，先做预检，再顺序构建 API 和页面、启动管理服务、检查 HTTP，并注册和登录测试账号。预检会检查端口是否空闲，应用启动后该端口已占用属于正常情况。同一 Docker 主机有其他实例时，首次启动前在 `.env` 选择独立的 `XIAOZHI_PROJECT_NAME` 和端口。
2. **已有环境：** 找到其 Compose 项目和匹配的私有 `.env`，复用原账号和数据；不要在已有数据库卷上重新生成密码。改变环境变量不会修改数据库中已经初始化的密码。
3. **接入语音：** 按[语音运行说明](../development/voice-baseline.md)，先登录获取管理 token，再执行 `init-voice.py --configure`，构建并启动 `voice`，然后发布设备 URL。新 clone 使用 `MANAGER_ENV="$PWD/.env"` 和 `.runtime/manager-check/credentials.json`；旧示例中的 `../3-upstream-manager-baseline/` 仅属于旧开发机目录布局。
4. **验证：** 使用带 `--settings /app/data/check.json` 的容器内客户端完成设备协议检查，再执行配置恢复检查和需要的回归。直接运行不带 settings 的 `check-voice.py` 仅验证 HTTP 存活。

只对新建或明确用于本项目的测试环境运行初始化。`init-voice.py --configure` 修改默认 ASR 和设备认证，`--publish-urls` 修改管理端设备地址；脚本先保存原值，停止容器不会撤销这些持久设置。

## 服务器上的管理登录

服务器可以先运行回环端口上的隔离服务；没有 Chrome 时，在另一台有 Chrome 的电脑通过 SSH 隧道完成真实管理页面注册和登录。以下将 `user@server` 替换为实际 SSH 目标；堡垒机访问按实际 SSH 配置使用 `ProxyJump` 或对应别名。电脑的 `28002` 端口须空闲。

在电脑保持一个隧道终端：

```bash
ssh -N -L 127.0.0.1:28002:127.0.0.1:18002 user@server
```

在电脑为该服务器准备的专用 clone 中执行，凭据须属于这台服务器的管理数据库：

```bash
npm --prefix scripts/manager-browser ci --ignore-scripts --no-audit --no-fund
MANAGER_URL=http://127.0.0.1:28002 node scripts/manager-browser/check.mjs --register
node scripts/manager-browser/voice-login.mjs --url http://127.0.0.1:28002 --credentials .runtime/manager-check/credentials.json
```

首次注册才加 `--register`；已有该服务器测试账号时复用其凭据，去掉该选项。按脚本提示查看本机验证码图片并输入；Chrome 不在默认位置时设置 `CHROME_PATH`。

服务器 clone 根目录先执行 `install -d -m 700 .runtime/voice`。将电脑本次登录生成的 `.runtime/voice/admin-token.json` 经安全文件传输放到服务器 clone 同一路径，例如在电脑执行（替换 SSH 目标和实际仓库路径）：

```bash
scp .runtime/voice/admin-token.json user@server:/srv/xiaozhi/.runtime/voice/admin-token.json
```

服务器再执行 `chmod 600 .runtime/voice/admin-token.json`，然后在服务器本机运行语音初始化和后续构建步骤。token 过期时重新登录并安全更新，然后在服务器重跑 `python3 scripts/init-voice.py`，刷新协议客户端 `data/check.json` 中保存的旧 token；无需重新加 `--configure`，自定义地址参数须沿用。它与账号密码均不写入 Git。迁移的是本次服务器登录 token，不可用另一套本地数据库的登录状态替代。

## 设备可达入口与服务器部署

当前 Compose 仅发布回环端口：管理入口 `18002`、语音 WebSocket `18000`、附加语音 HTTP `18003`。默认发布的 `http://api:8002/xiaozhi/ota/` 和 `ws://voice:8000/xiaozhi/v1/` 是容器内部地址。它们用于当前协议基线，局域网或公网音箱无法直接使用。

服务器部署时先核对 SSH／堡垒机访问、系统和资源、测试与正式数据边界、设备所在网络、可达域名或 IP、反向代理及端口映射。管理服务内部运行通过后，再配置设备实际可达的 OTA 与 WebSocket 入口，验证 HTTP／WebSocket 转发以及所选 HTTPS／WSS 配置。仅能通过堡垒机 SSH 登录不代表设备可达。

在局域网接入时，设备填写 Ubuntu 服务器的可达 OTA 地址，管理端下发同样可达的 WebSocket 地址；公网接入则使用实际部署的入口。使用 `init-voice.py --publish-urls` 并传入实际 `--ws-url`、`--ota-url`，同步发布地址和刷新检查设置；若已经在管理页面发布，仍须带实际地址参数重跑初始化，以更新 `data/check.json` 的预期 URL。实际 WebSocket 地址还须由管理 API 容器和协议客户端容器访问。

调整后重跑协议检查，再做 [#6 单设备真实语音及重启复验](https://github.com/yanghan24/xiaozhi/issues/6)。完整重启复现见 [#5](https://github.com/yanghan24/xiaozhi/issues/5)，集市环境与资源输入见 [#11](https://github.com/yanghan24/xiaozhi/issues/11)／[#12](https://github.com/yanghan24/xiaozhi/issues/12)，服务器容量见 [#19](https://github.com/yanghan24/xiaozhi/issues/19)。

## Git 能带走的内容与本地状态

本分支保存源码、构建和部署定义、接口与硬件资料、交接说明及脱敏验证证据。真实 `.env`、测试账号、管理 token、服务密钥、`.runtime/` 原始日志、Docker 镜像、容器及数据库卷均属于各主机本地状态。克隆后可重新构建和初始化；如需保留旧账号和配置，须单独迁移数据库及配套私有配置。

本次没有打包或上传这些私有运行数据。上游语音日志可能包含认证头，只传递脱敏证据。硬件原文引用的外部 `outputs/...` 确认函和排期表未随本次资料提供；候选设备固件包也不包含在本仓库。

## 新会话入口与建议技能

新电脑可直接使用以下提示：

> 请阅读 AGENTS.md、CONTRIBUTING.md 和 docs/handoff/2026-11-05-ubuntu-server.md。资料分支已包含完成本机验证的 #4 语音基线，先检查当前工作区、GitHub 任务和 Ubuntu 已有部署，再复用现有隔离环境执行适用检查；不要重新初始化现有数据库。硬件已确认能改 OTA 地址，固件仍在修改；真机测试记录当时版本。不要沿用旧 worktree 的私有路径或覆盖已有卷的密码。模型凭据、设备音频和服务器容量按真实结果验收。

- `ask-matt`：根据当前任务状态和新机器条件判断下一步。
- `implement`：按明确的现有任务及依赖继续实施；#4/#5 已验收，按当前任务依赖与运行手册继续，真实集市和设备验收仍待完成。
- `code-review`：对后续代码或部署改动按固定基线执行规范与规格复审。
