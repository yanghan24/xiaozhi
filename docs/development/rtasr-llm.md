# 产品语音切换至 rtasr-llm

2026-11-13 用户要求将小智使用的 AI 集市语音转写模型切换为 `rtasr-llm`。范围是产品语音的 ASR，不改变 `qwen3.8-flash` 对话模型、Edge TTS、设备绑定或账号授权规则。

## 接口与行为

依据当日登录集市读取的[模型详情](https://aimarket.hncig.cn/models)和[原生语音识别文档](https://aimarket.hncig.cn/docs/148621f9-b233-5071-8d24-9d8f4e319a97)。后端使用当前归属账号的业务 Key，在 Authorization 请求头连接 `/v1/asr/xfyun/rtasr-llm`。查询参数为 `model=rtasr-llm&lang=autodialect&audio_encode=pcm_s16le&samplerate=16000`，默认支持中英与方言自动识别。

保留现有逐句录音／说完回答方式；上游使用 WebSocket 并不意味着本次改为边收麦克风边转写。每段最多 60 秒，PCM 仅留在内存，不发布录音 URL、不写文件；按每 40 ms 最多 1280 字节发送 16 kHz、单声道、16 bit 小端 PCM。初始化取得 sessionId 后发送音频，结束发送文本 JSON `{end:true,sessionId:...}`，收到 `data.ls=true` 后才返回完整文本。总处理时限 80 秒，覆盖最长音频的实时发送节奏。

真实集市返回的 `seg_id` 是消息序号，中间结果也递增；`cn.st.type=0` 才是确定文本。忽略临时文本，按消息序号保留确定文本，最终单独的标点帧同样计入。对象和 JSON 字符串两种 data 封装均支持。缺少最后一帧、空文本、功能异常、业务错误和超时均失败，不将部分识别结果送给对话模型。

握手只允许 HTTPS／WSS（测试允许本机 HTTP／WS），拒绝重定向。发送前与收取结果时检查当前许可，另有 200 ms 观察任务处理上游沉默期间的失效；取消、断线、新录音和退出会关闭连接。仅明确 HTTP 401 使用原有持久失效上报路径；上游厂商错误码、403、额度不足和网络错误不解释为撤销集市账号授权。不自动重试付费请求。

模型详情当日显示 500 积分／小时、预扣 100 积分；实际结算由集市负责。本次未改变计费逻辑，也未开通额外语种／说话人分离功能。

## 验证

Ubuntu `jt@192.168.168.114` 上，以现网 `xiaozhi-product-voice:pr79-f830a8c` 为基础，只在隔离测试容器覆盖 ASR 模块。测试入口：

- `scripts/test_product_rtasr.py`：真实本机 HTTP／WebSocket 边界，原生消息、完整结果、取消、超时、权限失效、401、非鉴权错误、无效音频。
- `scripts/check-product-asr.py`：真实 Java 权威服务、正式设备 WebSocket／ConnectionHandler、Opus 解码与 ASR → 模型 → TTS；替代集市及 Edge 传输。
- `scripts/check-product-key-rejection.py`、`scripts/check-product-rejection-delivery.py`：ASR 握手 401 沿用跨连接隔离和持久上报验证。

先用原实现运行新协议测试失败，再替换传输实现通过。真实集市首次调用暴露消息序号包含临时结果，加入相应失败回归并修正后，10 项接口测试通过（包括 40 ms 权威接口延迟的发送节奏回归）。真实集市使用当前获准设备账号的 Key，仅发送 3.408 秒合成语音，返回“你好，小智。请问今天的天气怎么样？”。密钥在 Ubuntu 临时私有文件中传递并删除，未输出／提交密钥、账号标识或客户录音。

真实集市 ASR 和隔离设备链路分别通过，不等同于物理音箱麦克风到扬声器验收。部署及最终验证记录见当前部署入口；用户仍需做一次实机发问确认。
