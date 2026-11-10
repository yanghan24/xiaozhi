# 原账号恢复与集市大模型（#13）

2026-11-10 用户决定当前阶段不接智能体，直接调用 AI 集市 `qwen3.8-flash`，继续使用 Edge TTS。账号、授权和额度仍以集市为准；不因缺少余额阻止设备绑定，也不把资源调用失败解释为退出。此前“所选智能体待定”和阶段性全部 blocked 的描述由本切片补充，历史验证范围不变。

## 手机确认与许可

开启产品语音后，可信身份和本次有效授权下的首次手机确认可以建立归属并启用本台许可。缺少 Key/Refresh 的可信身份仍可绑定，但保持 blocked。原账号扫描 resume 请求并登录后仍未恢复；必须点击“恢复本台使用”，并通过当前归属、请求、设备凭据、许可版本与授权代际检查。account 登录、页面读取、历史成功重试均不能恢复许可。

单台退出保留归属与共享授权，推进本台许可版本并停止本台调用。共享授权进入 REFRESH_UNCERTAIN 或确认无法恢复时，所有依赖它且仍启用的设备变为 pending/required，原先已退出的设备不变。原账号重登并确认 A 只恢复 A。正常 Access Token 刷新不撤销独立有效的业务 Key；刷新结果未知/失败与 Key 到期会停用许可。

控制状态在 allowed 时签发 60 秒租约和最长 300 秒的随机语音令牌，令牌绑定设备、独立接入凭据、归属代际和许可版本。临近到期更换令牌；令牌到期拒绝新连接，不直接结束仍有当前许可的已建立连接。

## 服务之间的边界

Python 语音服务通过私网 `/xiaozhi/internal/device-voice/v1/{open,check,close,model-access}` 查询 Java 权威状态，使用独立 `X-Voice-Service-Key`。该路由不向公网代理开放。`model-access` 仅向已认证的语音后端提供当前归属账号的 Key；不把 Key 放入设备令牌、手机页面或握手日志。

产品 WebSocket 只接受精确 `/xiaozhi/v1/` 路径、Device-Id 与产品 Bearer 令牌；旧 HMAC 令牌、URL 查询参数、MQTT 回退、旧白名单和 auth.enabled=false 都不能绕过。产品 OTA 只发现 device_control 地址，不签发旧 websocket/activation；旧数字激活返回 403。该 discovery 响应不是固件下载能力，尚未验证实际固件兼容。

正式 ConnectionHandler 对输入、模型请求、后续 TTS 输入和全部文本/二进制发送重新校验当前许可，并由 200 ms 观察任务在失效时终止连接、清理队列。同台多个连接分别受检；一次失败后连接不自行恢复。直连模式不加载旧管理员 LLM、记忆、工具或外部上下文/声纹能力。ASR 保留部署配置，仍需要独立有效的端点与凭据。

模型适配读取 OpenAI 风格 SSE 的 `delta.content`，忽略 reasoning 内容，要求 `[DONE]`，不自动重试资源请求。已经被上游受理的调用可能继续执行/计费；发送前检查无法撤回已经进入传输或设备缓冲区的音频。真实设备停播仍依赖租约、许可版本和固件 ACK，不能用服务器关闭连接代替真机验收。

## 部署与回退约束

本切片未部署公网服务。上线时必须一起更新 Java、Python、代理及支持产品协议的固件：

- Java 设置 `device-control.enabled=true`、`device-control.voice-enabled=true`、正确的 HTTPS public-origin，以及至少 32 字符的独立 voice-service-secret。
- Python 私有配置 `product-voice` 设置 `enabled: true`、`authority_url: http://api:8002/xiaozhi` 和同一个 `service_key`；禁止把该配置提交到 Git。管理端 server-base 也会发送 product_voice_required，缺少私网凭据时语音服务启动失败。
- `device-control.model-base-url` 默认为 `https://aimarket.hncig.cn/v1/`，只允许可信运维配置；它接收真实业务 Key，不能指向不可信地址。测试 harness 单独指定 loopback 受控模型，合成 Key 不发送到真实集市。
- 开关切换必须协调重启；运行时 reload 拒绝产品模式切换。暂停产品语音时保持 device-control.enabled 与 Python 产品模式，只关闭 voice-enabled，确保不退回旧鉴权。读取旧 ready 状态时将其转为 device_disabled，并推进许可版本。
- 已有 device_disabled 记录不自动启用，以免清除真实管理员禁用。此前实验阶段的禁用设备需经明确的管理恢复流程处理，不能靠升级批量打开。

保留迁移 `202611101100`、归属、许可、操作记录和私有加密密钥；不修改已执行迁移或删除数据模拟回退。若回退到没有产品闸门的 Python 版本，必须先撤下公网语音路由，不能直接重新启用旧鉴权。

## 尚未覆盖

真实集市模型列表已确认存在 qwen3.8-flash。早期账号积分不足；有积分账号授权后，真实模型 SSE、Edge TTS 与正式 WebSocket 音频输出已通过隔离联调，见[真实模型到 TTS 证据](../verification/2026-11-10-live-model-tts.md)。该测试使用私有凭据代理，真实账号与产品设备归属、公网及真机整链路仍未验收。

当前逐帧/逐流片段权威检查会增加 HTTP 和数据库压力，全局短事务屏障的并发容量需 #19 验证。语音令牌与异常断线遗留会话尚无定期清理，部署前须补充保留/清理策略；正常连接关闭删除会话。固件、设备凭据交付、真实 ASR/模型/Edge 和公网停播端到端仍未验收，不关闭 #13 或 #11。
