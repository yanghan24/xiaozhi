# 独立开发环境

本机 Dev Container 运行前端、Java API、Python 语音服务；104 仅承载小智独立开发 MySQL、Redis。114 是生产环境，不参与本地开发；不修改 AI 集市资源。本人的新增配置集中在 `deploy/local-zhou/`，不覆盖同事的 `deploy/local/`。

## 本机开发约定

本目录记录个人开发环境，不要求其他同事采用相同容器、数据库地址或目录。在这台开发机器上操作本项目时，先阅读并遵守本节及下文步骤：

- 前端、Java API、Python 语音服务、Git、构建、依赖安装和开发检查全部在既有 `xiaozhi-dev` 容器内执行；宿主机不安装开发工具或依赖。未经明确授权不创建或重建容器。
- 依赖、缓存和生成文件使用 `xiaozhi-dev-*` 命名卷；容器临时文件和日志位于 `/opt/xiaozhi-dev`，不放入源码挂载目录。
- MySQL 和 Redis 仅使用 104 上小智独立开发实例、账号、端口及数据；AI 集市只读参考，不修改或复用其基础设施。
- 私有配置和认证材料保存在 `/home/timyai/dev/runtime/xiaozhi/`，不得提交。生产登录账号、连接命令、跳转链路和私有访问文件路径不写入任何仓库文档。
- 114 为生产环境，独立于本地开发。开发不连接其数据库、不复制运行配置、不操作其服务；生产操作和真机验收另行明确授权。

当前个人工作区使用固定分支：服务端 `dev/local-zhou`，兄弟固件工作目录 `xiaozhi-firmware/` 使用 `dev/firmware-local-zhou`，两者是同一 Git 仓库的不同 worktree。按功能或阶段提交，不为每个小步骤创建分支。2026-11-18 已重建本地提交历史，未推送；当前本地历史与远端旧历史没有共同祖先，后续远端交付须另行安排，不直接套用基于旧 `origin/main` 的合并或变基步骤。

## 目录与隔离

- 源码：`/home/timyai/dev/projects/hncig/xiaozhi`，挂载到容器 `/workspace`。
- 开发容器：`xiaozhi-dev`，镜像 `xiaozhi-dev:local`，用户 `dev`（UID 1000）。
- 私有配置：`/home/timyai/dev/runtime/xiaozhi/config/dev.env`，权限 600。
- 开发服务器的访问材料单独保存在私有位置，不入库；生产连接方式不记录在本文件。
- 工具包含 Java 21、Maven 3.9.9、Node 20.19、Python 3.10、Git、OpenSSH、FFmpeg、Opus；工具安装在镜像中。
- `node_modules`、`dist`、Java `target`、Maven 仓库、npm/pip/Python 缓存、Python 虚拟环境、上传目录及语音运行目录均使用独立 `xiaozhi-dev-*` 命名卷。
- 容器临时资料、Supervisor 状态及日志使用 runtime 卷 `/opt/xiaozhi-dev`。宿主机只保留源码、必要配置和部署日志。

104 的独立 Compose 定义见 [data104.compose.yaml](data104.compose.yaml)。遵循服务器 `/home/snow/README.md`：运行配置放 `/home/snow/app/xiaozhi-dev/`，数据分别位于该目录的 `data/mysql/`、`data/redis/`，备份位于 `backups/`；部署和备份脚本放 `/home/snow/packages/xiaozhi-dev/`。MySQL 使用 `172.16.18.104:13306`、库和账号 `xiaozhi_dev`；Redis 使用 `172.16.18.104:16380`、独立认证和 DB 0。不使用原 PostgreSQL 或 Redis 16379，也不迁移 114 数据。服务器规则允许有理由的独立实例；本次按用户确认的开发隔离方案独立部署，避免变更 AI 集市共享组件，小智原生使用 MySQL。

## 工具与服务

VS Code 可使用 `.devcontainer/devcontainer.json` 打开。已有 `xiaozhi-dev` 时优先 Attach to Running Container，避免创建第二个实例。创建配置需要先有上述私有 `dev.env` 和独立 access 目录。用户明确授权后才可创建或重建容器。

宿主机仅用现有 Docker 管理容器。开发命令在容器中运行：

```sh
docker exec -it -u dev xiaozhi-dev bash
supervisorctl -c /workspace/.devcontainer/supervisord.conf status
```

工具检查用 `java -version`、`mvn -version`、`node --version`、`python --version` 和 `git --version`。首次使用 `sh .devcontainer/setup.sh` 安装锁文件前端依赖、轻量远程语音依赖并打包 Java；不安装 Torch 或本地 FunASR。pip 固定为 25.3，以避免基础镜像旧解析器的依赖解析失败。

