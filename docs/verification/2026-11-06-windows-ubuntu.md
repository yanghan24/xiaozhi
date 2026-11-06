# Windows 复验与 Ubuntu 部署准备

日期：2026-11-06（Asia/Hong_Kong）。运行源码：`6e354edf292c3df14fd783c5dc7314a4402e3b74`，资料分支 `docs/ai-market-integration-questionnaire`。本报告及部署模板在独立任务分支 `docs/5-windows-ubuntu-verification`，基于当时 `origin/main@46e7f33`；PR #20 当时仍 OPEN，main 尚不包含语音基线。本文只记录本轮实际结果。

## 环境与隔离

本机为 Windows 11 Pro 10.0.26300、x86_64、i9-13900KF（24 核/32 线程）、约 64 GiB 内存。D 盘初始可用约 95.6 GiB。Docker Desktop 4.74.0、Engine 29.4.3、Compose 5.1.3；Linux 引擎可用内存约 31.3 GiB。Node 24.14.0、Windows Python 3.11.15；语音初始化使用已有 WSL Ubuntu 的 Python 3.12.3。浏览器使用本机 Edge Chromium。

初始 Docker 引擎未运行，启动在遗留 Unix socket 重解析文件处失败。停止状态下保留并重建仅含运行通信文件的目录，随后 `docker info` 成功。备份保留在 `%LOCALAPPDATA%/Docker/run.before-xiaozhi-20261106`、`run.retry-xiaozhi-20261106` 和 `%LOCALAPPDATA%/docker-secrets-engine.before-xiaozhi-20261106`。未重置 Docker 数据；原有 `zhuyu-*` 四个停止容器及其卷保留。

未发现本项目已有实例或根目录私有 `.env`。本轮创建 `xiaozhi-win-20261106`，管理端 `127.0.0.1:18002`，语音使用 `127.0.0.1:18000/18003`；MySQL、Redis、API 不发布宿主机端口。`.env` 随机生成一次并保留，`.env` 与 `.runtime` 的 NTFS ACL 仅授予当前用户和 SYSTEM。测试密码、token、服务密钥及原始日志不提交。

## 管理端

| 检查 | 本轮结果 |
| --- | --- |
| `scripts/preflight-manager.py` | 通过；Docker、Compose、配置、端口和磁盘满足启动条件 |
| API 镜像构建 | 通过；源码标签为完整 `6e354ed…` 提交 |
| 页面构建 | 通过；10 项契约、70 项单元、13 项快照检查全部通过 |
| `compose up --wait` | 通过；真实 MySQL、Redis、Java API 和 Nginx 运行 |
| `scripts/check-manager.py` | 页面、初始化配置、匿名管理访问拒绝通过 |
| 浏览器注册与登录 | 首个测试管理员注册；有效验证码+错误密码拒绝；有效验证码+正确密码登录通过 |
| 浏览器配置持久化 | 保存 `baseline.acceptance.1793949877860`，整页刷新后查询并读回相同值 |
| Java 回归 | 206 tests，0 failures，0 errors，0 skipped；设备 2 项、模型持久化 3 项、登录 3 项集成覆盖检查通过 |

浏览器在 15:24 完成本轮五项观察，原始脱敏结果与截图在根目录 `.runtime/manager-check/result.json`、`configuration-readback.png`。真实登录 token 另存私有 `.runtime/voice/admin-token.json`。

首次 Java 套件本身通过，但覆盖检查脚本因 Windows `core.autocrlf=true` 的 CRLF checkout 失败。确认 Git 中原文件为 LF，工作文件转回 LF 后重建测试镜像，完整命令退出 0。新增 `.gitattributes` 的 `*.sh text eol=lf` 防止复现；运行 clone 另有相同本地 `.git/info/attributes` 规则。Java 回归使用独立 `xiaozhi-baseline-tests` 数据卷，验证后仅清理该测试项目；管理测试账号与配置保留。

## 语音构建与协议

WSL 执行 `python3 scripts/init-voice.py --configure` 通过，保留原管理设置备份。默认 ASR 设置为 `ASR_OpenaiASR`、VAD 为 `VAD_SileroVAD`，启用设备认证。原生 Windows Python 没有 `os.fchmod`，因此本轮没有在原生 Windows 执行写私有配置的脚本。

原始语音构建在 Debian 默认软件源下载阶段长时间无进展。使用未提交的 `.runtime/host-check/voice-network.Dockerfile`，仅将 Debian 软件源改为阿里云 HTTPS 镜像；基础镜像摘要、直接 Python 依赖及运行源码保持原定义。构建覆盖文件为 `.runtime/host-check/compose.build-network.yaml`，镜像使用独立标签 `local-20261106-netmirror`、revision `working-tree`，不冒充原始配方的完全一致构建。MySQL 从镜像源补充下载后核对原锁定摘要一致。

语音运行与测试镜像构建成功，`pip check` 通过。直接运行容器健康检查成功；随后完成下列真实服务检查，退出码均为 0：

