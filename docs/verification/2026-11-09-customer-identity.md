# #29 客户身份切片验证

基线：`8091f87b8c1fe6d18da1487ae23ff9746c6f8fc4`。任务分支：`codex/29-customer-oidc`。验证日期：2026-11-09。

在 Ubuntu `192.168.168.114` 的独立源码 `/home/jt/xiaozhi-issue29`、`issue29-test` 网络和独立 MySQL/Redis 中运行；没有修改既有部署容器或数据库。Java 21、Maven 3.9.9，真实 Spring HTTP/Shiro/XSS/Liquibase；外部 AI 集市为本地 HTTP 可控替代，不是对真实集市的验收。

## 结果

| 检查 | 结果 |
| --- | --- |
| Java 全套 `mvn -B -ntp test -DskipTests=false` | 239 项通过，0 失败、0 错误、0 跳过 |
| 集成测试执行守卫 | 原有三组及新增 HTTP 32 项、重启 1 项均实际执行 |
| 身份与手机请求 | 有效/未知/过期请求、CSRF、多标签页、五分钟 OIDC 与 30 分钟闲置、轮询不续期 |
| OIDC | PKCE S256、Basic 客户端认证、固定回调、nonce/签名/issuer/audience/有效期/UserInfo.sub、拒绝/重放/跨浏览器/网络错误 |
| 授权生命周期 | 空 Key 到期时间、刷新轮换、双实例竞争、响应丢失、5xx、invalid_grant、真实 MySQL 保存失败、新旧授权隔离 |
| 注销 | 无 Cookie 的签名表单通知、sid 定位、错误 claims/签名无副作用、持久化去重、通知与回调竞态 |
| 重启 | 真实应用关闭重建、身份结果/授权/通知去重保留、双实例只兑换一次旧刷新令牌、闲置到期继续生效 |
| 独立 Nginx | 真实配置预检通过，准确允许手机/OIDC 路径，运营与未知路径 404 |
| Ubuntu Chromium 142 浏览器 | 允许/拒绝按钮、同浏览器多标签页、干净结果 URL、Secure/HttpOnly/SameSite Cookie、CSRF 和代理拒绝通过 |

浏览器使用 390×844 视口、独立自签名 TLS 入口和仅用于测试的 DNS 映射；地址始终为 `https://bridge.hncig.cn:18443`。未据此验证公网证书、网络出站登记或真实集市授权。截图只含测试音箱显示名，无账号、Token、Key、Secret 或授权码。

![手机身份已确认](assets/issue29-identity-confirmed.png)

## 回归发现并修复的问题

- 有效设备请求起初仍返回 404；实现服务端请求校验与手机页面后通过。
- 客户接口起初被管理过滤链返回 HTTP 200 错误包装；独立路由和客户验证后，缺 CSRF 返回 403。
- 旧回调晚到会覆盖同一毫秒建立的新授权；使用数据库流程顺序保护当前授权后通过。
- 通知先到、回调后到会恢复会话；持久化通知、短事务锁及流程发起时间判断后通过。
- Chromium 表单在 `no-referrer` 下提交 `Origin: null`，触发 403；手机入口改为 `same-origin`，仍严格校验公网 Origin，允许固定集市的表单重定向后通过。

## 证据与边界

Ubuntu 私有运行目录 `.runtime/` 保存 `customer-test.log`、`customer-restart.log`、`full-java.log`、`browser-evidence/browser-result.json` 和截图。仓库只保存本脱敏报告和截图，不保存完整运行日志或凭据。

复验、配置格式、#13 接入约束和 #11 专用测试账号共享撤销工具见[运行说明](../development/customer-identity.md)。共享撤销工具仅做语法检查，未对实际账号执行撤销。没有进行真实集市登录、规范通推理、设备绑定确认或真机验收；这些仍分别属于 #11/#13 等后续任务。
