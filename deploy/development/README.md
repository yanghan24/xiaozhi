# 通用 Compose 开发环境

适用于使用普通 IDE 开发的同事。Compose 启动独立的管理端、MySQL 和 Redis，可按需加入 Python 语音服务。使用本机已安装的 Docker Compose。个人 Dev Container 从各自环境目录启动，生产操作使用[生产规范](../production/README.md)。

通用 Compose 默认启动基础模式，适合管理端、API 和语音链路开发。设备登记、集市扫码与授权、音箱用户和设备管理、全局默认智能体及 MCP 关联属于音箱产品模式。联调这些功能时，使用工作区适用的开发环境，在 Java 的私有配置中启用 `speaker.enabled=true`，准备开发专用的设备授权客户端和独立加密配置。空开发库先完成管理员注册，再启用产品模式。相关流程见[项目架构](../../README.md#ai-集市接入)。

以下命令均从仓库根目录执行。

## 首次启动管理端

```sh
python3 scripts/init-manager.py
docker compose --env-file deploy/development/.env -f deploy/development/compose.yaml up -d --build
docker compose --env-file deploy/development/.env -f deploy/development/compose.yaml ps
```

初始化脚本从 [`.env.example`](.env.example) 生成 `deploy/development/.env`，以 600 权限写入独立开发库的随机密码。打开默认地址 [http://127.0.0.1:18002](http://127.0.0.1:18002)，在本地页面注册并登录开发账号。管理端口使用 `MANAGER_PORT`，数据库和缓存通过 Compose 内部网络访问。

已有旧根目录 `.env` 时，使用下列命令中的 `--env-file .env` 形式代替默认配置文件参数。沿用原 `XIAOZHI_PROJECT_NAME` 和数据库凭据可以继续访问对应的开发数据卷。已有配置由初始化脚本保护。

```sh
docker compose --env-file .env -f deploy/development/compose.yaml ps
```

## 按需启动语音

1. 在本地管理页面配置此运行镜像支持的远程 ASR（`openai` 或 `fun_server`），开启设备鉴权。根据开发范围配置模型。
2. 以开发管理员登录。在私有 `.runtime/manager-check/credentials.json` 中准备本地开发管理员的 `username` 和 `password`，使用现有浏览器辅助脚本完成验证码登录。此步骤使用可用的 Chrome、Node 与 npm 工具环境。已在私有目录取得本机测试管理员 token 文件的同事可以直接用初始化脚本。
3. 使用 [`scripts/init-voice.py`](../../scripts/init-voice.py) 读取本机管理 API 并写入私有的 `.runtime/voice/data/.config.yaml`。再加载语音 Compose。

```sh
npm --prefix scripts/manager-browser ci --ignore-scripts --no-audit --no-fund
node scripts/manager-browser/voice-login.mjs --credentials .runtime/manager-check/credentials.json
python3 scripts/init-voice.py
docker compose --env-file deploy/development/.env \
  -f deploy/development/compose.yaml -f deploy/development/compose.voice.yaml up -d --build voice
```

如果调整了管理端口，登录脚本和初始化脚本分别通过 `--url` 使用实际本机地址，初始化脚本的 URL 以 `/xiaozhi` 结尾。语音 WebSocket 和辅助 HTTP 默认分别监听本机回环的 18000、18003，可用 `VOICE_WS_PORT` 和 `VOICE_HTTP_PORT` 调整。开发模拟的内部 URL 与生产及真机接入分别管理。

## 测试、日志与停止

```sh
# 独立项目中的管理 API 回归。
docker compose --env-file deploy/development/.env \
  -f deploy/development/compose.tests.yaml run --build --rm api-tests

# 语音测试镜像内运行 pytest。
docker compose --env-file deploy/development/.env \
  -f deploy/development/compose.yaml -f deploy/development/compose.voice.yaml \
  --profile checks run --build --rm --no-deps voice-tests

# 查看管理端日志。
docker compose --env-file deploy/development/.env -f deploy/development/compose.yaml logs -f api web

# 停止同一套开发环境。若启动时加了语音文件，停止时使用相同的文件组合。
docker compose --env-file deploy/development/.env -f deploy/development/compose.yaml stop
```

测试 Compose 使用独立项目名和数据卷。语音测试使用隔离的 pytest 配置。真实模型调用和真机检查按[开发与验证指南](../../docs/development/README.md)记录。