| 检查 | 本轮结果与证据 |
| --- | --- |
| 独立设备第 1 轮 | 19 项通过，临时 MAC `02:31:cf:de:9d:84`；[结果](evidence/2026-11-06-windows/protocol-round1.json) |
| 独立设备第 2 轮 | 19 项通过，临时 MAC `02:fe:73:2a:93:98`；[结果](evidence/2026-11-06-windows/protocol-round2.json) |
| 提供者配置失败/恢复 | 错误配置在 hello 后由同会话 ready 识别为失败；恢复 Edge TTS 后完整检查通过，专属模型已删除；[结果](evidence/2026-11-06-windows/provider-result.json) |
| 启动配置失败/恢复 | 缺配置、缺服务密钥、错误密钥均按预期非零退出；正确配置恢复真实 HTTP 响应，4 项通过；[结果](evidence/2026-11-06-windows/startup-result.json) |
| 经设备入口的协议 | 19 项通过，包括 WebSocket Upgrade、设备认证、同会话 ready、跨身份拒绝、Ping/Pong、关闭清理；[结果](evidence/2026-11-06-windows/protocol-entry.json) |
| 本机到入口的 HTTP | 7 项通过，真实 OTA POST/激活/健康转发，以及 4 个管理/非设备路径返回 404；[结果](evidence/2026-11-06-windows/entry-result.json) |
| 恢复基线地址 | 重新发布内部 OTA/WS 地址后完整 19 项通过；[结果](evidence/2026-11-06-windows/protocol-restored.json) |
| 上游 Python 套件 | 129 passed / 5 skipped，23.29 秒；2 项因导入配置副作用跳过、3 项 LLM 因占位 key 跳过 |

协议检查使用真实 Java API、MySQL、Redis、Python 语音服务，每轮清理自己的智能体与设备。HTTP 入口额外探测只生成未绑定激活缓存，按上游 TTL 过期。设备入口测试绑定 `127.0.0.1:18080`，不证明物理设备网络可达；测试后已停止并删除该入口容器。

已将脱敏证据、[完整 Python 依赖清单](evidence/2026-11-06-windows/voice-requirements.txt) 和 [六个运行镜像身份](evidence/2026-11-06-windows/image-identities.json) 保存到本报告同级 evidence 目录；复制前对本轮管理员 token、服务密钥、测试密码、数据库密码和 SSH 密码进行字符串匹配检查，未发现泄漏。原始语音日志仍只在私有 `.runtime` 中。

最终保留 5 个管理/语音容器运行。一次空闲快照：voice 123.8 MiB、web 24.93 MiB、API 376.4 MiB、MySQL 167.1 MiB、Redis 7.109 MiB；这是单机测试观测，不是容量结论。

## Ubuntu 与设备入口

已通过既有认证只读登录 `jt@192.168.168.114:22`，本机直连成功。Ubuntu 20.04.3、x86_64、8 vCPU、16010 MiB 内存（检查时可用 13252 MiB）、449 GiB 可用磁盘，Docker 26.1.3、Compose 2.27.1。`jt` 不在 docker 组，Docker 检查通过 sudo 完成。JetLinks 有 3 个运行容器和 3 个停止容器，本轮未修改服务器容器、卷、业务配置或防火墙。

小智语音后台未部署，未发现匹配的私有配置；旧 JetLinks 中名称含“小智”的设备记录不作为本项目实例。候选独立项目为 `xiaozhi-ubuntu-test`，候选设备入口 `192.168.168.114:18080`；执行前重新核对端口。

[Ubuntu runbook](../deployment/ubuntu-isolated-test.md)、[设备联调模板](device-integration-template.md) 以及设备入口 Compose/Nginx 已准备。Compose 合并解析、固定镜像的 `nginx -t`、本机真实 HTTP/WS 转发及管理接口隔离均通过。模板只允许 OTA、WebSocket 和 ready 路径，其余路径 404；管理端继续绑定回环地址。

服务器 Python 为 3.8.10、UFW 检查时 inactive；到本机 `172.19.6.206` 的返回路由经 `192.168.168.254`。不使用代理的 HEAD 检查中，Docker Registry/GitHub 返回连接错误，Maven/npm/阿里云 Debian/PyPI 返回 200。准备了 [离线部署说明](../deployment/offline-bundle.md)，包位于本机 `.runtime/ubuntu-test-bundle-20261106`：固定源码、六个运行镜像、已审查模板、构建配方、依赖清单与 SHA-256 校验。导出镜像逐项核对镜像身份、平台、文件系统层及 revision，未包含账号、token、数据库卷。服务器导入和启动仍待执行。

设备可以修改 OTA 地址、固件仍在修改，这两项沿用用户确认。联调记录当时的测试固件版本/构建号；若仅改 OTA 地址则无需虚构固件包或升级结果。设备所在网络到服务器的业务入口尚未实测，公网映射未开通，SSH 可达不代表设备可达。

## 验收边界

真实 ASR/LLM 凭据、设备音频输入、语音传输与现场播放尚未验证。模拟协议 ready 不能证明模型调用有效，独立 TTS 用例也不能证明设备播放。现有完整协议脚本每轮删除自己的设备/智能体，因此两轮通过不等于 #5 的同一设备跨重启绑定持久化。服务器实际部署、设备网络验证、固件升级/回滚与 #19 并发容量仍分别验收；本轮不关闭相关任务。
