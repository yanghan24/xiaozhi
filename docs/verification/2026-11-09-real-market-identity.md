# 真实集市身份联调记录（#11 / #28）

日期：2026-11-09；文中时间均为香港时间（UTC+08:00）。身份实现已随 [PR #31](https://github.com/yanghan24/xiaozhi/pull/31) 合并，主线提交为 `ec5a49cdb8a19963a5c7ba2c2f98cb3af206e808`。

## 当前结论

真实手机登录、授权确认、拒绝授权、刷新轮换、共享授权撤销及重新授权恢复已通过。用户确认企业微信登录可用；没有逐一验收全部手机浏览器或登录方式。

OIDC 退出后尚未观察到有效后通道通知，#11 / #28 保持未完成。用户于当晚暂停联调，计划 2026-11-10 请集市管理员核查，见[管理员检查清单](../handoff/2026-11-10-market-logout-notification.md)。#13 的设备绑定与真机闭环未执行；#12 正式资源契约的缺口未因此消失。

## 网络与运行环境

| 项目 | 已核实事实 |
| --- | --- |
| 身份 issuer | `https://aimarket.hncig.cn/oidc`；web 授权码 + PKCE S256 + client_secret_basic |
| 内网解析 | 用户提供 `192.168.241.202`；Ubuntu `/etc/hosts` 和实际管理 API 的 Compose `extra_hosts` 均已配置 |
| 身份 443 | 21:43 起，宿主机与实际管理 API 容器的 Discovery、JWKS 均 HTTP 200；保留正式域名、TLS 链及主机名校验 |
| 资源 4090 | 后续宿主机与管理 API 容器的内网 TCP 连接成功；未验证规范通响应、推理或正式 TTS |
| 公网入口 | `https://bridge.hncig.cn:18443`，TCP 转发至 Ubuntu `192.168.168.114:443` |
| 真实身份服务 | `issue11-identity`，使用 #29 正式 JAR、真实集市和隔离的 `issue29-db` / `issue29-redis`；不包含可控身份提供方 |
| 测试入口 | 隔离数据库预置有效请求，公网仅放行手机页、结果、回调和通知；运营登录实测 404 |

此前 Windows 可访问集市公网地址，而 Ubuntu 连接超时。Ubuntu 曾收到访问内网地址的 `admin prohibited`，之后相同内网地址连接成功；没有证据证明具体由哪台设备或哪条策略修复。不能把 NAT 回流假设写成已确认根因，也不能把现在的内网成功写成公网路径已修复。

两份原始接入文档的 web 身份端点均使用不带端口的 HTTPS 地址，即默认 443。当前手机扫码打开的是 web OIDC 流程，不是设备授权 grant。原始资料保留原文件；通用授权生命周期按[新版接入文档](../../元筑AI集市应用接入接口文档(1).md)核对。

## 真实验收结果

| 项目 | 观察与判断 |
| --- | --- |
| 真实登录与 consent | 手机返回“身份已确认，等待手机确认”；数据库 `IDENTITY_VERIFIED / ACTIVE`，PKCE verifier 已消费 |
| 授权凭据 | Access Token、Refresh Token、业务 Key 加密保存，Key 的 `expires_at` 为 null；领取凭据及刷新均实际成功 |
| 自动刷新轮换 | 同一授权的 Access Token 到期时间由 21:58:20 推进至 22:03:00，仍为 ACTIVE；正式代码只接受与旧值不同的新 Refresh Token，没有重放旧令牌 |
| 共享授权撤销 | 用户确认独立测试账号并授权实验；既有运维脚本核对用户/应用后执行：模型列表 HTTP 200 → 撤销 HTTP 200 且 `revoked=true` → 同一 Key HTTP 401；没有模型推理 |
| 拒绝授权 | 用户在真实 consent 页取消，返回“已取消身份授权，请重新扫码”；服务端 `IDENTITY_REFUSED`，未建立身份，verifier 已清理 |
| 撤销后失效识别 | 未修改到期时间；22:02:30 自然刷新后转为 `REAUTH_REQUIRED` |
| 重新授权恢复 | 同一测试账号再次 consent，新流程 `IDENTITY_VERIFIED`，授权恢复 ACTIVE，新凭据保存；旧失效状态未覆盖新授权 |
| 无效通知 | 公网通知路径 GET 返回 403；发送无效 `logout_token` 的 POST 返回 400，没有生成有效通知记录 |
| 有效注销通知 | 两轮手机 OIDC 退出均报告成功，但有效通知数仍为 0；第二轮先重新登录，确认保存了 sid，再退出并额外等待 15 秒，会话仍为 `IDENTITY_VERIFIED` |

数据库检查仅输出状态、布尔值和时间；不输出用户标识、Token、Key 或 Secret。共享撤销工具临时创建的明文测试会话文件已删除。

## 通知问题的证据边界

第二轮约 22:13–22:15。通知入口启用了仅该路径的诊断日志，字段为时间、来源 IP、方法、HTTP 状态和 upstream 状态，不记录查询串、请求体或令牌。日志中只有 22:13:20 的诊断 POST（HTTP 400），该请求由小智侧主动发送以验证日志工作，不是集市通知。用户再次退出后，在观察窗口内未出现对应新请求。

这支持“本次通知未到达已监测入口”，不能独自区分集市未登记、未发送、目标地址错误、源浏览器会话不匹配或发送网络失败。首次退出时尚无该路径日志，只能确认没有通过验签的收据。文档没有保证应用授权撤销一定发注销通知；浏览器 OIDC 退出也不等于撤销独立刷新授权/Key，不能把仍为 ACTIVE 直接判为错误。

## 尚未验收

- 集市实际通知地址登记、通知触发与有效签名通知送达；通知失败/重试及覆盖范围。
- 第二独立账号、无应用权限等差异状态。
- 真实环境完整的网页 30 分钟闲置、临时错误及多设备联动边界；#29 自动回归不替代所有真实验收。
- #13 的设备认证、正式请求发行、手机最终确认、唯一归属、单台退出/恢复及真机；#12/#14 的正式资源调用。

## 运行记录与恢复位置

- Ubuntu 隔离运行目录：`/home/jt/xiaozhi-issue29/.runtime/real-identity/`。包含加密密钥、私有环境文件、脱敏状态检查和代理配置备份；不要整体上传。
- 客户端配置沿用 `/home/jt/xiaozhi-ubuntu-test/.runtime/ai-market/oidc-client.json`，0600；密钥文件为上述隔离目录内 `identity.key`，0600，只读挂载到容器。
- 宿主机解析备份：`/etc/hosts.issue28-20261109T133803Z`；实际部署覆盖文件备份：`/home/jt/xiaozhi-ubuntu-test/compose.offline.yaml.issue28-20261109T133909Z`。应用同一 Compose 文件组合重建才会保留 `extra_hosts`；宿主机 hosts 不自动传入容器。
- 公网代理配置为 `/home/jt/xiaozhi-ubuntu-test/.runtime/public-entry/nginx-tls.conf`；真实身份块标记 `BEGIN/END ISSUE11 REAL IDENTITY`。原始备份为隔离目录内 `original-nginx.conf`，启用通知诊断前备份为 `nginx-before-notify-log.conf`。
- 诊断日志在容器 `xiaozhi-ubuntu-test-public-entry-public-entry-1` 的 `/var/log/nginx/issue11-notify.log`。下一轮应按时间区分诊断请求和真实通知。
- 本次整理没有停止服务或删除数据库，定时刷新仍可能运行；用户暂停的是人工联调。旧二维码是限时 fixture，不能作为长期产品入口。恢复测试前检查服务状态并生成新请求。
- 结束联调时，只移除本次标记路由和通知专用日志配置，`nginx -t` 通过后 reload；确认网络无其他用途后再断开临时网络，按需停止 `issue11-identity`。保留数据和加密密钥。不要用旧备份覆盖后来增加的配置，也不要停用原业务服务。

相关资料：[身份模块运行说明](../development/customer-identity.md)、[#29 可控外部服务回归](2026-11-09-customer-identity.md)、[#11](https://github.com/yanghan24/xiaozhi/issues/11)、[#28](https://github.com/yanghan24/xiaozhi/issues/28)。
