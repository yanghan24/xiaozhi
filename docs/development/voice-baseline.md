# 本机语音服务与设备协议验证

本流程承接[管理端本机环境](manager-baseline.md)，运行导入源码中的 Python 小智语音服务。管理 API、MySQL、Redis 均为真实服务；客户端模拟设备的 HTTP／WebSocket 协议。当前不依赖服务器、集市专用接口或样机。

## 复用已有环境

在本任务 worktree 根目录执行。下例复用任务 #3 的 Compose 项目及原始私有 `.env`，不会重新初始化数据库密码：

```bash
export MANAGER_ENV=../3-upstream-manager-baseline/.env
export COMPOSE_PROJECT_NAME=xiaozhi-baseline
export COMPOSE_FILE=compose.yaml:compose.voice.yaml
docker compose --env-file "$MANAGER_ENV" ps
python3 scripts/check-manager.py
```

如果现有环境在其他目录，将 `MANAGER_ENV` 改为它的 `.env` 路径，并核对项目名。全新环境先按管理端说明初始化、启动并完成测试账号注册。保留已有数据卷和配套凭据；本流程仅新增 `voice` 容器。

## 登录及初始化

复用现有测试账号，经过真实管理页面和验证码登录，token 只保存在本机私有文件：

```bash
npm --prefix scripts/manager-browser ci --ignore-scripts --no-audit --no-fund
node scripts/manager-browser/voice-login.mjs \
  --credentials ../3-upstream-manager-baseline/.runtime/manager-check/credentials.json
```

按脚本提示查看 `.runtime/voice/captcha.png` 并输入验证码。需要宿主机 Chrome；可用 `CHROME_PATH` 指定位置。token 过期后重新登录；脚本不绕过认证。非默认管理端口用 `--url http://127.0.0.1:端口`，初始化脚本相应加 `--url http://127.0.0.1:端口/xiaozhi`。

```bash
python3 scripts/init-voice.py --configure
```

脚本通过管理 API 取得实际 `server.secret`，将服务认证配置写入 `.runtime/voice/data/.config.yaml`，检查设置写入同目录 `check.json`，均为 `0600`。`--configure` 将默认 ASR 改为上游远程模型 `ASR_OpenaiASR`、启用设备认证，并在 `.runtime/voice/manager-settings-before.json` 保存修改前的管理配置。仅对本项目测试环境执行。

## 构建和启动

```bash
docker compose --env-file "$MANAGER_ENV" --progress plain build voice
docker compose --env-file "$MANAGER_ENV" up -d --no-deps --wait --wait-timeout 120 voice
python3 scripts/check-voice.py
python3 scripts/init-voice.py --publish-urls
```

先启动语音服务，再发布设备 URL。上游管理 API 拒绝回环 WebSocket URL，并实际连接待保存地址进行校验。本流程发布 Compose 内部地址 `ws://voice:8000/xiaozhi/v1/` 和 `http://api:8002/xiaozhi/ota/`，用于容器内协议客户端。宿主机健康检查使用 `127.0.0.1:18000`；附加 HTTP 入口为 `127.0.0.1:18003`。局域网真机需要另外配置设备可达地址，在后续真机任务验证。

镜像保留 FFmpeg、libopus 和仓库内 Silero ONNX VAD 模型；不安装 Torch／本地 FunASR 推理栈。远程 ASR 支持 `openai`、`fun_server`，其他提供者需要各自的 SDK、模型及凭据。运行依赖直接版本见 `deploy/voice/requirements.txt`，实际完整依赖清单位于镜像 `/usr/local/share/xiaozhi-requirements.txt`。

已提交版本构建时，设置 `VOICE_SOURCE_REVISION` 和 `VOICE_BUILD_TAG` 为实际源码提交。语音镜像使用独立变量，避免继承旧管理镜像的 `SOURCE_REVISION`：

```bash
export VOICE_SOURCE_REVISION=$(git rev-parse HEAD)
export VOICE_BUILD_TAG="$VOICE_SOURCE_REVISION"
docker compose --env-file "$MANAGER_ENV" build voice
docker compose --env-file "$MANAGER_ENV" up -d --no-deps --wait --wait-timeout 120 voice
```

有未提交改动时保留默认 `working-tree` 标记，不用旧提交号标记新代码。管理镜像继续使用原 `.env` 的版本，构建和启动命令只选择 `voice`。

## 设备协议与配置恢复检查

上游推荐的浏览器工具在未导入的数字人模块中；现有 `performance_tester` 面向模型推理，不覆盖管理端激活绑定。因此新增最小 HTTP／WebSocket 客户端，使用上游的实际请求、激活码和认证方式：

```bash
docker compose --env-file "$MANAGER_ENV" exec -T voice \
  python /checks/check-voice.py --settings /app/data/check.json \
  --output /app/data/protocol-result.json
docker compose --env-file "$MANAGER_ENV" exec -T voice \
  cat /app/data/protocol-result.json > .runtime/voice/protocol-result.json
```

每轮生成独立 MAC、Client-Id 和测试智能体，验证未注册／待绑定状态、激活码绑定、当前测试账号下的设备回读、绑定后的 OTA／智能体配置、错误 WebSocket token 拒绝、真实 `hello`、同一会话的提供者初始化完成及三次 WebSocket 控制帧 Ping/Pong。检查结束经管理 API 删除本轮智能体和设备，保留本轮身份和脱敏结果用于追踪。当前上游默认关闭应用层 JSON 心跳，本检查使用 WebSocket 标准控制帧。

