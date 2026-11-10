# Ubuntu 独立测试部署与复验

> 2026-11-10 设备续报：真实 P4 音箱通过现有公网 OTA/WSS 接入、现场问答及原首页恢复，见 [设备与固件交接](../handoff/2026-11-10-device-firmware.md)。本轮服务器版本、端口/防火墙/Wi-Fi 隔离及会话关联仍需配套归档；以下 2026-11-06 部署快照不表示本轮重新执行或覆盖了这些检查。

记录日期：2026-11-06，Asia/Hong_Kong。后续后台服务、回归测试与设备协议检查统一在 Ubuntu `192.168.168.114` 进行，本机用于编辑、SSH 和浏览器访问。小智已部署到 `/home/jt/xiaozhi-ubuntu-test`，管理端、内部设备协议及同设备跨服务重启检查已通过，详见 [Ubuntu 实际验收](../verification/2026-11-06-ubuntu-deployment.md)。设备网络入口及真机音频仍待验证。[Windows 本机复验](../verification/2026-11-06-windows-ubuntu.md)保留为历史结果。

当前环境复用服务器已有 `.env`、账号和数据卷。下文 clone、解压、初始化密码只用于全新环境；服务器 GitHub/Docker Registry 直连失败时使用已校验的[离线部署包](offline-bundle.md)。不要在当前数据库上重新初始化密码。

## 源码与模板边界

现有部署的源码来源为固定提交 `6e354edf292c3df14fd783c5dc7314a4402e3b74`。#4 已通过 [PR #20](https://github.com/yanghan24/xiaozhi/pull/20) 合并，主线对应提交为 `afc9ca9e03ce85bf6ebb5fb8943060ccb54a5349`，已包含 `compose.voice.yaml` 和语音检查脚本。本任务分支已合入该主线提交。

已用 Git 比对确认 `6e354ed` 与 `afc9ca9` 的 `main/`、`scripts/`、`compose.yaml`、`compose.voice.yaml` 和 `deploy/voice/` 内容完全一致。因此本轮保留已校验的镜像和数据库，重新执行服务重启及协议验收；没有把旧镜像改标为新构建。结果与 #5 逐项对应关系见 [A 层验收](../verification/issue-5-acceptance.md)。全新部署选择包含本任务文件的固定提交；下文固定 `6e354ed` 的流程用于复现现有历史部署，并需配套本任务的入口和离线覆盖文件。

本次准备的新文件为 [Compose 入口](../../compose.device-entry.yaml)、[Nginx 入口](../../deploy/device-entry/nginx.conf) 和 [单设备记录模板](../verification/device-integration-template.md)。它们不在 `6e354ed` 中；执行部署前需把审查后的两个入口文件复制到验证源码 clone 的同名路径，并记录文件 SHA-256。也可把模板保存在单独目录，显式传入 Compose 文件路径和 `DEVICE_ENTRY_NGINX_CONFIG` 的绝对路径。不要把服务器 `.env`、账号或运行目录从其他项目复制过来。

入口模板的本机准备检查：

| 检查 | 当前结果 |
| --- | --- |
| 将固定验证源码的 base/voice Compose 与新入口模板合并解析 | `config --quiet` 通过；显式设置 `DEVICE_BIND_IP=127.0.0.1` 和实际配置文件路径 |
| 固定 Nginx 镜像加载真实入口配置并执行 `nginx -t` | 通过；没有发布宿主机端口 |
| 真实 API/voice 入口转发、认证及 ready | 本机回环入口完整协议 19 项、外部 HTTP 7 项通过；包括 OTA、认证、ready 与管理路径隔离，结果见本机复验报告 |
| Ubuntu 启动、内部协议及同设备服务重启 | 已通过，见 Ubuntu 实际验收；设备网络及真机音频未执行 |

## 已核对的服务器状态

| 项目 | 2026-11-06 观察 |
| --- | --- |
| SSH | 本机可直连 `jt@192.168.168.114:22`；本轮连接不需要堡垒机跳转 |
| 系统 | Ubuntu 20.04.3，x86_64 |
| CPU、内存 | 8 vCPU；约 16010 MiB 总内存、13252 MiB 可用内存 |
| 磁盘 | 约 449 GiB 可用 |
| 容器工具 | Docker 26.1.3；Compose 2.27.1；`jt` 不在 docker 组，本轮通过既有 `sudo` 认证检查 |
| 已有业务 | 初次盘点为 JetLinks 3 个运行、3 个停止；按用户要求于 16:07（UTC+08:00）停止运行中的项目，现有 6 个相关容器全部停止 |
| JetLinks 业务端口 | 停机后 1883–1890、5060–5061、8800–8810、8848 均无 TCP 监听；SSH 22 保持监听 |
| 小智 | 已部署到 `/home/jt/xiaozhi-ubuntu-test`，独立项目 `xiaozhi-ubuntu-test`，5 个服务运行；使用新生成的私有配置和独立数据卷 |
| 小智端口 | `127.0.0.1:18002/18000/18003` 已使用；18080 设备入口尚未启用 |
| 设备及公网 | 设备网络可达性未确认；公网映射未开通 |

