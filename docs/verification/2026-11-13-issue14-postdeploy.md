# #14 合并后部署与复验

2026-11-13（Asia/Hong_Kong）在 Ubuntu `jt@192.168.168.114` 完成 PR #93 合并后的部署核对、当前运行镜像隔离回归和 B 实机复验。PR #93 的合并提交为 `66d77eeefaae8f7039f7dcb1bf190eb303b4f2f5`，其 head 为 `e98c300daa8192972969e263f169d2b339a376f7`。

## 部署核对

服务器在本轮开始前已经更新，运行中的语音服务为 `xiaozhi-product-voice:ota99-26b1703`，digest 为 `sha256:f931a8ea6c7442019a0ef64254adb377cf855678974db7f9ff3899d5928599f2`，容器自 `2026-11-13T06:11:22.137206008Z` 起运行。API 和管理页也保持运行，digest 分别为 `sha256:292846c40a6279bf720ccc75eed08796eb042b287350b0b13e048d04a9fa500d` 与 `sha256:6f36cacdb1ab2d8eb7234eb8cf07e2f8996be29179e3ea6a7c1f34ac70aaa9c5`。

从正在运行的语音容器提取并核对了 PR #93 的五个源码文件；按 CRLF 归一化后与合并源码一致。当前镜像同时带有后续维护和 ASR 变更，因此本轮没有用旧镜像重建、源码挂载覆盖或重复重启服务。部署详情见 [deployment.json](evidence/2026-11-13-issue14-postdeploy/deployment.json)。

## 当前镜像隔离回归

直接使用上述运行镜像 digest（没有源码 overlay）重新执行了产品单元与 WebSocket 场景。容器使用 `--network none`、只读根文件系统、丢弃全部 capabilities 和合成模型/MCP/ASR/TTS fixture；没有访问真实模型、知识库或设备。70 项产品/ASR 单元测试全部通过，22 个 WebSocket 场景全部通过，失败、错误和跳过均为零。场景包括普通回答、两类知识工具路径、权限/取消/错误路径、启动就绪与超时、三种退出指令和设备工具兼容性。原始摘要及 SHA-256 见 [current-image-summary.json](evidence/2026-11-13-issue14-postdeploy/current-image-summary.json)。

## B 实机复验

用户在同一台已授权的 P4 B 上依次完成唤醒问候、普通介绍题和两道固定知识题，并确认四步均听到完整、可理解的回答。服务器最近窗口的脱敏事件记录显示两轮 `knowledge_start`/`knowledge_complete`/`knowledge_result_hash`，每轮均有对应模型续答和音频发送事件；知识工具调用次数与两道知识题一致。服务器只保留事件元数据和哈希，不保留原始识别正文、工具正文或录音，因此“扬声器实际播放完整”以用户现场确认作为物理证据。详见 [physical-b-confirmation.json](evidence/2026-11-13-issue14-postdeploy/physical-b-confirmation.json)。

本轮证明了合并后的代码已在当前服务器镜像运行，并在 B 上完成普通回答与自动知识库回答的现场复验。服务器隔离回归不替代真实设备的麦克风、ASR 和扬声器证据；本记录也不扩大为逐字工具正文或知识库法规现行性的完整审查。