客户端只在全部步骤通过且自己的测试数据清理成功后返回 `0`；失败返回 `1`，结果为 `status: failed`。每次执行均覆盖指定结果文件，不根据旧结果判断通过。`scope: device_protocol` 才表示完整设备协议；不加 `--settings` 只检查 HTTP 存活，报告范围是 `http_health`。服务凭据错误会在配置读取处停止；设备 URL 与预期不一致也会失败，不能把上游备用地址当成可用配置。

`hello` 会早于后台提供者初始化返回。本项目在同一语音端口增加 `GET /xiaozhi/v1/ready/{session_id}`：使用该设备的 OTA token、Device-Id 和 Client-Id 校验会话归属，返回 `pending`、`ready` 或 `failed` 及失败阶段，不返回配置或异常详情。客户端最多等待 30 秒，初始化失败或超时会使完整检查失败；错误 token 返回 HTTP `401`，其他设备会话返回 `403`，连接结束后会话移除。正常设备的 `hello` 时序保留。

就绪表示提供者对象和本地组件初始化完成，推理凭据有效性及实际音频仍需各自验证。本机私有语音日志包含认证头，提取补充证据时仅保留必要信息。

可重复执行提供者配置错误／恢复回归，使用经管理 API 新增的独立 TTS 模型：

```bash
docker compose --env-file "$MANAGER_ENV" exec -T voice \
  python /checks/check-voice-providers.py
```

检查先把专属模型设为不受支持的类型，要求设备已收到 `hello` 后仍被判为初始化失败；再将同一模型修正为真实 Edge TTS，要求初始化及协议检查通过。每次设备检查清理自己的智能体和设备，最后删除该专属模型。结果保存在 `/app/data/provider-result.json`，不会修改默认 TTS 模型或调用模型推理。

以下检查在独立容器中给真实 `app.py` 提供缺失／错误配置，再恢复正确配置。已运行的语音服务继续保留自己的配置与端口：

```bash
docker compose --env-file "$MANAGER_ENV" run --rm --no-deps -T voice-config-check
docker compose --env-file "$MANAGER_ENV" run --rm --no-deps -T \
  --entrypoint cat voice-config-check /evidence/result.json \
  > .runtime/voice/startup-result.json
```

三个失败情形为缺少 `.config.yaml`、缺少 `manager-api.secret`、错误服务密钥；都必须非零退出并出现对应诊断。正确配置必须重新返回真实 HTTP 健康响应。检查服务使用临时 `/app/data` 和 `/app/tmp`，启动前拒绝覆盖现存 `.config.yaml`。私有日志位于 `.runtime/voice/startup-check`，脱敏结果为其 `result.json`。

## 提供者与凭据

| 提供者 | 初始化条件 | 真实调用条件与当前验证范围 |
| --- | --- | --- |
| `VAD_SileroVAD` | 真实 ONNX Runtime、仓库内 ONNX 模型 | 已验证载入；未验证麦克风音频判定 |
| `ASR_OpenaiASR` | `requests`、可写音频临时目录 | 需要远程 ASR 端点、模型名和有效 API key；初始化不会校验密钥，当前缺少有效 key，未执行识别 |
| `LLM_ChatGLMLLM`／`openai` | OpenAI SDK、上游模型配置 | 初始化允许占位 key 并记录警告；实际生成需要有效 key，当前未执行 |
| `TTS_EdgeTTS` | Edge TTS、FFmpeg、libopus、可写临时目录 | 无用户 API key，但需要可达服务；独立上游用例实际访问 Edge TTS 并通过，设备音频链路仍待验证 |
| `Memory_nomem`、`Intent_nointent` | 上游对应提供者 | 专用测试智能体关闭记忆和工具调用，聊天记录设为 `0` |

测试账号沿用上游本地账号，尚未实现集市 OIDC、规范通权限和计费。实际音频识别、模型生成、经设备协议传输的语音合成、麦克风输入及设备播放仍待后续凭据与真机验证。上游默认 TTS 用例仅断言输出文件存在，不验证语音内容或播放质量。

## 上游 Python 测试

```bash
docker compose --env-file "$MANAGER_ENV" --progress plain build voice-tests
docker compose --env-file "$MANAGER_ENV" run --rm --no-deps -T voice-tests
```

测试镜像具有独立公开配置和测试依赖，不挂载实际管理密钥。上游 `tests/conftest.py` 的默认模式替换部分提供者网络与管理配置加载；已安装的 Edge TTS SDK 仍会由一个测试用例发出真实合成请求，因此该用例需要可达 Bing 语音服务的网络。这套结果用于源码回归检查，不能代替上面连接真实 Java API、MySQL、Redis、Python 服务的协议检查；默认模式中的替代调用也不能作为模型实际调用证据。完整依赖清单位于测试镜像 `/usr/local/share/xiaozhi-test-requirements.txt`。

## 停止与恢复

```bash
docker compose --env-file "$MANAGER_ENV" stop voice
docker compose --env-file "$MANAGER_ENV" start --wait --wait-timeout 120 voice
docker compose --env-file "$MANAGER_ENV" ps
docker compose --env-file "$MANAGER_ENV" stats --no-stream
```

停止只针对 `voice`，保留管理端、数据库卷、配置及日志。`.runtime/voice` 中包含 token、服务密钥和上游打印的请求头，不能直接提交或转发日志。读取失败时先核对 `.config.yaml` 的管理 API 地址和密钥；镜像下载失败需恢复网络重试。内存限制为 640 MiB，只用于本机单连接协议检查，不代表生产容量。

默认 ASR、设备认证和设备 URL 是管理端持久配置，停止容器不会撤销这些修改；恢复原管理设置时参照初始化保存的私有备份，在上游模型配置和参数管理入口调整。不要删除原 MySQL／Redis 卷或重新生成已有 `.env`。实际测试结果、镜像标识、依赖版本和资源观测见[验证记录](../verification/issue-4-voice-protocol.md)。
