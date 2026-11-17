# 独立开发环境

本机 Dev Container 运行前端、Java API、Python 语音服务；104 仅承载小智独立开发 MySQL、Redis。114 是另一套部署环境，不参与本地开发；不修改 AI 集市资源。本人的新增配置集中在 `deploy/local-zhou/`，不覆盖同事的 `deploy/local/`。

## 目录与隔离

- 源码：`/home/timyai/dev/projects/hncig/xiaozhi`，挂载到容器 `/workspace`。
- 开发容器：`xiaozhi-dev`，镜像 `xiaozhi-dev:local`，用户 `dev`（UID 1000）。
- 私有配置：`/home/timyai/dev/runtime/xiaozhi/config/dev.env`，权限 600。
生产访问方式不在仓库文档中记录；此处仅保留历史操作结果。
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

当前方案与检查见[原版恢复实施记录](../../docs/development/upstream-restoration-progress.md)和[第一批交付](../../docs/development/upstream-restoration-batch1-delivery.md)。原版调用链的本地协议联调为 `check-upstream-voice.py`：真实 API 配置、WebSocket/Opus 与原版提供者适配器连接本地 ASR/LLM/TTS 替身，临时数据用完清理，不调用集市模型。不同使用默认角色的数据库/浏览器联调顺序运行，避免同时更改同一开发默认项。回退须恢复配套服务端、固件与私有配置；保留新增表和加密文件，不能单独关闭身份模式代替回退。

固件构建工具也位于同一容器 runtime 卷：ESP-IDF 5.5.5 在 /opt/xiaozhi-dev/tools/idf-v5.5.5，工具链在 tools/idf-tools，独立 Python 环境在 tools/idf-python。服务端 `bash deploy/local-zhou/build-speaker-firmware.sh` 使用 /opt/xiaozhi-dev/restoration-firmware 的显式同步源码副本（对应兄弟固件 worktree），以官方核心加 sdkconfig.defaults.account 的 P4 4.3 profile 和固定组件锁构建，最多 4 个 Ninja 任务；产物在 /opt/xiaozhi-dev/run/account-p4。编辑兄弟目录后先同步当前改动，不把旧副本当作最新源码。该命令不刷机、不访问 114。新的源码检查/依赖安装命令同样应显式把 TMPDIR 置于 /opt/xiaozhi-dev/run/tmp。

## 独立访问

生产访问方式不在仓库文档中记录；此处仅保留历史操作结果。

```sh
ssh -F /run/xiaozhi-access/ssh_config xiaozhi-data104
生产访问方式不在仓库文档中记录；此处仅保留历史操作结果。
```

114 登录不属于日常开发启动步骤。私有 access 目录保存主机指纹、独立配置、认证材料及详细说明，不上传 Git。

## 环境检查

容器内执行 `python deploy/local-zhou/check-dev.py`，检查网页、API、前端代理、语音 HTTP、104 数据库连接与认证拒绝；不会调用 ASR、模型、MCP 或 TTS。实际初始化和重启验证结果见[本次交付记录](development-20261116.md)。