SSH 可达仅代表管理通道可用，不代表设备能访问服务器。资源快照仅用于独立测试启动条件，不表示生产容量或并发能力已验收。

已使用独立 Compose 项目 `xiaozhi-ubuntu-test`。管理页面监听 `127.0.0.1:18002`，语音直连监听 `127.0.0.1:18000` 和 `127.0.0.1:18003`；设备测试入口候选为 `192.168.168.114:18080`，当前未启用。用户已要求关闭 JetLinks；保留其容器、镜像、网络、数据目录和私有配置，后续小智操作保持 JetLinks 停机。

本次停止项目为 `jetlinks-restored-99a1e3b3`，顺序为 `jetlinks` 应用，再停止专用 `postgres` 和 `redis`。应用等待 30 秒后退出码为 137，`OOMKilled=false`；PostgreSQL 和 Redis 均退出码 0。三个容器的 ID、挂载目录及重启策略前后相同，`/home/jt/jetlinks-restored/data/` 保留。没有重启服务进行恢复测试；保留目录不等于完成数据恢复验证。[停机证据](../verification/evidence/2026-11-06-ubuntu/jetlinks-stop.json)记录完整结果。

这三个容器仍使用 `unless-stopped`，手动停止状态会跨 Docker 重启保留；旧项目 `jetlinks` 的三个容器本就停止，策略为 `no`。检查未发现名称或配置引用 JetLinks 的 systemd 单元；未修改服务器启动项。后续如获准恢复，应先核对端口与数据，再显式启动已保留项目，不能在小智部署流程中顺带启动它。

## 执行前复查

以下命令在服务器以 `jt` 执行，Docker 命令使用已有 `sudo` 权限，不新增 docker 组权限；普通初始化、HTTP 检查和配置文件仍由 `jt` 运行与持有。先核对目录所有者、Docker 使用权限以及这些端口和项目名仍未被使用，再创建独立目录。已有小智环境时转为复用配套 `.env`，不要重复初始化密码。

```bash
uname -m
cat /etc/os-release
free -m
df -h .
sudo docker version
sudo docker compose version
sudo docker compose ls --all
sudo docker ps -a --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'
sudo docker volume ls --filter label=com.docker.compose.project=xiaozhi-ubuntu-test
ss -ltnup
```

构建需要镜像仓库、Debian 软件源、Maven、npm 和 PyPI 出站访问。使用原有网络设施；下载失败须修复访问后重试，不能记为构建通过。不执行全机 Docker prune，不修改现有业务防火墙规则。

## 取得固定源码并初始化管理服务

下例使用当前账号家目录中的新目录，不假定 `/srv` 可写。目录已存在时先检查其状态，不能覆盖或删除。

```bash
git clone --branch docs/ai-market-integration-questionnaire \
  https://github.com/yanghan24/xiaozhi.git "$HOME/xiaozhi-ubuntu-test"
cd "$HOME/xiaozhi-ubuntu-test"
git checkout --detach 6e354edf292c3df14fd783c5dc7314a4402e3b74
git rev-parse HEAD
git status --short
git config --local pull.ff only
git config --local push.default simple
git config --local core.logAllRefUpdates true
test -f compose.voice.yaml
test -f scripts/check-voice.py
python3 scripts/init-manager.py
```

初始化生成本环境独有的随机数据库密码。随后只更新非秘密设置，保留刚生成的密码：

```bash
python3 - <<'PY'
from pathlib import Path
import subprocess
path = Path('.env')
revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
updates = {
    'XIAOZHI_PROJECT_NAME': 'xiaozhi-ubuntu-test',
    'MANAGER_PORT': '18002',
    'BUILD_TAG': revision,
    'SOURCE_REVISION': revision,
    'VOICE_BUILD_TAG': revision,
    'VOICE_SOURCE_REVISION': revision,
    'VOICE_WS_PORT': '18000',
    'VOICE_HTTP_PORT': '18003',
}
lines = path.read_text().splitlines()
seen = set()
for index, line in enumerate(lines):
    key = line.partition('=')[0]
    if key in updates:
        lines[index] = key + '=' + updates[key]
        seen.add(key)
lines.extend(key + '=' + value for key, value in updates.items() if key not in seen)
path.write_text('\n'.join(lines) + '\n')
path.chmod(0o600)
print('Updated project, ports and source labels; secrets were not displayed.')
PY
sudo python3 scripts/preflight-manager.py
sudo docker compose --progress plain build api
sudo docker compose --progress plain build web
sudo docker compose up -d --wait --wait-timeout 360
python3 scripts/check-manager.py
```

