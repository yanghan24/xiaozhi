# 客户 OIDC 身份切片（#29）

本切片在现有 Java 管理应用中运行。客户 Cookie 不进入 Shiro 管理身份体系，回调不创建运营账号、不写设备归属或本地调用许可。正式设备请求发行、最终确认及音箱许可传播由 #13 完成。

## 部署配置

应用默认不启用集市连接。设置 `CUSTOMER_OIDC_CONFIG_FILE` 为只读挂载的私有 JSON 文件路径，设置 `CUSTOMER_OIDC_ENCRYPTION_KEY_FILE` 为独立 AES-256 密钥文件路径。两个文件必须为普通文件，权限 `0600`，不能是符号链接。密钥文件内容为 32 个随机字节的 Base64，首次生成后持久保存，不能随容器重建而更换。密钥丢失后，已有加密授权无法恢复，须重新授权；轮换需要另行安排数据重加密。

已交付的实际客户端文件在 Ubuntu `/home/jt/xiaozhi-ubuntu-test/.runtime/ai-market/oidc-client.json`，本实现直接读取该 JSON 的 `issuer`、`client_id`、`client_secret`、`redirect_uri`、`backchannel_logout_uri`，不要求重新提供凭据。文件中申请的 scope 不等于集市实际批准的 scope。不要把该文件复制进镜像或 Git。

固定公网来源为 `https://bridge.hncig.cn:18443`。回调为 `/xiaozhi/customer/oidc/callback`，通知为 `/xiaozhi/customer/oidc/backchannel-logout`。配置严格检查这两个完整地址。生产 issuer 固定为 `https://aimarket.hncig.cn/oidc`；仅允许 `127.0.0.1` HTTP issuer 供隔离测试。发现文档的端点必须与 issuer 同源，网络请求不跟随重定向。

将 `deploy/device-entry/nginx-tls.conf` 挂载到独立 TLS 入口的 `/etc/nginx/conf.d/default.conf`，将 `customer-routes.conf` 挂载到 `/etc/nginx/customer-routes.conf`；保留现有证书挂载及 TCP 18443→443 映射。后端名 `api:8002` 与现有 Compose 一致。先执行 `nginx -t`，再对真实应用检查手机页、回调和表单通知。允许路径按方法收窄，其他客户路径、运营登录、配置及页面返回 404。入口关闭 access log，避免记录授权码和请求句柄。

## 持久化与 #13 接口

| 数据 | 作用 |
| --- | --- |
| `customer_device_request` | 已登记 `ai_device` 的限时请求；保存用途、许可版本、过期和消费状态。本切片没有公开发行接口 |
| `customer_browser` | 不透明 Cookie 的 SHA-256、CSRF、最后主动操作时间；闲置 30 分钟失效 |
| `customer_identity_flow` | 每个标签页独立的请求、浏览器、用途、许可版本、一次性 OIDC 事务及身份结果 |
| `customer_authorization` | issuer+sub 的当前授权代际，加密 Access/Refresh Token 与业务 Key；Key 的空到期时间保留为空 |
| `customer_logout_receipt` | 已验签通知的持久化去重与时间范围，供回调和重启后的通知处理使用 |

设备请求由 #13 的认证设备接口写入，不能通过匿名手机入口生成；设备状态变化时必须原子消费/失效旧请求。最终确认仍须在 #13 的设备事务中重新校验真实设备归属、准入代际和当前许可版本，不能仅凭 flow 中的快照修改设备。

`GET /xiaozhi/customer/binding-flows/{flow_id}` 仅接受原浏览器，返回脱敏身份状态、设备显示名、用途、请求状态和授权状态；不会延长闲置时间。页面扫描和创建 flow 不消费设备请求。`POST /xiaozhi/customer/binding-flows` 接受 JSON `{request_handle}`、`X-CSRF-Token` 和精确 Origin；手机页面使用等价表单。成功返回 `data.flow_id`、`data.authorization_url`。回调只将 flow 置为 `IDENTITY_VERIFIED`，清除临时参数后显示等待确认。它不是设备已绑定/恢复的证明。

授权状态：`ACTIVE`、`REFRESHING`、`REFRESH_UNCERTAIN`、`REAUTH_REQUIRED`。#13 必须读取当前 `generation` 和状态，不能凭旧 flow 的授权快照恢复设备。`REAUTH_REQUIRED` 表示收到 invalid_grant，或已确认轮换成功但新响应经三次保存尝试仍无法持久化，当前刷新链不可恢复；须按已确认产品规则停用关联本地调用许可。`REFRESH_UNCERTAIN` 表示响应或崩溃结果不确定，不得把网络/5xx 当作集市退出，也不得删除仍独立存在的业务 Key。补充服务端消费接口时须保留这些含义。

