# #12 Ubuntu 最小契约签收

2026-11-13 09:57–10:00（Asia/Hong_Kong），环境 `jt@192.168.168.114`。按[执行单](../handoff/2026-11-13-mcp-minimal-server-check.md)核验 PR #86 的 `830b11a30550de9533065706c1c9d275484b4cbb`，确认测试范围、复核历史原件并完成一次新的真实 MCP 查询。当前五项最小交付已满足；PR 的合并、部署和 #14/#19 的模型／音箱验收分别处理。

## 目标与版本

用户在本次会话直接确认：“确认使用 B 及其当前归属测试账号，按上述范围测试”。确认对象是现有 P4 B、当前归属测试账号、现有 MCP 配置，以及 `service-669a396c73` 的 `ask_knowledge` 固定公开问题。以既有 B 维护目标和只读 `dc_device` 查询建立真实对应，受控登记为 `/home/jt/issue12-minimal-verification-20261113T015751Z/scope-private.json`（0600）；共享标签为 `P4-B` / `B-current-owner-20261113`。

B 为 `ready / admitted=1 / local_enabled=1 / permission_version=24 / ownership_version=7`。调用前后归属、许可及 MCP 配置完全一致。本登记仅确认这组对象的测试范围，未重新验证模型／OIDC 授权或建立多客户映射。#14 每次调用仍检查当前归属和许可，不把本次快照作为永久放行条件。

| 项目 | 本次实际核对 |
| --- | --- |
| 候选 | `830b11a`；14 个受测文件内容与 Ubuntu 历史源码一致 |
| API | `xiaozhi-product-api:1b50709`，镜像 ID `429f8577…` |
| 运行语音 | `xiaozhi-product-voice:pr79-f830a8c`，镜像 ID `19070d27…`；MCP 模块仍为旧代码，候选未部署 |
| 测试镜像 | `sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5` |
| Python / MCP SDK | `3.10.22` / `2.3.0`；已安装依赖与历史报告一致 |
| 配置 | `/home/jt/.private-mcp/config.json`，0600、uid 1000；SSE 地址符合契约，Authorization 已设置 |
| 真实服务／协商协议 | `service-669a396c73` **4.32**；真实 SDK 协商 **2025-06-18** |
| 工具 | `ask_knowledge`，独立版本未暴露；Schema 仍仅接收 1–300 字的 `question` |

历史标准库发现为服务 4.31／协议 2024-11-05。本次服务版本已变，SDK 协商结果也不同；两次使用不同客户端，不把协议差异单独推断为服务兼容策略。当前接线以本次观察为准。现网 API 和语音在本轮操作前后镜像与启动时间均一致。

## 原件与回归核验

[audit.json](evidence/issue-12/server-20261113/audit.json)记录原件路径、实读 SHA-256、版本和逐文件比较。取回的 [installed01-result.json](evidence/issue-12/server-20261113/installed01-result.json) 与 [installed02-result.json](evidence/issue-12/server-20261113/installed02-result.json)保留服务器原字节：两轮各 **81/81**，fail/error/skip 均 0、`guard_denials={}`，各 81 个唯一用例。两份文件内容相同，SHA-256 均为 `e7411021ae983b15d4fdc5e53eb2c22dea109f37a970a65af824ca221ccb48ba`；两轮是同一套 81 项。

14 个服务器文件的 raw hash 均对应两份报告的 `source_sha256`，LF 归一化后均与固定候选一致。服务器 `errors.py` 和回归脚本为 LF，其余 12 个文件为 CRLF；仓库历史清单使用全部 CRLF。服务器生成的 `test-manifest.json` 和仓库历史清单分别保留，判定依据为实际逐文件内容和报告关联。

测试镜像及六项依赖版本一致，因此复用两轮结果，本次未重跑 81 项。多数传输／会话为合成，真实 SDK 内存流覆盖超时分支；这些结果不能证明真实远端取消、费用或设备停播。

历史发现和正常查询原件的 hash 均与旧文档一致。两份 A2 报告则与旧文档哈希不一致，保留差异：

| 报告 | 本次实读 SHA-256 | 重新核对结果 |
| --- | --- | --- |
| `issue12-a2-20261112/run01/result.json` | `a8be4d736a5a3711859ed07216cd91217deda9a07993578f6480ec160c057d31` | 无关问题仍 ok／5 片段，三项各一次提交，清理完成 |
| `issue12-a2-20261112/errors01/result.json` | `b5262ba7dc487997f561f6ee6a3e339816a5f0fbecbf84113977fef9407aac43` | 301 字及空输入各一次，均 `MCPError/-32602`，清理完成 |

旧值与新值一起存于 audit.json。现有证据不足以判定是历史转录错误还是文件后来变化，不声称这两份文件历史字节未变。必要输入／错误／超时／关闭后迟到边界另有上述可核验的 81 项回归，本次正常真实查询另有新证据。

## 本次真实查询

09:59:32–09:59:33，复用现有 `ServerMCPClient` 和历史探针入口，提交“怎么申请成立社会团体”。使用原测试镜像、host 网络、只读源码及配置挂载、容器 uid/gid 1000；初始化／调用外层 30 秒、显式 SDK 25 秒、清理 22 秒。探针增加初始化结果记录和完整 SDK 结果的受控保存，未新建传输或工具框架。

- 初始化 1 次、工具提交 **1 次**、重试 **0 次**；返回 `CallToolResult`，`is_error=false`。
- `status=ok / mode=chunks / generated=false / modelCalled=false`，5 个来源、6 个内容块；服务自报 476ms。全过程约 1.45 秒（含初始化和清理），不是模型或语音延迟。
- 清理完成、连接关闭、worker 引用清空；模型／TTS 请求均 0。

[live-summary.json](evidence/issue-12/server-20261113/live-summary.json)为脱敏摘要。原始报告在服务器 `.../live01/result.json`，4,741 字节，SHA-256 `e9965f481ab43c086a44f93077b479d72e9d4bd35a61ce90cbf926c2606b0205`。完整 SDK 结果序列化保存在同目录 `tool-result-private.json`（31,576 字节，hash 见摘要），不是网络抓包；工具正文和资源 URI 留在服务器。远端取消能力仍未知，执行状态不明请求未重放。

## 五项签收与接续

| #12 当前验收 | 结论与依据 |
| --- | --- |
| SSE、Schema、脱敏请求／结果、客户端版本 | 通过：配置元数据、当前协议／服务和 Schema 实查；候选与现网分别记录 |
| 测试范围、配置、凭据分离 | 通过：用户确认及当前 B 归属的受控登记；只使用 MCP 配置，秘密留在后端 |
| 片段 → 模型有据回答约定 | 通过：[契约](../contracts/mcp-knowledge-contract.md)规定错误、相关性、资料不足反馈及结果边界；产品实现由 #14 验收 |
| 正常真实调用与必要边界 | 通过：本次一次真实调用、清理成功；复用已核验的 81 项回归，历史哈希差异单独保留 |
| #14 交接 | 通过：[交接](../handoff/2026-11-12-mcp-knowledge-to-issue14.md)更新了当前版本、范围和证据入口 |

#12 最小契约可签收。候选尚未合并或部署；#14/#19 仍负责模型自动选工具、相关性判断、依据片段续答、Edge TTS、真实播放和停播围栏。#88 保留多客户及完整远端契约。本次没有业务数据库写入、服务重启或物理设备动作。