端口预检应在启动前执行；服务启动后端口已占用是预期行为。初始化 `.env` 和数据库后必须成对保留，修改 `.env` 密码不会修改已有数据库用户。

## 通过本机完成服务器管理页面复验

在具备 Chromium 浏览器（Chrome 或 Edge）的电脑建立 SSH 隧道，保持其运行：

```bash
ssh -N -L 127.0.0.1:28002:127.0.0.1:18002 jt@192.168.168.114
```

使用服务器专属的本机 clone/工作目录存放测试凭据，避免覆盖本机 Windows 测试账号。首次运行注册，已有服务器账号则复用其凭据并去掉 `--register`。Windows PowerShell 例子：

```powershell
$env:CHROME_PATH = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$env:MANAGER_URL = 'http://127.0.0.1:28002'
npm.cmd --prefix scripts/manager-browser ci --ignore-scripts --no-audit --no-fund
node scripts/manager-browser/check.mjs --register
node scripts/manager-browser/voice-login.mjs --url http://127.0.0.1:28002 --credentials .runtime/manager-check/credentials.json
```

本机已确认存在上述 Edge 路径；脚本变量仍名为 `CHROME_PATH`，可指向可用 Chromium 浏览器，其他电脑按实际安装路径设置。按提示读取真实验证码，验证错误密码拒绝、正确登录、参数保存及刷新回读。该流程生成的是**服务器数据库对应的**管理员 token；不能使用另一套本地数据库的登录 token。

服务器先创建私有目录：`install -d -m 700 .runtime/voice`。通过安全文件传输把本次 token 放到服务器 clone 的 `.runtime/voice/admin-token.json`，再执行 `chmod 600 .runtime/voice/admin-token.json`。原始凭据和 token 不加入 Git，也不输出到报告。

## 语音与内部协议基线

在服务器验证源码根目录定义便于复用的 Compose 命令；显式 `-f` 避免跨平台 `COMPOSE_FILE` 分隔符差异：

```bash
dc() {
  sudo docker compose --env-file "$PWD/.env" -f compose.yaml -f compose.voice.yaml "$@"
}
python3 scripts/init-voice.py --configure
dc --progress plain build voice
dc up -d --no-deps --wait --wait-timeout 120 voice
python3 scripts/check-voice.py
python3 scripts/init-voice.py --publish-urls
dc exec -T voice python /checks/check-voice.py \
  --settings /app/data/check.json --output /app/data/protocol-result.json
dc exec -T voice python /checks/check-voice-providers.py
dc run --rm --no-deps -T voice-config-check
```

完整检查必须带 `--settings`，本轮报告应为 `scope: device_protocol`、成功状态且测试数据清理通过。建议两轮使用不同输出文件名保存独立身份的结果。`check-voice.py` 不带 settings 仅为 HTTP 存活检查，`hello` 也不能代替同会话 ready 检查。

`--configure` 修改此测试库的默认 ASR 与设备认证；`--publish-urls` 修改设备地址。脚本保存原值到私有 `manager-settings-before.json`，停止容器不会撤销这些数据库设置。token 过期后重新登录并重跑初始化以更新 `check.json`；已使用自定义 URL 时继续传入相同 URL 参数。

上游回归按验证范围另行执行。前端构建含前端套件；Java API 构建使用 `-DskipTests`，不等于 Java 套件通过。完整命令见验证源码中的 `docs/development/manager-baseline.md` 和 `docs/development/voice-baseline.md`。Java 测试项目必须独立，测试清理仅限该测试项目；不对管理数据库执行删除卷操作。

## 准备设备入口

确认设备网络能路由到 `192.168.168.114`，并核对服务器防火墙及设备网段后再启用入口。当前公网映射未开通，不能把本节 LAN HTTP/WS 模板作为公网 HTTPS/WSS 部署。域名、证书和公网映射应另行配置并验收。

将审查后的 `compose.device-entry.yaml` 和 `deploy/device-entry/nginx.conf` 放入验证 clone 对应路径。在私有 `.env` 中添加下列非秘密设置（已有同名项时替换原项），由 Compose 读取，避免 `sudo` 丢弃 shell 环境变量：