刷新提前 30 秒触发，数据库条件更新跨实例抢占，发请求前提交 `REFRESHING` 并清空旧刷新令牌的可重用存储；成功持久化新的轮换令牌后恢复 ACTIVE。响应丢失和崩溃遗留不重发旧令牌，超时遗留变为 UNCERTAIN。收到成功轮换响应后，保存失败只重试持久化同一份新响应，最多三次；仍失败时标记 REAUTH_REQUIRED，不把共享 Key 说成已撤销。新登录使用数据库流程顺序和授权代际保护，旧回调/旧刷新结果不能覆盖新授权。

后通道单独使用 Nimbus LogoutTokenValidator 并检查时间、events、jti、sid/sub、禁用 nonce。sid 优先，否则按 issuer+sub；会话失效与共享授权状态分开。短数据库锁只覆盖身份落库与通知处理，不覆盖外部请求，防止“通知先到、回调后到”恢复旧身份。重复 jti 幂等 204。无效通知返回 400，不修改会话。

## Ubuntu 自动回归

按 `compose.tests.yaml` 使用独立项目名和测试 `.env`。该配置仅在独立 MySQL 启用触发器 fixture，用于真实保存失败测试，不修改部署数据库配置。

```bash
docker compose -p xiaozhi-identity-tests -f compose.tests.yaml up -d --wait mysql redis
docker compose -p xiaozhi-identity-tests -f compose.tests.yaml run --rm api-tests \
  mvn -B -ntp test -DskipTests=false -Dtest=CustomerIdentityHttpTest,CustomerIdentityRestartTest
docker compose -p xiaozhi-identity-tests -f compose.tests.yaml run --rm api-tests
```

测试使用真实 HTTP/Shiro、MySQL/Liquibase 和 Redis 环境；只有集市 HTTP 边界由 `ControlledMarket` 替代，时间使用注入的 Clock。SQL 只用于设备/请求 fixture 和真实数据库失败条件，断言通过 HTTP 或外部集市观察。重启测试启动、关闭并重建真实 Spring 应用，并运行双实例刷新竞争。

浏览器测试在测试容器的 `/build` 编译并启动 `CustomerBrowserHarness`：

```bash
mvn -B -ntp test-compile dependency:build-classpath -DincludeScope=test -Dmdep.outputFile=target/browser-classpath.txt
java -cp "target/test-classes:target/classes:$(cat target/browser-classpath.txt)" \
  xiaozhi.modules.customer.CustomerBrowserHarness
```

它只存在于测试 classpath，为真实应用预置三个请求，把句柄写入 `target/browser-fixture.json`，并启动可控集市授权页。正式 JAR 不包含这些类或匿名测试入口。浏览器须与测试 API 共用网络命名空间，才能访问可控集市的 loopback 地址。独立 Nginx 使用上面的真实配置，仅将容器监听端口改为 18443、后端名改为测试 API；使用隔离自签名证书，不能据此声称已验证公网证书。

在 Ubuntu 安装该脚本固定版本的依赖后执行：

```bash
npm --prefix scripts/customer-identity ci --ignore-scripts --no-audit --no-fund
FIXTURE_FILE=/path/to/browser-fixture.json EVIDENCE_DIR=/private/evidence \
CHROME_PATH=/path/to/chrome-headless-shell BROWSER_PROXY_IP=TEST_PROXY_IP \
node scripts/customer-identity/check-browser.mjs
```

脚本使用真实页面按钮完成允许/拒绝和同浏览器多标签页，检查清理后的 URL、Cookie、代理拒绝和不泄露凭据。`BROWSER_PROXY_IP` 只用于隔离浏览器的域名映射；产品 Origin 和 redirect_uri 仍是完整公网来源。

## #11 真实集市验证

2026-11-09 已完成真实登录、刷新、共享撤销、拒绝与恢复，见[真实验收记录](../verification/2026-11-09-real-market-identity.md)。有效后通道通知尚未送达，下一步见[管理员检查清单](../handoff/2026-11-10-market-logout-notification.md)。下述步骤用于后续复验，不再表示全部尚未执行。

接通 #28 的环境与登记后，使用专用集市测试账号从真实有效请求扫码，复核实际批准 scope、允许/拒绝、刷新、通知及客户端配置。不能将本切片的可控集市结果当作真实集市或真机验收。

共享撤销工具仅为 `scripts/customer-identity/verify-shared-revocation.py`，没有产品 HTTP 入口。运维从专用测试账号的受保护验收会话准备 `0600` JSON（`access_token`、`subject`、`client_id`），不从客户页面导出。显式指定 `--client-config`、`--private-test-session`、`--confirm-revoke-test-subject` 后，工具先核对 UserInfo 和应用，检查 Key 可访问模型列表，再撤销该用户在该应用的共享授权，验证同一 Key 被拒绝；只输出 HTTP 状态，不打印凭据，不发起模型推理。该动作影响此测试账号在同一应用的所有终端，不能用于单台音箱退出。

## 回退

新增表与旧服务兼容；回退应用和代理允许路径即可停止新流程，保留表及加密密钥，不能用 Git 回退数据库或撤销外部已发出的授权。部署前须备份私有密钥并安排授权数据的受控恢复。#13、硬件验收、真实集市验收和规范通流式调用不在本次完成范围。
