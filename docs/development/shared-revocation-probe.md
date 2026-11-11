# #11 共享授权撤销专项工具

`scripts/customer-identity/revocation_probe.py` 是 Ubuntu 运维验收工具，不属于产品单台退出路径。它在令牌有效时核对身份和应用、取得 Key 基线，再用同一轮私有状态发送一次撤销请求，支持等待 Access Token 自然过期后直接测试撤销端点。原成功路径工具保持不变。

## 准备与执行

只用于已确认影响范围的专用测试账号：应用授权和 Key 按账号/应用共享，即使使用独立数据库也会影响该账号同应用的其他终端。CLI 仅连接固定集市 HTTPS 地址，保留 TLS 验证，不跟随重定向，不读取环境代理；真实网络不通时停止。

先由服务端将测试会话写入私有普通 JSON 文件（0600），包含 `access_token`、`subject`、`client_id`、`access_expires_at`（服务端取得的 Unix 秒数）。不保存或手工使用 Refresh Token。客户端配置使用已有私有文件。将以下占位路径替换为 Ubuntu 私有目录中的路径，父目录应为 0700。

```bash
python3 scripts/customer-identity/revocation_probe.py prepare \
  --client-config /private/client.json \
  --private-test-session /private/test-session.json \
  --state /private/round-01.json --confirm-shared-test-account
```

prepare 核对 UserInfo 主体、凭据返回的主体/应用及旧 Key 模型列表 HTTP 200，不执行推理。状态文件含 Access Token 和 Key，使用独占创建、0600 权限及 fsync；不得上传或展示。

待令牌自然过期后执行（不重复 UserInfo 预检）：

```bash
python3 scripts/customer-identity/revocation_probe.py execute \
  --state /private/round-01.json --require-expired --confirm-shared-test-account
python3 scripts/customer-identity/revocation_probe.py observe --state /private/round-01.json
```

有效令牌/故障专项省略 `--require-expired`。该开关检查记录的到期时间，真正是否过期由集市判断。本工具不注入故障；注入须另行限定到专用链路。真实账号测试仍需确认账号和影响范围。

## 结果及恢复

- execute 发请求前独占持久化 `.attempt`。同一路径再次执行或并发执行不会重复发送。崩溃在发出前也会保留标记，以牺牲一轮测试换取不重放。
- 只有 HTTP 200、业务 `code=20000` 且 `data.revoked=true` 才输出 `confirmed_revoked`。401/403、5xx、超时、断连、非法响应均为 `unconfirmed`，不自动重试。无 HTTP 响应不证明请求未送达。
- `.result` 仅含脱敏结果。结果写入失败时 `.attempt` 继续阻止重发；不得删除标记或复制状态绕过保护。仅保证同一状态路径不重复尝试，不是跨复制文件的全局去重。
- observe 只读检查旧 Key，始终提示需要关联证据；401/403 不独自证明授权撤销，200 也不能代替集市对延迟/授权状态的说明。
- 未确认结果先核对集市记录和旧授权事实，核查完成前不重新授权后又用旧轮次撤销。§7.2 Key 撤销重试承诺不套用到 §7.4 应用授权撤销。
- 恢复使用原账号正式重新授权流程，不手工重放刷新令牌、不改库强制 ACTIVE。新轮次重新 prepare，保留旧证据；验收结束按私有凭据规则删除明文会话/状态，保留脱敏结果和尝试记录。

退出码：0 表示该阶段完成（prepare/observe 不表示撤销成功），2 表示撤销结果未确认，1 表示停止或本地异常。输出不含令牌、Key、账号、请求 URL 或上游响应正文。

## Ubuntu 可控验证

2026-11-11，在 `jt@192.168.168.114` 的 `/home/jt/issue11-revocation-probe-20261111` 执行：

```bash
python3 -m unittest discover -s tests -p test_revocation_probe.py -v
```

10 项通过，使用真实 loopback HTTP 服务和虚构凭据。覆盖成功、过期令牌直达 DELETE、未到期阻止、503、断连丢响应、超时、307 不跟随、非法 JSON 结构、重启遗留标记、并发单次发送、身份不匹配、文件权限、只读观察和错误输出脱敏。过期时间用可控时钟推进，不冒充真实等待过期。

没有访问真实集市、撤销真实授权、重启产品服务或操作音箱。真实异常、最终授权事实及重新登录恢复仍按 #11 单独验收。
