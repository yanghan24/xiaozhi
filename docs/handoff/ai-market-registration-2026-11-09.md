# 小智应用接入登记信息

2026-11-09。请按集市接口文档第 1 节核对已有应用，未登记时使用以下信息。

| 项目 | 内容 |
| --- | --- |
| 应用名称 | 小智智能音箱 |
| 客户端类型 | `web` |
| 登录回调 `redirect_uri` | `https://bridge.hncig.cn:18443/xiaozhi/customer/oidc/callback` |
| 授权范围 `scope` | `openid profile credentials:manage offline_access` |
| 注销通知地址 | `https://bridge.hncig.cn:18443/xiaozhi/customer/oidc/backchannel-logout` |
| 退出回调 | 不使用 OIDC 全局退出，留空 |
| 浏览器来源 | web 后端应用，不适用 SPA 来源登记 |

请返回实际 Client ID，并通过私密渠道交付 Client Secret。采用授权码 + PKCE S256，客户端认证为 `client_secret_basic`。

**登录回调与注销通知接口尚未部署，当前仅供预登记，部署后再联调。**
