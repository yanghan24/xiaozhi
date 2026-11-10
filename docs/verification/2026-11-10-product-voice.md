# 原账号恢复与直连模型验证

日期：2026-11-10。基线 `d70433d`，任务 #13，PR #26。运行环境仅 Ubuntu `jt@192.168.168.114` 的 `/home/jt/issue13-integration`；真实 MySQL、Redis、Java HTTP、Python WebSocket 与 Chromium/Nginx。未更新现有公网业务容器，未连接物理设备。

## 结果

| 验证 | 结果与边界 |
| --- | --- |
| Java 完整回归 | 317 tests，0 failures/errors/skipped；集成守卫通过（新增语音权限 8 项、关闭语音 1 项）。日志 `.runtime/full-tests.log`，完成于 01:52:14 UTC。 |
| 正式 Python 连接链 | 实际 WebSocketServer、ConnectionHandler、模型适配器、Edge TTS 适配器、队列及音频编码器。受控模型回答生成二进制音频；延迟 TTS 返回在退出后被拦截；同台两连接关闭。日志 `.runtime/voice-python-tests.log`。 |
| 鉴权与隔离 | 旧令牌、跨设备令牌、查询参数拒绝；旧 auth 禁用及白名单不绕过。单台退出不影响另台；模型请求在退出后不发出。 |
| 模型响应 | 受控 SSE 只输出 content，不输出 reasoning；积分不足 HTTP 400 后原许可仍有效，可发起下一次请求。外部模型 HTTP 与 Edge 传输为可控替代，未验证真实声音。 |
| 手机尺寸浏览器 | 390×844 Chromium 经 TLS Nginx：绑定、退出、重新扫码登录、刷新仍停用、明确恢复、成功页刷新不重复操作。内部模型凭据接口、管理及未知路由 404。日志 `.runtime/browser-result.log`。 |
| 真实集市最小请求 | 已有私有授权查询 `/v1/models` 确认 `qwen3.8-flash`。`/v1/chat/completions` 返回 HTTP 400“积分不足，请充值后重试”；真实生成/SSE 受阻，未重复发付费探测。 |

Java 新增覆盖：原账号和当前授权代际要求、A/B/C 共享授权不可恢复及未知状态传播、仅目标显式恢复、正常 Access Token 刷新不误停用独立有效 Key、语音令牌过期拒绝握手但既有会话继续、内部接口鉴权/畸形请求/媒体类型、关闭语音后的旧 ready 状态失效及 OTA 不签发旧令牌。

## 失败到通过

- 手机 resume 页面原无按钮，HTTP 断言失败；补显式恢复表单后通过。
- Access Token 到期曾永久关闭设备，回归失败；以独立 Key 和刷新结果判断调用许可后通过。
- voice-enabled=false 曾输出非法 blocked/ready，回归失败；作废旧 ready 并推进版本后通过。
- 内部接口 text/plain 曾落入管理端 HTTP 200；补精确媒体类型回退后返回 400。
- 正式双连接首次 TTS 初始化暴露并发 import 读取未完成模块；改用 Python import_module 自身的导入锁后通过。
- 浏览器模拟设备发恢复请求发现 TLS 代理缺控制路径；补三个精确方法/路径路由后完整流程通过，内部服务接口仍拒绝。

本轮没有运行上游所有 Python 测试；Python 证据为本切片的实际跨服务协议回归。ASR 使用远程适配器初始化，未执行真实识别。

## 双轴复核

Standards：0 硬性违反，1 非阻断建议（封装共享权限事务顺序）。Spec：初次发现普通刷新误停用及 watcher 已结束分支漏清理，两项已修正并复核；最终无阻断发现。代理路由补充经独立复核，无新增阻断。

## 剩余关卡

真实模型积分、有效 ASR、真实 Edge 与物理音箱闭环仍需验证；固件动态二维码、独立凭据写入恢复和停播 ACK 不在本轮完成范围。逐帧权威检查容量和遗留令牌/会话清理策略仍是部署前工作。保留 #11/#13，不把受控回归视为公网部署或真机验收。部署开关、旧禁用设备与回退要求见[产品语音说明](../development/customer-product-voice.md)。