```dotenv
DEVICE_BIND_IP=192.168.168.114
DEVICE_PORT=18080
```

若模板位于其他目录，同样在 `.env` 设置 `DEVICE_ENTRY_NGINX_CONFIG` 为实际绝对路径。随后记录并验证：

```bash
sha256sum compose.device-entry.yaml deploy/device-entry/nginx.conf
dce() {
  sudo docker compose --env-file "$PWD/.env" \
    -f compose.yaml -f compose.voice.yaml -f compose.device-entry.yaml "$@"
}
dce config --quiet
dce run --rm --no-deps -T device-entry nginx -t
```

`DEVICE_BIND_IP` 必填，不默认监听所有网卡。只填写服务器已分配且本轮明确用于设备测试的地址，不使用 `0.0.0.0`。模板固定使用当前管理端同版本的 Nginx 镜像摘要。语法校验需要已运行的 API/voice 服务可被 Compose DNS 解析；它仅校验模板，不能证明设备可达。

准备完成并确定执行测试后，启动入口和发布地址：

```bash
dce up -d --no-deps device-entry
python3 scripts/init-voice.py --publish-urls \
  --ota-url http://192.168.168.114:18080/xiaozhi/ota/ \
  --ws-url ws://192.168.168.114:18080/xiaozhi/v1/
dc exec -T voice python /checks/check-voice.py \
  --settings /app/data/check.json --output /app/data/protocol-device-entry.json
```

入口只允许精确 OTA 检查、激活路径和 WebSocket/ready 路径，保持完整 URI 转发；管理页、登录、配置 API 和其他路径返回 `404`。它不开放 MySQL、Redis、上传下载或附加语音 HTTP 服务。对应路径：

| 入口路径 | 上游 | 行为 |
| --- | --- | --- |
| `/xiaozhi/ota/` | `api:8002` | 设备 OTA 检查响应、激活信息、WebSocket 配置 |
| `/xiaozhi/ota/activate` | `api:8002` | 设备激活状态 |
| `/xiaozhi/v1/` | `voice:8000` | HTTP 存活与 WebSocket Upgrade，300 秒读取超时 |
| `/xiaozhi/v1/ready/{session_id}` | `voice:8000` | 同会话认证后的就绪状态，普通 HTTP |
| 其他路径 | 无 | `404` |

容器必须能访问发布的服务器 LAN 地址；管理 API 保存 WebSocket 地址时会实际连接它。若网络或 Docker 路由不能通过服务器 LAN 地址回到入口，应解决该可达性后再发布，不绕过检查或把仅容器可达地址交给设备。

## 从设备网络验证，再做真机音频

现有 `check-voice.py` 的 OTA 请求使用内部 `manager_url`；`check.json` 只记录预期 WebSocket URL。**完整协议通过不表示外部 OTA 路由已验证。** 必须从设备所在网络或等价网络执行一次真实 POST。

下面的探测不使用真实设备标识；生成专属临时 MAC/Client-Id，输出脱敏结果，不打印响应中的 token 或激活码。在设备同网络的电脑执行（修改目标地址为实际值）：

```python
import json
import urllib.request
import uuid

trace = uuid.uuid4().hex
mac = '02:' + ':'.join(trace[i:i + 2] for i in range(0, 10, 2))
body = {'version': 1, 'mac_address': mac, 'uuid': trace,
        'application': {'name': 'lan-ota-probe', 'version': '1.0.0'},
        'board': {'type': 'protocol-check', 'mac': mac}}
request = urllib.request.Request(
    'http://192.168.168.114:18080/xiaozhi/ota/',
    data=json.dumps(body).encode(), method='POST',
    headers={'Content-Type': 'application/json',
             'Device-Id': mac, 'Client-Id': 'lan-probe-' + trace})
client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
with client.open(request, timeout=15) as response:
    data = json.load(response)
    code = data.get('activation', {}).get('code', '')
    assert response.status == 200 and len(code) == 6 and code.isdigit()
    print(json.dumps({'http': response.status, 'activation_code_length': len(code),
                      'device_id': mac, 'client_id': 'lan-probe-' + trace}))
```

该探测只请求未绑定激活信息，不执行设备绑定；其临时激活缓存按上游有效期过期。把探测身份保留在本轮私有记录，不能使用已有设备身份代测。随后以真实设备完成激活/绑定/音频流程，并使用[单设备记录模板](../verification/device-integration-template.md)记录本轮测试固件版本或构建号。

从同一来源网络核对 `/`、`/xiaozhi/user/pub-config`、`/xiaozhi/admin/params` 返回 `404`。用发布的 WebSocket URL完成带身份认证的完整协议检查，确认 Upgrade、错误 token 拒绝、HTTP ready、Ping/Pong；仅 `curl` 到健康页不能代替这些检查。

