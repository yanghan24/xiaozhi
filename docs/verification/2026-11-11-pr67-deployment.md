# PR #67 Ubuntu 回归与部署

2026-11-11 16:28–16:35（Asia/Hong_Kong），Ubuntu `jt@192.168.168.114`。按用户要求先完成 Ubuntu 回归，再部署 API 与公网 `unbind`。本轮完成服务端范围；PR #67、PR #63 均未合并，未安装固件或操作真实设备。

## 固定源码与现网兼容

开始时实查 API 为 `7738647`，包含 PR #63 的设备凭据补发；本地及 GitHub main 部署入口尚记录 `1468eeb`。最新 [PR #63 部署证据](2026-11-11-pr63-deployment.md)确认现网状态，因此未直接用缺少补发实现的 PR #67 head 覆盖现网。

| 项目 | 固定值 |
| --- | --- |
| PR #67 head | `71130c1b5f886f74af20f9faf18353678f600fa5` |
| 保留的现网 PR #63 | `7738647759689d8a345a0f563b103467ae0b3c68` |
| 最终组合提交 | `1b50709fd1cadfad966b12d4afedf78b1f279aa6`，分支 `codex/pr67-server-deployment` |
| API 镜像 | `xiaozhi-product-api:1b50709` |
| 镜像 ID | `sha256:429f8577b4559b27e7dacb558db0e4f53321faed2e2eb4f70798bef1f47ecb63` |
| 受测及运行 JAR SHA-256 | `24eedf38b764c60a94d90392651f76db502333faac5c51f91dc8040fb506b24a`，一致 |
| 部署快照 | `/home/jt/pr67-deployment-1b50709` |
| 运行 Compose | `/home/jt/pr26-deployment-885918d/private/compose.json` |

向前合并两个固定提交，Nginx 唯一文本冲突保留两侧的解绑与补发路由，测试文件自动合并后核对两侧用例均保留。相对现网，API 运行代码仅增加 PR #67 的控制器、服务及凭据过滤路径；不增加数据库迁移。组合源码已推送保留，未将两个功能 PR 合并进 main。

## Ubuntu 执行证据

源码通过 `git archive` 传入独立目录 `/home/jt/pr67-regression-71130c1` 和 `/home/jt/pr67-regression-1b50709`。各自使用独立 Compose 项目、MySQL、Redis、网络和卷，不连接现网数据库运行回归。复用离线 Maven 依赖，删除旧构建目录后复制全部源码，执行 `mvn -o -B -ntp package -DskipTests` 构建，再运行 `mvn -o -B -ntp test -DskipTests=false` 和 `bash scripts/check-integration-tests.sh`。两套测试完成后已停止，报告及日志保留于各自 `.runtime/pr52/`。

| 检查 | 结果 |
| --- | --- |
| PR #67 原候选完整 Java | 357 tests，0 failures/errors/skipped，集成测试守卫通过 |
| 最终组合版本完整 Java | 362 tests，0 failures/errors/skipped，集成测试守卫通过 |
| CustomerVoicePermissionHttpTest | 24 项通过，包含解绑后同/异账号绑定、旧页面/语音/迟到请求围栏、并发账号读取、第二设备隔离，以及补发后旧凭据与迟到恢复拒绝 |
| DeviceDeliveryHttpTest / CustomerProfileDisplayTest | 20 / 6 项通过，保留补发、长期交付凭据及昵称行为 |
| 候选及实际 Nginx 配置 | `nginx -t` 通过，API readiness 后重载 |
| 公网预检 | 27/27，含 unbind 匿名和未签发凭据 v1 401、GET 403、OTA/WSS 及管理/内部路由隔离 |
| 补发入口拒绝检查 | 10/10，匿名、无效 bearer、非法 ID、错误方法及超大请求 |
| 设置及长期凭据 | 9/9，账号/旧退出鉴权、有效与撤销凭据状态、禁止越权管理 |
| 手机页面与 CSP | 3/3，集市样式、UTF-8、移动 viewport、样式 hash 和禁止缓存 |
| 交付拒绝协议 | 20/20 |

共 69 项公网检查通过。公网客户端运行于 Ubuntu，经临时 loopback SSH HTTP CONNECT 出站代理访问 `https://bridge.hncig.cn:18443`，保持 TLS 证书校验；不宣称 Ubuntu 直连公网已恢复。结束后关闭本轮代理通道。公开预检使用合成未签发凭据，额外长期凭据检查使用已有私有有效/撤销凭据只读查询；未预登记或修改真实设备。完整脱敏证据见 [evidence.json](evidence/2026-11-11-pr67-deployment/evidence.json)。

## 部署、备份与回退

16:34–16:35 仅重建 API，向现有 Nginx 配置增加 `POST /xiaozhi/device-control/v1/unbind` 精确入口，保留现有补发路由。部署前后校验所有当时运行容器：除 API 外 ID、启动时间不变。语音仍为 `xiaozhi-product-voice:prompt-7f10cbd`；原数据库、Redis、管理页面、OIDC 配置、积分开关及密钥保留，JetLinks 仍停止。Compose 除 API 镜像字段外与部署前一致。

- 部署前备份：`/home/jt/pr26-deployment-885918d/private/backups/20261111T083432Z`。
- 部署后备份：`/home/jt/pr26-deployment-885918d/private/backups/20261111T083455Z`。
- 执行脚本：`/home/jt/pr67-prepare.sh`、`/home/jt/pr67-deploy.sh`、`/home/jt/pr67-verify.sh`。
- 回退脚本：`/home/jt/pr67-rollback.sh`，Bash 语法检查通过，未执行恢复演练。先检查当前仍为本轮版本，由私有 sudo runner 执行；拒绝覆盖后续部署，恢复 API `7738647` 与本轮之前的入口，保留凭据补发能力和当前业务数据。

备份包含必要身份、设备、交付、补发记录、原密钥及配置，排除对话正文和音频；SQLite 副本完整性验证通过。同主机备份不等于异地灾备。已经执行的解绑属于持久业务状态，回退 API 不会恢复旧归属，不能用旧数据覆盖新账号。旧 API 不支持解绑时，新固件应保持停播并提示重试，不能降级调用旧 `sign-out`。

## 实机边界

本轮未安装配套固件 `817e47b`，未执行真实另一账号接手、物理停播 ACK、真实语音或第二台音箱现场隔离，也未重启 Ubuntu 主机。上述自动化结果不能替代这些验收。下一步按 [PR #67 服务器交接](https://github.com/yanghan24/xiaozhi/blob/71130c1b5f886f74af20f9faf18353678f600fa5/docs/handoff/speaker-settings-server.md)在已部署 API 上安装配套固件并完成现场验收。README / AGENTS.md → [当前部署入口](../deployment/current.md) → 本报告导航已核对。
