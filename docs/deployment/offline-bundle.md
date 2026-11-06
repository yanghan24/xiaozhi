# 2026-11-06 离线测试部署包

本机交付目录为 `D:\AI\xiaozhi\.runtime\ubuntu-test-bundle-20261106`。这是已验证运行镜像和源码的准备产物，尚未上传或在 Ubuntu 启动。服务器检查时 Docker Registry/GitHub 直连失败，Maven/npm/阿里云 Debian/PyPI 的 HEAD 请求返回 200，因此提供离线路径。探测成功不代表完整依赖下载已验证。

包内包含 `source.tar.gz`、`overlays.tar.gz`、`images.tar`、`manifest.json`、`SHA256SUMS`、`verify-images.py`、本次语音 Dockerfile 与完整依赖清单。源码通过固定 `6e354edf292c3df14fd783c5dc7314a4402e3b74` 的 `git archive` 导出，不含 `.git`、根目录私有 `.env`、`.runtime`、本机账号或数据库卷。源码自带的 `main/manager-web/.env` 仅含公开页面标题、描述和关键词，随固定源码保留。覆盖包提供已审查的部署模板、报告和 LF 属性规则。

语音镜像为 `xiaozhi-voice:local-20261106-netmirror`，revision 标签为 `working-tree`；仅 Debian 软件源 URL 改为阿里云 HTTPS 镜像，实际构建配方及 SHA-256 随包记录。API/web 使用完整源码提交作标签。清单分别记录 Docker 29 的运行时镜像标识和 Docker archive 配置标识，用于兼容 Ubuntu Docker 26 的导入结果；同时校验平台、文件系统层和 revision 标签。

源码导出显式使用 `git -c core.autocrlf=false archive`，避免 Windows 将 Linux 脚本和配置转换为 CRLF；包内 shell 脚本均已检查使用 LF。

## 导入并初始化

经安全文件传输将整个包放到服务器专属目录后，先校验完整性，再在**新目录**解压。目录已存在时应检查并复用已有环境，不覆盖数据。

```bash
set -e
cd ~/xiaozhi-package-20261106
sha256sum -c SHA256SUMS
mkdir ~/xiaozhi-ubuntu-test
tar -xzf source.tar.gz -C ~/xiaozhi-ubuntu-test
tar -xzf overlays.tar.gz -C ~/xiaozhi-ubuntu-test
sudo docker load --input images.tar
sudo python3 verify-images.py
cd ~/xiaozhi-ubuntu-test
python3 scripts/init-manager.py
```

上述校验或命令失败即停止；先处理失败原因，再继续初始化和启动。

不运行 `git rev-parse`：源码归档没有 `.git`。新 `.env` 中的随机密码属于服务器这套新数据库，保留它们，只更新下列非秘密字段：

```bash
python3 - <<'PY'
from pathlib import Path
p=Path('.env')
updates={
    'XIAOZHI_PROJECT_NAME':'xiaozhi-ubuntu-test',
    'MANAGER_PORT':'18002',
    'BUILD_TAG':'6e354edf292c3df14fd783c5dc7314a4402e3b74',
    'SOURCE_REVISION':'6e354edf292c3df14fd783c5dc7314a4402e3b74',
    'VOICE_BUILD_TAG':'local-20261106-netmirror',
    'VOICE_SOURCE_REVISION':'working-tree',
    'VOICE_WS_PORT':'18000','VOICE_HTTP_PORT':'18003',
    'DEVICE_BIND_IP':'192.168.168.114','DEVICE_PORT':'18080',
}
lines=p.read_text().splitlines()
seen=set()
for i,line in enumerate(lines):
    key=line.partition('=')[0]
    if key in updates:
        lines[i]=key+'='+updates[key]
        seen.add(key)
lines.extend(k+'='+v for k,v in updates.items() if k not in seen)
p.write_text('\n'.join(lines)+'\n')
p.chmod(0o600)
print('Updated non-secret settings; generated database passwords preserved.')
PY
```

原 Compose 的 MySQL/Redis/Nginx 使用 registry digest；`docker load` 不一定保留 RepoDigests。随包 [compose.offline.yaml](../../compose.offline.yaml) 使用经清单校验的专用离线标签。先验证镜像，再使用覆盖配置；不要用未经核对的同名镜像。

```bash
dco() {
  sudo docker compose --env-file "$PWD/.env" \
    -f compose.yaml -f compose.voice.yaml \
    -f compose.device-entry.yaml -f compose.offline.yaml "$@"
}
sudo python3 scripts/preflight-manager.py
dco config --quiet
dco up -d --no-build --pull never --wait --wait-timeout 360 mysql redis api web
python3 scripts/check-manager.py
```

随后按 [Ubuntu runbook](ubuntu-isolated-test.md) 通过 SSH 隧道完成**服务器专属**账号注册/登录，传递本次服务器 token，再执行 `python3 scripts/init-voice.py --configure`。不使用本机数据库的 token。

```bash
dco up -d --no-deps --no-build --pull never --wait --wait-timeout 120 voice
python3 scripts/check-voice.py
python3 scripts/init-voice.py --publish-urls
dco exec -T voice python /checks/check-voice.py \
  --settings /app/data/check.json --output /app/data/protocol-result.json
dco exec -T voice python /checks/check-voice-providers.py
dco run --rm --no-deps -T voice-config-check
```

这些离线镜像覆盖服务运行和运行时协议检查，不含单独的 Maven/前端/Python 单元测试镜像。离线流程不重新 build；所有 Compose 操作（含恢复）都使用相同的 `dco` 覆盖配置。

## 设备入口与边界

在设备网段、路由和访问条件确认后，才单独执行 `dco up -d --no-deps --no-build --pull never device-entry`，然后发布真实 OTA/WS 地址并重跑协议及外部 OTA 检查。候选 LAN 地址为 `http://192.168.168.114:18080/xiaozhi/ota/` 和 `ws://192.168.168.114:18080/xiaozhi/v1/`；当前它们尚未在服务器部署或从设备网络验证。

服务器 `jt` 通过 sudo 使用 Docker，不新增 docker 组权限。UFW 检查时 inactive，仍不能据此认定跨网段、防火墙、NAT 或公网入口可达。现有 JetLinks 服务、数据、端口和账号保持原状态。

镜像校验/加载成功不是 Ubuntu 服务或真机验收。真实 ASR/LLM/TTS 仍需要有效配置与网络，设备输入和播放由现场测试确认。联调记录实际测试固件版本或构建号；不把修改 OTA 地址视为固件升级或回滚测试。
