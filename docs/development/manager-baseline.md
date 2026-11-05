# 本机管理端运行与验证

在仓库根目录执行下列命令。需要 x86_64 Docker Engine、Docker Compose、Python 3，以及可访问镜像和依赖仓库的网络。运行范围为管理页面、Java API、MySQL 和 Redis。

## 初始化和启动

```bash
python3 scripts/init-manager.py
python3 scripts/preflight-manager.py
docker compose --progress plain build api
docker compose --progress plain build web
docker compose up -d --wait --wait-timeout 360
python3 scripts/check-manager.py
```

`.env` 从 [.env.example](../../.env.example) 创建，权限为 `0600`，数据库密码随机生成。初始化脚本拒绝覆盖已有文件；凭据和运行数据不提交到 Git。已有数据库初始化后须保留同一份 `.env`，修改密码环境变量不会修改数据库中的密码。默认 Compose 项目名 `xiaozhi-baseline`，默认页面入口 <http://127.0.0.1:18002>。启动前可在 `.env` 修改 `MANAGER_PORT` 或项目名；检查脚本使用非默认端口时加 `--url http://127.0.0.1:端口`。

MySQL、Redis 和 Java API 均不向宿主机发布端口。Nginx 只监听本机回环地址。这份配置用于本机管理验证，音箱局域网接入在 #4／#6 中另行落实。

首次启动时 MySQL 创建专属空库及应用用户，Java 的上游 Liquibase 初始化表、默认参数及认证配置。初始化有时需要数分钟；`--wait` 有超时，随后 HTTP 检查验证页面、已初始化的公共配置及匿名参数访问拒绝。检查未包含浏览器登录或参数保存，它们由下述浏览器流程验证。

## 管理页面验收

首次打开管理页面，注册第一个测试账号；上游将第一个用户设为超级管理员。使用测试身份，不使用集市或其他系统的真实账号。

1. 使用有效验证码和错误密码登录，确认被拒绝。
2. 使用正确密码和验证码登录。
3. 进入「参数管理」，新增一个 `baseline.acceptance.*` 测试参数。
4. 刷新整个页面，按参数编码查询，确认读回同一值。

可重复执行浏览器检查（需要宿主机 Node、Chrome；应用构建仍在容器内）：

```bash
npm --prefix scripts/manager-browser ci --ignore-scripts --no-audit --no-fund
node scripts/manager-browser/check.mjs --register
```

第一次使用 `--register`，后续执行去掉此参数。脚本生成并保留专用测试账号凭据在 `.runtime/manager-check/credentials.json`，按上游真实页面注册和登录。每次在终端提示时查看 `.runtime/manager-check/captcha.png`，输入图片中的 5 个字符；脚本不关闭、绕过或模拟验证码。每个提示最长等待 10 分钟（验证码本身 5 分钟过期，过期需刷新后重试），页面和网络步骤也有超时。Chrome 不在默认路径时设置 `CHROME_PATH`，非默认端口设置 `MANAGER_URL`。失败返回非零退出码，不会产生通过报告。

成功记录写入 `.runtime/manager-check/result.json`，截图为 `configuration-readback.png`。记录不含密码、token、数据库凭据或服务密钥。每轮新增独立测试参数便于关联证据；仅在本项目测试环境运行。

## 状态与排障

```bash
docker compose ps
docker compose logs --tail 100 api
docker compose logs --tail 100 mysql
docker compose logs --tail 100 web
docker compose stats --no-stream
```

API 启动失败时先检查数据库健康状态、Liquibase 错误及 `.env` 是否与已有卷一致。页面正常而 API 检查失败时，检查 Java 日志和 Nginx 代理错误。镜像或依赖下载失败须恢复网络后重试；不要将下载失败当作构建通过。日志可能包含运行配置，只保存脱敏片段到报告。

已提交版本构建时，把 `.env` 的 `BUILD_TAG` 和 `SOURCE_REVISION` 都设为实际 `git rev-parse HEAD` 输出。运行以下命令记录产物和资源：

```bash
git rev-parse HEAD
docker compose images
docker compose stats --no-stream
```

本机约 8 GiB 内存，先顺序构建 API 和页面，再启动服务。首次构建至少预留约 8 GiB 磁盘。资源检查仅表示启动条件，实际占用见验证报告，不作为生产容量或 100 路并发结论。

## 上游测试

页面构建过程运行上游的 contract、unit 和 snapshot 测试。Java 全套测试连接专属测试数据库及 Redis；先停止管理环境以腾出内存，测试结束后恢复：

```bash
docker compose stop
docker compose -p xiaozhi-baseline-tests -f compose.tests.yaml build api-tests
docker compose -p xiaozhi-baseline-tests -f compose.tests.yaml up --abort-on-container-exit --exit-code-from api-tests
docker compose -p xiaozhi-baseline-tests -f compose.tests.yaml down --volumes
docker compose start --wait --wait-timeout 360
```

测试设置 `CI=true`，并执行上游 `check-integration-tests.sh`，确保依赖 MySQL 和 Redis 的测试实际运行。测试项目名固定为 `xiaozhi-baseline-tests`，不能改为管理环境的项目名；清理仅针对这套测试资源。Python 语音服务测试在 #4 接入时执行。

## 停止、恢复和清理

正常停止保留容器及数据：`docker compose stop`；恢复：`docker compose start --wait --wait-timeout 360`。删除本项目容器和网络、保留持久化数据：`docker compose down`；随后重新 `up` 会使用已有数据卷。

需要从全新测试数据重新开始时，先确认当前目录及 `.env` 的项目名，再执行 `docker compose down --volumes`。此操作删除该 Compose 项目的数据库、缓存及上传文件卷，不可用 Git 恢复；不要用于需要保留的数据。`.env` 和 `.runtime` 不由 Compose 清理。新数据库重新注册前，将旧 `.runtime/manager-check/credentials.json` 移到备份位置，以便生成新测试身份。

上游 `main/xiaozhi-server/docker-compose_all.yml` 的固定全局容器名、镜像和清理步骤不适用于本环境。其他项目容器与数据无需停止或修改。
