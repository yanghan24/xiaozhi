# #14 产品普通回答与自动知识库回答：实现及隔离验证

2026-11-13，Ubuntu `jt@192.168.168.114`。本轮完成产品接线和隔离验证，**尚未部署，也未完成真实模型／MCP／Edge 与 B 麦克风、扬声器验收，#14 保持打开**。用户在开发过程中明确将 B 暂时释放给 #18；本轮后续仅完成代码、审查和草稿 PR。

[PR #86](https://github.com/yanghan24/xiaozhi/pull/86) 已按管理员指令于 10:14（Asia/Hong_Kong）squash 合并为 `289f97a40e1236a4320b0c9988d0e6775d2dc314`。本轮分支基于随后包含 #13 签收的 `642bd3992c9a92ff50f96c7510762ed2a4a73e2d`。实现受审查提交为 `8a9f243`，审查修复为 `c418994`；[源码 SHA-256 清单](evidence/2026-11-13-issue14/source-sha256.json)固定受测文件。

## 产品行为

```text
本台当前许可与归属
  → MarketModel 自动决定
      ├─ 普通问题 → 直接回答
      └─ 获准知识问题 → ask_knowledge 一次 → 检索结果 → 模型续答
  → Edge TTS → 当前轮次音频输出
```

模型访问继续走正式 `model-access`，使用本台归属账号的 Key；没有共享模型 Key。产品知识库配置同时匹配设备 ID 和后端授权主体指纹，显式传入唯一服务，只向模型提供原名 `ask_knowledge` 的最小 Schema；远端同名清洗别名、其他服务、设备工具和插件均不能获得这项权限。具体私有配置结构见[最小契约](../contracts/mcp-knowledge-contract.md#配置方式)。

每轮独立保存输入上下文，工具结果只供原轮次续答。新录音、打断、撤权或断线后，旧结果不能提交历史、发起续答或进入新的 TTS／输出。模型授权等待、MCP 调用前后、TTS 新调用和最终 socket 发送均复核许可／轮次。工具超时只取消本地等待，不自动重放远端结果未知的请求。

`chunks/generated=false` 的结果交给模型判断相关性与充分性；提示明确禁止将工具数据中的指令当成规则。协议成功、来源数量或 `status=ok` 不代表内容相关。工具错误、无知识库范围及空模型回答都有明确反馈。初始化失败时仍可普通问答。设备 MCP 发现与独立维护通道继续保留，设备工具不加入产品模型工具列表。

## Ubuntu 验证

| 检查 | 结果 | 证据及边界 |
| --- | --- | --- |
| 产品测试完整集合 | 51/51，失败／错误／跳过均 0 | 模型 8、知识库 16、音频 12、已有维护通道 15；[摘要](evidence/2026-11-13-issue14/unit-summary.json)、[原始输出](evidence/2026-11-13-issue14/unit-tests.txt) |
| #86 MCP 已安装 SDK 回归 | 81/81，失败／错误／跳过均 0 | [摘要](evidence/2026-11-13-issue14/mcp-summary.json)；真实 SDK 数据类型，替换传输，不访问真实服务 |
| 产品 WebSocket 集成 | 10 个场景通过 | [摘要及原始日志哈希](evidence/2026-11-13-issue14/integration-summary.json)；真实产品入口、SDK SSE、工具处理器、模型适配器、Edge 适配器和 Opus 输出，外部服务均为 fixture |
| 既有语音与 ASR | 全脚本退出 0 | [摘要](evidence/2026-11-13-issue14/legacy-voice-asr-summary.json)；真实 Java 权威服务和生产 Python 链，外部模型／ASR／Edge 受控，包含普通音频回答与迟到输出拒绝 |
| Key 拒绝及通知交付 | 401、503、timeout、lost-ack 均通过 | [摘要](evidence/2026-11-13-issue14/key-rejection-summary.json)；每轮新建隔离测试 fixture |

10 个集成场景分别为自动查库续答、普通问题不查库、其他归属账号无工具、工具错误、重新录音丢弃迟到结果、打断后新问题隔离、停用后拒绝迟到调用、空回答反馈、初始化失败后普通回答，以及设备 MCP hello／工具列表兼容。前 9 个在 `8a9f243` 对应源码上通过；最后一个在 `c418994` 修复上通过。修复仅将设备发现后的刷新改经公共处理器方法，没有改变前 9 个场景的调用路径。

回归保存了实际失败再通过的证据：基线产品入口不查库；`listen/start` 后旧查询仍续答；空模型结果没有语音反馈；设备工具列表回包访问缺失 `tool_manager`。对应错误和原件位置列于集成摘要 `red_evidence`，不是通过跳过或预期失败隐藏。

产品测试和新集成使用镜像 `sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5`，Python 3.10.22、MCP 2.3.0，容器 `--network none`，只读源码和临时运行目录，不挂载生产配置。新集成输入为 `listen/detect` 文本；其声音 fixture 仅证明编码及 socket 输出，不证明真实回答内容或实际扬声器播放。

既有语音／ASR／拒绝回归使用独立 `issue13-integration` 网络、测试 MySQL／Redis 和每轮新 Harness fixture，无宿主机发布端口。本轮复用固定历史编译 Java Harness，未重新运行 Java 全套。测试容器已恢复停止。未访问 B、业务数据库、真实模型／知识库／Edge 或部署服务。

## 复现与原件

新集成入口为 `scripts/check-product-knowledge.py <scenario>`，场景名为 `happy ordinary other-owner tool-error listen-start abort-new-turn revoked empty-answer init-failed device-tools`。运行须遵守 Ubuntu 测试策略；脚本从 `/app` 导入生产模块，需要镜像自带配置、音频依赖和 VAD 模型，以及可写的临时 `data`、`tmp`、`config/assets`。外部 HTTP fixture 只绑定容器 loopback。

产品测试使用 `main/xiaozhi-server/tests/test_product*.py`，仅加载这些文件，避免 unittest 递归扫描测试目录后以 `tests/plugins_func` 遮蔽生产包。知识库测试所需的全局配置为固定 `.invalid` 服务 fixture，详见测试文件说明。准备期间的一次导入错误保留在原始记录中，51 项结果来自修正测试加载路径后的完整执行。

原件保留在服务器：

- `/home/jt/issue14-integration-20261113/results/`：新集成各轮、源码归档哈希；`units-20261113T024105Z` 为产品测试完整结果。
- `/home/jt/issue14-model-20261113/`：模型切片、拒绝交付与真实 Java 权威服务集成原件，摘要逐项保存哈希。
- `/home/jt/issue14-knowledge-20261113/`：知识库切片及 `regression/run2` 的 81 项结果。
- `/home/jt/issue14-audio-20261113/`：音频七轮红绿与 `full02` 的 12 项结果。

## 审查及剩余验收

两名独立上下文审查者分别执行 Standards 与 Spec。Standards 初次发现一项 P2：产品处理器没有 legacy `tool_manager`，但设备工具发现仍直接访问它。已改用公共刷新接口，并用设备协议回归复现后修复；复核未解决发现为 0。Spec 初审及修复复核均为 0 项代码发现，明确保留真实设备验收缺口。

等 #18 归还 B 后，先核对当时固件、当前归属账号、许可与服务器版本。沿用已获准的 B／原测试账号范围；如果实际归属变化，不以旧映射给新账号开放知识库。部署前固定候选镜像和原语音镜像的恢复方式，保留系统提示词、私有配置、维护通道及 API／数据库。

真实验收仍须在同一 B 完成普通问题，以及预先固定的“怎么申请成立社会团体”“境外非政府组织的登记和备案”两道自然问题；逐轮关联麦克风识别、模型自动选工具、真实检索、基于相关资料续答、TTS 和完整扬声器播放，记录首段／完成耗时与固件／服务版本。此前 #12 的直接工具证据和本轮 fixture 成功不能替代这些结论。#14 不自动关闭，#19 的 OTA 闭环不属于本次实现。
