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

## 独立访问

生产访问方式不在仓库文档中记录；此处仅保留历史操作结果。

```sh
ssh -F /run/xiaozhi-access/ssh_config xiaozhi-data104
生产访问方式不在仓库文档中记录；此处仅保留历史操作结果。
```

114 登录不属于日常开发启动步骤。私有 access 目录保存主机指纹、独立配置、认证材料及详细说明，不上传 Git。

## 环境检查

容器内执行 `python deploy/local-zhou/check-dev.py`，检查网页、API、前端代理、语音 HTTP、104 数据库连接与认证拒绝；不会调用 ASR、模型、MCP 或 TTS。实际初始化和重启验证结果见[本次交付记录](development-20261116.md)。
