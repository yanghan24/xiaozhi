# 小智应用接入登记信息

2026-11-09。请按集市接口文档第 1 节核对该客户端的以下登记信息。

| 项目 | 内容 |
| --- | --- |
| 应用名称 | 小智智能音箱 |
| Client ID | `app_07a20fc2e1c14995a9f8c2cc668a7aff` |
| 客户端类型 | `web` |
| 登录回调 `redirect_uri` | `https://bridge.hncig.cn:18443/xiaozhi/customer/oidc/callback` |
| 申请范围 `scope` | `openid profile credentials:manage offline_access`，实际获准范围待核对 |
| 注销通知地址 | `https://bridge.hncig.cn:18443/xiaozhi/customer/oidc/backchannel-logout` |
| 退出回调 | 不使用 OIDC 全局退出，留空 |
| 浏览器来源 | web 后端应用，不适用 SPA 来源登记 |

实际 Client ID/Secret 已收到；请核对该客户端的登记地址、获准范围和刷新设置。采用授权码 + PKCE S256，客户端认证为 `client_secret_basic`；这些设置尚未实测。

**登录回调与注销通知接口尚未部署，当前仅供预登记，部署后再联调。**