新开发库首次初始化：启动 API，等待公开配置接口就绪后，在容器执行 `python deploy/local-zhou/init-voice.py`，再启动 voice。该脚本只允许本项目 104 开发库、无用户的初始状态，已有语音配置时拒绝覆盖；不创建管理员或配置真实模型凭据。首次管理员通过开发页面正常注册，模型/集市凭据需独立配置，不借用 114 的配置。

完成首次初始化后，runtime 卷的 `ready` 标记使容器启动时自动由 Supervisor 启动三个服务。Supervisor 从容器入口启动；不要在 docker exec 会话中再启动第二个 Supervisor。宿主机恢复命令为 `docker compose -f deploy/local-zhou/dev.compose.yaml up -d --no-build`；需要载入修改后的 dev.env 时，仅重建此开发容器，保留命名卷。

本机端口：前端 18001、API 18002、语音 WebSocket 18000、辅助 HTTP 18003，分别映射容器 8001、8002、8000、8003。前端请求通过 `/xiaozhi` 代理到同容器 Java API；开发数据库凭据从 `dev.env` 注入，不进入前端。

当前开发机页面 `http://172.16.111.196:18001/#/login`，本机也可用 `http://127.0.0.1:18001/#/login`。只有前端开发端口向局域网发布，API/语音映射绑定本机回环地址；本轮未配置真机或公网入口。前端首次编译需要等待，页面可用不代表模型问答已配置。

前端保留热更新；Java 修改后在容器打包，再重启 api；Python 修改后重启 voice：

```sh
mvn -B -ntp -f main/manager-api/pom.xml package -DskipTests
supervisorctl -c .devcontainer/supervisord.conf restart api
supervisorctl -c .devcontainer/supervisord.conf restart voice
```

Supervisor 日志位于 `/opt/xiaozhi-dev/logs/`，只读检查后再摘取脱敏错误。不要向报告复制原始日志或配置。停止服务用 supervisorctl stop；停止容器保留命名卷。不要删除数据库卷或重新生成已使用的密码。

## 第一阶段音箱接入

开发 API 可选加载 `/opt/xiaozhi-dev/speaker/speaker.properties`；默认缺失时不启用身份模式。当前独立开发环境已配置 `speaker.enabled=true`、开发专用 device Client ID 和独立加密文件，私有备份在宿主 runtime/xiaozhi/config/speaker。语音配置保存在原命名卷 data/.config.yaml；2026-11-17 恢复原版运行时后删除 product-voice 配置，身份接口通过原版 manager-api 的 server.secret 保护，不再使用独立语音服务 Key 或旧 v2 语音接口。管理员明确选择完整默认角色后才允许对话。不要复制 114 配置。

新入口为 `/#/speaker-agents`、`/#/speaker-users`、`/#/speaker-devices`。仍使用原运营管理员登录；先新增角色并点击“设为默认”才能开始对话。系统不自动选用旧模板，也不开放客户自助注册。

`check-speaker.py` 调用 `check-agent-management.py`：仅限 104 小智开发库，创建临时管理员、模拟客户、角色、设备及停用的 MCP 服务，验证无客户资源 Key 的完整原版配置下发、默认选择及页面。结束清理夹具并恢复默认角色，不重启 API、不请求集市授权或真实模型。浏览器工具、缓存和字体只放 runtime 卷。原版运行时、身份轮次和本地 MCP 协议替身的定向测试见实施记录；它们不代表真机或真实服务验收。

`speaker-simulator.py` 以管理员登记后保存的私有设备身份运行，模拟心跳和设备状态，最长 600 秒。`--begin` 才会显式发起一次真实集市设备授权；它输出手机授权地址和核对码，令牌与 Key 不输出。退出模拟器不会退出用户或解绑。请给模拟设备明确的非实机名称，不将模拟在线当作音箱验收。

```sh
python deploy/local-zhou/speaker-simulator.py \
  --identity-file /opt/xiaozhi-dev/speaker/development-device.json --begin --duration 600
```

当前方案与检查见[原版恢复实施记录](../../docs/archive/completed/voice/upstream-restoration-progress.md)和[第一批交付](../../docs/archive/completed/voice/upstream-restoration-batch1-delivery.md)。原版调用链的本地协议联调为 `check-upstream-voice.py`：真实 API 配置、WebSocket/Opus 与原版提供者适配器连接本地 ASR/LLM/TTS 替身，临时数据用完清理，不调用集市模型。不同使用默认角色的数据库/浏览器联调顺序运行，避免同时更改同一开发默认项。回退须恢复配套服务端、固件与私有配置；保留新增表和加密文件，不能单独关闭身份模式代替回退。

