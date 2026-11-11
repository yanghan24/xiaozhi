# 音箱集市显示名称修正部署

2026-11-11 10:51–10:52（Asia/Hong_Kong），Ubuntu `jt@192.168.168.114`。

## 修正与版本

用户要求先部署附件 `speaker-settings-profile-fix.patch`，再继续页面工作。补丁 SHA-256 为 `91a92656b4f116b6dd27593ff27c47359cbc4d0d3dafc06050aa4fc8ba0727ef`，在已部署的手机扫码样式版 `52e627f` 上干净应用，固定提交 `1468eeb0ec0962e6a2862274f79471d3f7699e00`，源码分支 `codex/speaker-profile-fix`。

账号显示名称从仅取 `preferred_username` 改为依次选择有效 `nickname`、`name`、`preferred_username`，无可用值则为 null，不将 `sub` 当名称。保持非空、无控制字符、UTF-8 不超过 256 字节的约束。读取前后的主体/设备归属与许可校验、积分和退出契约保留。不新增数据库迁移或路由，不要求重新烧录或扫码；音箱设置点“刷新”即可再次读取当前显示名称。真实账号最终显示仍由使用者核对。

| 项目 | 结果 |
| --- | --- |
| Ubuntu 完整 Java 回归 | 349 tests，0 failures/errors/skipped，运行守卫通过 |
| CustomerProfileDisplayTest | 6 项通过：中文昵称、优先级、显示名回退、登录名回退、非法候选、禁止使用 subject |
| CustomerVoicePermissionHttpTest | 18 项通过，新增昵称/显示名变化后的实时查询，原归属、并发退出、ACK、恢复防重放及第二设备隔离仍通过 |
| DeviceDeliveryHttpTest | 16 项通过，长期烧录凭据能力保留 |
| 公网检查 | 56/56 通过，含既有 OTA/WSS/扫码、设置接口、长期凭据有效/撤销/越权及页面 HTML/CSP |
| API 镜像 | `xiaozhi-product-api:1468eeb` |
| 镜像 ID | `sha256:a8d952e68545b4e41a0729cde17b6cbfdf37dc3d851eef7748a51558f62adf07` |
| 受测及运行 JAR SHA-256 | `fbb10d3d145beea3bbde9f70c3204213d52e96bdef9f1dca55098cc36bee240d`，一致 |

公网测试客户端运行在 Ubuntu，经临时 loopback SSH 转发 HTTP CONNECT 出站代理访问同一公网域名，保留 TLS 校验；原因及前轮直连超时证据见[手机页面部署记录](2026-11-11-mobile-binding-style.md)。未宣称 Ubuntu 直连已恢复。测试容器已停止，临时代理通道已关闭。

完整脱敏结果：[evidence.json](evidence/2026-11-11-speaker-profile-fix/evidence.json)。隔离回归源码/日志/XML：`/home/jt/profile-fix-regression-1468eeb/.runtime/pr52/`。没有进行真实账号授权、真实音箱操作或实时积分验收。

## 部署与恢复

仅重建 API，Nginx 内容不变，`nginx -t` 后重载。语音、管理前端、数据库、Redis、入口容器身份与启动时间保留。`CUSTOMER_OIDC_BALANCE_ENABLED=true`、现有 OIDC/设备密钥、长期凭据及撤销状态保留；JetLinks 仍停止。原 AI 集市风格页面已随版本保留并经公网 HTML/CSP 检查。

- 部署快照：`/home/jt/profile-fix-deployment-1468eeb`。
- 部署前备份：`/home/jt/pr26-deployment-885918d/private/backups/20261111T025110Z`。
- 部署后备份：`/home/jt/pr26-deployment-885918d/private/backups/20261111T025142Z`。
- 恢复脚本：`/home/jt/profile-fix-rollback.sh`，Bash 语法检查通过，未执行恢复演练。通过私有 sudo runner 执行，拒绝覆盖后续部署并回退到页面版 `52e627f`；不覆盖数据库、凭据、归属或退出状态。

备份包含必要账号/设备/长期凭据及原密钥，排除对话正文和音频，SQLite 副本完整性检查通过；同主机备份不是异地灾备。源码、部署证据提交任务分支，README/AGENTS → 当前部署入口 → 本报告导航已核对，未合并 main。
