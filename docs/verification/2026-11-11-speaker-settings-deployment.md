# 音箱设置服务端 Ubuntu 部署记录

2026-11-11 10:05–10:06（Asia/Hong_Kong），Ubuntu `jt@192.168.168.114`。

## 版本与范围

部署固定源码 [`b670a17c88ea86b5396c56785e776845fd93d82a`](https://github.com/yanghan24/xiaozhi/commit/b670a17c88ea86b5396c56785e776845fd93d82a)，在服务器已运行的长期烧录凭据版 `3f963b6` 上应用音箱设置补丁。源码保存在 `codex/speaker-settings-deployment` 分支；没有合并功能 PR 或修改 main。已核对最新 main `850e73d` 的部署入口；它仍记录 PR #26，因此同时核对此前部署证据与服务器实际镜像。随后整合最新 main 的文档/天气资料，API、管理前端及入口源码与受测提交无差异。

原附件 `speaker-settings-server.patch` SHA-256：`35f4767c0953d4c7ef80437c044ce1fedb73b0dd0108c18248923db137ed9b7f`。补丁干净应用，无新增数据库迁移。首轮 342 项中两个新增退出用例返回 400：测试客户端沿用了管理端 Long→字符串序列化。修正为 JSON 数值节点后，重新在独立数据库/Redis 上完整运行；没有放宽服务端版本号校验。

| 项目 | 结果 |
| --- | --- |
| API 镜像 | `xiaozhi-product-api:b670a17` |
| 镜像 ID | `sha256:238c68cc9f1207eea0b8ac5cadd2afd254d24e5731a7ff9f7ca423a275944673` |
| 受测与实际部署 JAR SHA-256 | `45af02bb8df6b174440d4b18389a8b64af0cec1d1fdc0374c181c682fad496f7`，一致 |
| 入口配置 SHA-256 | `72ba0185d071c00f88408266d8e4e71150c774386573115593cdae020509ab3f` |
| 产品运行 Compose | `/home/jt/pr26-deployment-885918d/private/compose.json` |
| 本次部署快照 | `/home/jt/speaker-settings-deployment-b670a17` |

仅重建 API 容器，Nginx 校验候选配置并在 API 就绪后热重载。语音 `885918d`、管理前端、身份数据库、Redis、入口容器身份与启动时间均保留。原 OIDC 客户端/密钥、设备数据、烧录凭据及撤销记录保留；JetLinks 仍停止。

新增两条公网精确路径：`GET /xiaozhi/device-control/v1/account` 与 `POST /xiaozhi/device-control/v1/sign-out`。接口契约见[受测版本交接说明](https://github.com/yanghan24/xiaozhi/blob/b670a17c88ea86b5396c56785e776845fd93d82a/docs/handoff/speaker-settings-server.md)。退出请求含数值 `expected_permission_version`，同一次退出重试幂等，恢复后旧请求拒绝；设备归属及共享授权保留。

## 验证证据

| 检查 | 结果 |
| --- | --- |
| Ubuntu 独立完整 Java 回归 | 342 tests，0 failures/errors/skipped；集成测试运行守卫通过 |
| `CustomerVoicePermissionHttpTest` | 17 项全部通过 |
| 新增场景 | 当前归属账号、精确小数/负积分、缺积分授权、积分主体不匹配、查询中并发退出无泄露、停播 ACK、恢复后拒绝重放、第二设备隔离 |
| `DeviceDeliveryHttpTest` | 16 项通过，长期凭据能力保留 |
| 原公网预检 | 24/24：OTA、语音 HTTP/WSS、设备控制及管理/内部隔离等 |
| 原公网拒绝协议 | 20/20 |
| 新公网及兼容检查 | 9/9：account/sign-out 无凭据 401、错误方法 403、扫码无效请求处理、OIDC 无效回调拒绝、长期凭据有效/撤销/越权拒绝 |
| Nginx | 候选与线上 `nginx -t` 通过，热重载完成 |

全部公网请求正常验证 TLS。有效长期凭据只查询不存在的 operation，返回 `404 DELIVERY_NOT_FOUND` 证明认证通过；已撤销凭据返回 `401 TOOL_AUTH_REQUIRED`。没有签发新凭据、操作真实设备或真实账号授权。

回归源码/日志/XML：`/home/jt/speaker-settings-regression-b670a17/.runtime/pr52/`，隔离测试容器已停止。失败首轮保留在同级 `speaker-settings-regression-dcb8473`。脱敏汇总见[证据 JSON](evidence/2026-11-11-speaker-settings/evidence.json)；服务器副本为部署快照目录下 `evidence.json`。

## 积分权限实际状态

现有客户端：`app_07a20fc2e1c14995a9f8c2cc668a7aff`，Issuer `https://aimarket.hncig.cn/oidc`。正式开通途径是集市超级管理员在“系统管理 → 应用接入”编辑现有客户端并勾选 `balance:read`，保留原范围和回调，不重建客户端或重置秘密。

2026-11-11 用户明确回复“已经勾选过了”，据此确认客户端权限前提后，将 API 的 `CUSTOMER_OIDC_BALANCE_ENABLED` 设为 `true`，并核对运行容器确有该环境变量。当前可用浏览器未登录集市管理员；本次没有独立查看管理员配置，也没有执行集市权限变更，不能将开关本身或公开 Discovery 当成客户端权限证明。

历史账号的授权范围与客户端允许范围分开：之前未同意 `balance:read` 的用户可能需要从设备设置扫码补授权。未代用户扫码，未查询真实账号积分；真实积分展示仍需该用户授权后验收。缺积分授权时接口返回 `balance:null`，不伪造 0，不停用语音。

## 备份与恢复

部署前必要数据备份：`/home/jt/pr26-deployment-885918d/private/backups/20261111T020505Z`。

部署后备份：`/home/jt/pr26-deployment-885918d/private/backups/20261111T020526Z`。包含长期凭据表、设备/账号必要状态、原加密密钥及持久失效状态，排除对话正文与音频；SQLite 副本完整性校验通过。同主机备份，未作异地灾备或恢复演练。

部署快照内 `compose-before.json` / `nginx-before.conf` 保留原 `3f963b6` API 与入口配置。恢复脚本 `/home/jt/speaker-settings-rollback.sh` 已做 Bash 语法检查，未执行回退。授权恢复时使用私有 sudo runner 执行该脚本：它先拒绝覆盖后续部署，恢复之前配置、启动旧 API、等待就绪、校验并重载入口；不回滚数据库，不覆盖私有密钥，也不恢复旧凭据或批量启用设备。旧版仍支持长期烧录凭据，但不提供本次两个设置接口。

已经发生的本机退出不会因 API 回退而撤销，仍需原归属账号扫码恢复。运行配置及备份含秘密，只留在服务器私有路径。

本次仅完成服务端验证和部署。没有烧录固件、真机退出、真实扬声器停播、真实账号积分、Wi-Fi 断连恢复或 50 路完整语音验收；这些不由 HTTP 用例代替。README 与 AGENTS.md 到当前部署入口、入口到本报告的导航已核对。