真实 ASR/LLM 需要有效端点、模型和凭据。初始化、ready 或独立 Edge TTS 文件生成均不表示设备采集、真实推理、音频传输与播放已通过；这些结果必须由本轮真机测试分别给出。固件仍在修改可使用测试构建，不重复索取已确认的板型信息；若选择烧录包，再核对实物适配、版本和包校验值。

## 重启、证据与恢复

先记录小智项目、源码提交、模板校验值、镜像 ID、容器状态和本轮脱敏报告。仅对本项目操作：

```bash
dce ps
dce images
dce stats --no-stream
dce stop --timeout 60 voice web api mysql redis
dce up -d --no-recreate --no-build --pull never --wait --wait-timeout 360 mysql redis api web
dce up -d --no-deps --no-recreate --no-build --pull never --wait --wait-timeout 120 voice
python3 scripts/check-manager.py
dc exec -T voice python /checks/check-voice.py \
  --settings /app/data/check.json --output /app/data/protocol-after-restart.json
```

上述 `check-voice.py` 每轮创建新身份并清理测试数据；重启前后各运行一次，只证明服务重新可用，**不能证明同一设备绑定和配置持久化**。#5 的持久化复验必须另外保留同一专属测试设备（或本轮真实样机）、测试账号和智能体：重启前记录脱敏设备标识、归属账号、智能体/模型选择与设备可达 URL；重启后用相同身份重新查询及连接，逐项比对后再清理专属测试数据。不要让自动清理脚本删除这份持久化测试对象，也不使用 JetLinks 或其他客户数据代测。

本轮使用新增的 [check-device-persistence.py](../../scripts/check-device-persistence.py) 完成该检查。脚本需要固定语音基线镜像内的 `/checks/voice_api.py` 和 `/checks/check-voice.py`；已经复制到服务器源码目录。内部协议环境的命令如下，若已发布设备地址，应使用对应的实际 OTA URL：

```bash
dc cp scripts/check-device-persistence.py voice:/checks/check-device-persistence.py
dc exec -T voice python /checks/check-device-persistence.py prepare \
  --ota-url http://api:8002/xiaozhi/ota/ \
  --output /app/data/persistence-prepare.json
# 保存容器 ID、StartedAt、镜像 ID 和数据挂载，执行上方 stop/up，再比较相同容器。
dc exec -T voice python /checks/check-device-persistence.py verify \
  --output /app/data/persistence-verify.json
dc exec -T voice python /checks/check-device-persistence.py cleanup \
  --output /app/data/persistence-cleanup.json
```

`prepare` 留存唯一标记的设备和智能体，`verify` 只读回并重新连接同一对象，`cleanup` 校验归属和标记后删除测试对象。私有 state 及每次结果均禁止覆盖；重复验收时为三个阶段指定同一新的 `--state` 路径，并为每次尝试指定新的 `--output`。每次重建 voice 容器后需重新复制脚本。服务停止和启动的证据由操作者另外记录，脚本不会自行重启或证明主机重启。

本机型服务器的 Compose `run` 不接受 `--pull`，`start` 不接受 `--wait`。运行检查前校验已导入的镜像；恢复服务使用以上 `up --no-recreate --no-build --pull never --wait`，并核对容器 ID 保持不变。若设备入口已经启用，应另外记录并恢复其原有状态。离线部署的全部命令继续使用 `dco` 覆盖文件，不能切回仅使用 registry digest 的配置。

重启后重复设备网络 OTA 请求、相同设备认证连接、真实设备对话及重连。此模板尚未配置随服务器重启自动启动，不能将容器 stop/start 检查记为主机重启验收。主机重启、restart policy、持久备份及生产容量需另行准备，并核对 JetLinks 仍保持停机、数据保留。

暂停设备入口使用 `dce stop device-entry`；暂停全部小智测试服务使用 `dce stop`，保留卷与私有配置。`--publish-urls` 修改的持久配置不会因停机回退；切回内部基线时使用 `init-voice.py --publish-urls`，同时显式传入原来的内部 OTA/WS 地址并重新验证。恢复测试前已有的管理设置则依据私有 `manager-settings-before.json` 逐项恢复。不要删除数据库卷来恢复配置，不使用全局容器清理命令。

归档只提交脱敏报告、源码/镜像标识与实际命令结果。`.env`、管理员账号/token、服务密钥、原始语音日志与录音保存在相应主机私有目录。未执行的步骤保持待测，包括设备网络可达性、公网入口、主机重启和真机音频。
