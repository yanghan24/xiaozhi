# 服务器侧：二维码绑定协议 v1 交接

2026-11-09。[接口约定](../contracts/device-binding-v1.md)是本次服务器/固件的共同实现依据，[Schema](../contracts/device-binding-v1.schema.json)和[报文示例](../contracts/device-binding-v1.examples.json)固定字段与单位。对应 [#13](https://github.com/yanghan24/xiaozhi/issues/13)，依赖 #11 保留。本次交付文档，不包含服务器或固件实现，也未部署、刷机或运行真实集市请求。

## 取得约定

协议已入主线；当前补充以本 PR 中的协议、Schema、示例和[身份模块接入约定](../contracts/customer-identity-integration.md)为一组审阅。旧分支 `docs/13-device-binding-contract-v1` 仅用于历史追溯。本补充合并后从主线读取；无需切换正在运行服务的源码目录：

```bash
git fetch origin
git show origin/main:docs/contracts/device-binding-v1.md
git show origin/main:docs/contracts/device-binding-v1.schema.json
git show origin/main:docs/contracts/device-binding-v1.examples.json
```

执行前确认仓库 origin 为 `yanghan24/xiaozhi`，并记录实际读取的分支提交。服务端实现从最新主线建独立任务分支/worktree，遵循 CONTRIBUTING.md；不要在已部署的固定提交目录直接 pull、切分支或初始化数据库。文档合并后也可以从主线读取。真实 `.env`、管理员 token、设备凭据和客户授权均继续留在私有配置中。

## 服务器侧实施清单

1. **准入与状态。** 建立预登记设备与独立凭据校验、归属、本地调用许可、revision/permission_version、600 秒请求、浏览器 flow、幂等结果及状态 ACK。归属、版本变更和幂等记录在事务中处理，数据库保障单设备唯一归属。逐设备凭据物理写入/恢复另与固件交付方落实，不以 MAC/UUID 白名单替代。
2. **设备接口。** 实现 `/xiaozhi/device-control/v1/` 下的 state 长轮询、requests、state-acks；使用专用设备认证及本文封装。Shiro 配置匹配的是去掉 `/xiaozhi` context 后的路径，不误入管理员 userFilter，也不匿名放行。blocked 不返回语音 token；allowed 返回有界本地许可和设备专属短期 WSS 参数。
3. **手机接口。** 复用 #29 的二维码页、客户绑定 flow、固定 OIDC 回调与后通道通知，新增 v1 适配、手机 confirm、account 请求的 open-account、单台 sign-out 和操作状态查询。客户 Cookie/CSRF 与运营后台分离；多标签页按 flow_id 区分目标。回调只确认身份，手机确认后才绑定/恢复，不要求音箱确认。
4. **单台退出闭环。** 保存本台停用、推进许可版本，通过可靠跨实例事件阻止本台新资源调用及所有连接输出，唤醒长轮询，等待匹配 ACK。区分“停用已保存”和“设备已确认停播”；离线不伪报执行。共享 Key/Refresh Token/OIDC 全局登录不受该操作影响，恢复也只启用本次设备。
5. **堵住旧入口。** 产品 OTA 当前能先发语音 token，Python 语音也有关闭认证/白名单/query 回退路径；必须一并施加准入和当前许可验证。只新增安全绑定接口而保留旧 token 绕过不算完成。旧协议升级通路与产品语音权限分别控制，不通过禁用 TLS/认证完成联调。
6. **公网代理和配置。** 使用带实际端口的 PUBLIC_ORIGIN 生成二维码、回调、控制和 WSS 地址，精确放行所需设备/客户/OIDC 路径，支持 20 秒长轮询及 WebSocket，保留可信外部 authority。现有 LAN 模板只允许旧 OTA/WS/readiness，不能直接替代新公网配置；运营页面、配置和数据库继续按独立权限控制。受控固件下载另按 #18 设计。
7. **回归与真实验收。** 按协议第 9 节以及 #13 A01–A22 验证认证过滤链、幂等/并发/过期、两台音箱隔离、退出/恢复/重启、旧令牌及非 443 入口。自动回归可在集市边界使用确定性响应，但实际手机与音箱结果分别记录。服务端与跨服务运行在已有 Ubuntu 隔离环境，复用已有配置和数据卷。

## 已实现身份模块与产品适配

#29 已由 PR #31 交付并关闭，HTTP/浏览器/重启等可控回归通过。#13 不重写身份引擎：新增 v1 适配、区分可信身份与调用授权、落实 REFRESH_UNCERTAIN 到 blocked/market_auth_pending 的传播和仅目标恢复。详见[接入约定](../contracts/customer-identity-integration.md)。固件须识别新 reason 并按现有 blocked 保护，不沿用旧 Schema。

## 固件侧配套交付

- 声明 device_control v1，以受控配置授权的 HTTPS OTA/目标 origin 完成发现，再以独立凭据访问三个控制接口；旧 HTTP OTA 不建立信任，不跟随重定向转发凭据。
- 新增后台长轮询/租约处理、按版本应用状态和停播 ACK，覆盖按键/唤醒、MCP、旧缓存和重连。
- 显示完整 verification_uri 的二维码、期限和状态；未绑定为 bind，退出/需授权为 resume，账号菜单为 account。二维码过期隐藏，状态恢复后收起；首次绑定不新增音箱确认按钮。
- 修正/验证 WSS 握手 Host 的外部非默认端口，保留有效证书校验；处理已有 NVS OTA 地址优先级与 MQTT 回落。
- 重新构建核对 OTA 槽大小。当前源配置为 16 MB，附带应用槽余量约 46 KiB；实物容量与未来分区方案待核实，不在本交接中改为 32 MB 或授权刷机。

## 仍待输入

已有公网域名/端口、证书、映射、身份内网解析及真实登录证据，实施者通过项目资料和受控交接取得所需访问权限；运维/集市仍需落实反向通知可达性。集市：实际客户端登记和获准范围，核对已有协议与私有配置，记录差异和缺失的证据或访问条件。固件/交付：正式逐设备凭据写入、保留和恢复、实物板型/Flash/revision。客户会话闲置期限已确认为 30 分钟，其真实验收仍按 #11 落实，不把 60 秒设备技术租约当作客户登录期限。

本期接入 [MCP 知识库](../planning/mcp-knowledge-scope-2026-11-10.md)，智能体接入留待后续需求；默认 Edge TTS，不等待或切换集市 TTS。可以准备接口和可控响应回归，但不能以此解除 #11 依赖、勾选真机验收或关闭 #13/#14。实现遇到必须改变协议的事实时，先更新本文链接的契约与 Schema/示例，双方同步版本后再实施，避免各自改字段。
