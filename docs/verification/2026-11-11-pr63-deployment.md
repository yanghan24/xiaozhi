# PR #63 Ubuntu 服务端部署

2026-11-11 16:24–16:27（Asia/Hong_Kong），`jt@192.168.168.114`。用户明确要求部署 PR #63，本次部署其固定 head `7738647759689d8a345a0f563b103467ae0b3c68`；PR 仍为 Draft，未合并，不代表 #42 的固件与真机验收完成。

## 版本与变更

| 项目 | 结果 |
| --- | --- |
| API 镜像 | `xiaozhi-product-api:1468eeb` → `xiaozhi-product-api:7738647` |
| 新镜像 ID | `sha256:bcc1c6cef74ae6f1c909303a7123160455c0e8622983eb45e9ec81c569f6b7a7` |
| 受测及运行 JAR SHA-256 | `8ca7cb5debf499ccbf28c7c6eb88a688250d3dbfc49284773808747ac5621af4`，一致 |
| 部署 Compose | `/home/jt/pr26-deployment-885918d/private/compose.json` |
| 本轮部署快照 | `/home/jt/pr63-deployment-7738647` |
| 语音 | `xiaozhi-product-voice:prompt-7f10cbd`，未重建或重启 |

仅重建产品 API，Nginx 增加 recovery 创建、查询与完成的精确入口并重载。`dc_device_recovery` 加法迁移已执行；补发记录数为 0，没有对真实设备轮换秘密。API 同时包含其基线已合入的设备许可转换整理。手机集市页面、昵称优先显示、积分开关、既有环境配置与密钥保留。部署前后逐项核对所有运行容器：除 API 外容器 ID 与启动时间不变。

## 验证证据

复用此前在 `/home/jt/issue42-recovery-cdd24a5` 完成的隔离回归，未重复运行整套测试。测试报告为 `.runtime/issue42/reports-20261111T062851Z`：357 tests，0 failures/errors/skipped，集成测试守卫通过。部署前将固定 head 的 API 与管理前端共 1,083 个文件逐个与测试镜像及受测目录做 LF 规范化比较，全部一致；从受测镜像提取 JAR，运行产物校验值一致。

| 本轮检查 | 结果 |
| --- | --- |
| 候选 Nginx 与实际配置 `nginx -t` | 通过 |
| API readiness 与三张交付/补发表存在 | 通过 |
| 既有公网预检 | 24/24；含 OTA、管理/内部路由隔离、WSS 无凭据和无效凭据 1008 拒绝 |
| 补发与账号入口 | 12/12；三种 recovery 操作拒绝匿名/无效 bearer，非法 ID、方法及超大请求拒绝，账号/退出拒绝匿名 |
| 既有交付拒绝协议 | 20/20；Content-Type 与鉴权错误契约 |
| 备份及配置 | recovery 表纳入备份，原交付密钥与配置保留 |

公网测试客户端在 Ubuntu，通过临时 SSH loopback HTTP CONNECT 代理访问 `https://bridge.hncig.cn:18443`，保持证书校验；不宣称 Ubuntu 直连公网已恢复。测试只使用合成无效凭据，没有预登记设备或读取真实客户积分。临时代理在检查结束后关闭。[脱敏机器证据](evidence/2026-11-11-pr63-deployment/evidence.json)包含产物、回归计数及 36 项入口结果；额外 20 项交付检查日志保存在部署快照的 `delivery-rejections.jsonl`。

## 备份与恢复边界

- 部署前：`/home/jt/pr26-deployment-885918d/private/backups/20261111T082449Z`。
- 部署后：`/home/jt/pr26-deployment-885918d/private/backups/20261111T082535Z`。

备份保留必要身份、设备、交付与补发记录、原密钥及配置；排除对话正文和音频。持久 SQLite 备份完整性检查通过。同主机备份不等于异地灾备，未执行恢复演练。

部署脚本保留于 `/home/jt/pr63-deploy.sh`，部署快照保留前后 Compose、Nginx、容器状态和受测 JAR。脚本包含失败回退保护，但未触发或演练。发生补发后，旧凭据立即失效，不能回滚秘密或退回不理解 recovery 的旧 API。需要暂停新补发时仅撤回 recovery 路由，保留支持新凭据的 API、操作记录、归属、当前 hash 与原加密密钥；不能删档或重跑旧 complete。

本轮未执行管理员成功补发、补发后服务重启、真实 P4 写回、物理停播、真实语音问答或 Ubuntu 主机重启。这些继续按 [PR #63 的恢复协议与 P4 接续](https://github.com/yanghan24/xiaozhi/blob/7738647759689d8a345a0f563b103467ae0b3c68/docs/development/device-credential-recovery.md)验收。当前服务地址与版本维护于[当前部署入口](../deployment/current.md)，README 与 AGENTS.md 到该入口的导航已核对。