固件从小智官方 `v2.6.0` 的 P4X 4.3 寸变体构建。ESP-IDF 6.1 commit `f0114250e205e33a945e2f3e7ec40085c5a029a2` 与工具链位于容器 runtime 卷的 `tools/idf-v6.1-f011425/`。在 `xiaozhi-dev` 运行 `bash deploy/local-zhou/build-speaker-firmware.sh`，使用 `/opt/xiaozhi-dev/run/p4x-260-rebuild/product` 的同步源码副本及其组件锁构建中文集市版，最多 4 个 Ninja 任务。完整镜像 ZIP 在该副本的 `releases/`，应用镜像在 `build/xiaozhi.bin`。编辑兄弟固件目录后先同步源码，再执行构建。构建与实机刷写分别执行。工具临时文件使用 `/opt/xiaozhi-dev/run/tmp`。

## 开发服务器访问

本目录仅记录开发环境。开发服务器认证材料保存在私有位置，不入库。生产账号、命令、跳转链路及私有登录脚本入口不写入仓库文档。

## 环境检查

容器内执行 `python deploy/local-zhou/check-dev.py`，检查网页、API、前端代理、语音 HTTP、104 数据库连接与认证拒绝；不会调用 ASR、模型、MCP 或 TTS。实际初始化和重启验证结果见[本次交付记录](development-20261116.md)。


## AI 集市模型接入（第二阶段）

模型配置保留“小智系统”，新增“AI 集市”分组。每种用途保存一个适配配置，远端模型由协议、能力及已验证输入方式筛选，不逐个导入成本地配置。管理员选择并启用后，在完整智能体设置中选用对应的 AI 集市配置；不自动改变默认智能体。

Java 从同一私有 speaker.properties 读取目录应用设置：

```properties
market.catalog.client-id=<开发环境网站后端 Client ID>
market.catalog.client-secret-file=/opt/xiaozhi-dev/speaker/market-catalog.secret
```

Secret 文件为仅 dev 可读的 600 权限文件，宿主私有备份在 runtime/xiaozhi/config/speaker/；不在前端或 Git 配置 Secret。目录令牌仅用于 models:read。
设备客户端请求 credentials:manage；用户扫码授权后，实际使用集市模型时自动领取并加密保存用户应用专用 Key。个人 Key 不写入模型配置。原版配置请求携带语音会话，按设备归属注入；Java 摘要和标题按原会话的归属账号解析。

ASR 本期仅 spark_zh_iat_new / xfyun_asr_v2，使用已有 PCM/Opus 处理和原版提供者接口，单次不超过 60 秒。其他 ASR、向量/重排、复杂记忆不在本期；TTS、VAD、RAG 保留原配置。

限定验收入口为 deploy/local-zhou/check-market-models.py 和 main/manager-web/scripts/check-market-ui.cjs。前者仅接受 runtime 下、已明确授权的临时模拟设备夹具，临时切换适配和角色后恢复，不能作为日常启动步骤；--live-voice 才会执行真实模型对话，产生用户集市用量。浏览器工具及字体缓存继续使用已有 runtime 卷。真实设备验收仍按 CONTRIBUTING 的独立 Ubuntu 环境规则执行。

结果及提交见[模型接入交付](../../docs/archive/completed/models/ai-market-model-integration-delivery.md)。

## 语音服务地址与运维（2026-11-17）

设备连接地址统一由系统参数 `server.websocket` 管理，身份状态接口下发地址和短期票据；服务管理使用同一来源。不再使用私有 `speaker.voice-url` 属性。已有环境先将该旧属性的值迁移到参数并清理参数缓存，核对一致后移除旧属性；没有地址时应明确显示未配置，不能默认连接其他环境。开发模拟地址为 `ws://127.0.0.1:8000/xiaozhi/v1/`，真机需要设备可达的 WSS 地址；本改动不构成真机可达性验收。

后台运维使用独立 `/xiaozhi/management/` WebSocket 路径和既有 `server.secret` / Python `manager-api.secret` 服务间认证，不创建临时设备或音频会话。反向代理若只转发 `/xiaozhi/v1/`，需要在部署任务中另行配置运维路径；本次不修改任何部署环境。默认由设备地址的同源运维路径访问。确有内外网差异时，在私有 properties 设置显式映射，例如：

```properties
speaker.management-url-map={"wss://voice.example/xiaozhi/v1/":"ws://127.0.0.1:8000/xiaozhi/management/"}
```

映射仅决定后台运维目标，不改变设备连接地址；页面同时显示两者，运行状态标为未检测。管理请求不自动重试；重启返回的是服务端已接受请求，不能等同于重启后健康检查成功。代码更新须配套加载 Java 与 Python 版本；日常角色/模型切换仍使用原有配置版本机制，不必逐次点击更新配置。
