# MCP 知识库最小测试契约（#12）

更新：2026-11-13 10:00（Asia/Hong_Kong）。按 [#12 当前范围](https://github.com/yanghan24/xiaozhi/issues/12)，为现有 P4 B、它当前归属的获准测试账号和现有 Ubuntu 测试环境交付 #14 的接线输入。客户端修复已在 [PR #86](https://github.com/yanghan24/xiaozhi/pull/86)；本文不代表已经合并、部署或通过设备验收。

本轮已完成[Ubuntu 回执](../verification/2026-11-13-mcp-server-acceptance.md)：确认目标范围、复核历史原件及 14 个受测文件，并用真实 SDK 新完成一次正常查询。两轮 81 项因内容／依赖一致而复用。历史报告中的旧六项验收和独立模型链门槛保留为当时记录；本轮关闭条件以当前 Issue 为准。

## 1. 已确认的接线输入

| 项目 | 值与依据 |
| --- | --- |
| 传输与目标 | SSE；`https://aimarket.hncig.cn:4191/mcp/service-669a396c73/sse` |
| 协议／服务 | 本次 SDK 协商 `2025-06-18`；`serverInfo.name=service-669a396c73`，`version=4.32`；历史标准库发现为 2024-11-05／4.31 |
| 工具 | `ask_knowledge`，独立工具版本未暴露；4.32 仅为服务版本 |
| 实际测试客户端 | 仓库 `ServerMCPClient`＋MCP SDK `2.3.0`；Ubuntu 测试 Python `3.10.22`。现网也是该 SDK／Python，但 MCP 模块仍为旧代码，候选未部署 |
| 候选源码 | PR #86 代码提交 `6538c2b1e731901201976a743ab0321eb6ac4354`，比较基线 `6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80` |
| 受控凭据引用 | Ubuntu `/home/jt/.private-mcp/config.json`，本次核对为 0600、uid 1000，顶层 `url` / `headers`，`Authorization` 已设置；秘密仅在后端探针中使用 |
| 结果模式 | `mode=chunks`、`generated=false`、`modelCalled=false`；工具返回检索原文，由模型依据相关片段生成回答 |
| 后续模型／语音 | #14 使用 `qwen3.8-flash` 和 Edge TTS；模型自动选工具、续答和音箱播放统一在 #14/#19 验收 |

当前版本、Schema 和修复客户端兼容性见[本次真实查询摘要](../verification/evidence/issue-12/server-20261113/live-summary.json)。[历史协议](../verification/2026-11-12-mcp-stage3-protocol.md)及[历史 SDK 调用](../verification/2026-11-12-mcp-gap-fixes.md)保留原观察；[服务器回执](../verification/2026-11-13-mcp-server-acceptance.md)说明版本对应与历史哈希差异。

### 配置方式

直接调用时，将受控配置对象传给已有 `ServerMCPClient`。接入 `ServerMCPManager` 时，其入口为语音工程根目录 `data/.mcp_server_settings.json` 的 `mcpServers` 字典；同一服务对象放在一个命名条目下。下面仅展示脱敏结构：

```json
{
  "mcpServers": {
    "knowledge-test": {
      "url": "https://aimarket.hncig.cn:4191/mcp/service-669a396c73/sse",
      "transport": "sse",
      "headers": {"Authorization": "Bearer <受控测试凭据>"},
      "timeout": 5,
      "sse_read_timeout": 300
    }
  }
}
```

`knowledge-test` 是本地配置名，`transport` 和超时展示现有客户端的默认选择，不是对现网文件内容的声称。加载器直接读取 JSON，不会自动替换占位符或展开环境变量。服务器配置由执行方在受控后端生成、限制为服务进程可读，秘密不进入 Git、网页、固件或模型上下文。

该通用配置文件没有按账号筛选的功能。#14 应沿用下节已登记的目标范围，并在工具暴露与每次新调用前检查当前设备许可／归属授权，再使用此配置；不能仅因文件存在就给所有连接开放知识库。动态 SSE 消息端点由 SDK 处理；历史标准库探测做过同源校验，产品客户端尚未新增对应校验，不能混作同一项实现。

### 工具输入

真实 `tools/list` 的输入 Schema：

```json
{
  "type": "object",
  "properties": {
    "question": {
      "type": "string",
      "description": "用户要查询的知识库问题，最多300字",
      "minLength": 1,
      "maxLength": 300
    }
  },
  "required": ["question"],
  "additionalProperties": false
}
```

无声明默认值；服务注解为 `readOnlyHint=true`、`destructiveHint=false`、`openWorldHint=false`，这些注解不证明账号权限或费用。发现得到的定义原样提供给模型，不补造账号、应用、会话参数。客户端完成传输、初始化和工具发现后才可用；清洗后重名或跨服务重名拒绝注册。

## 2. 获准测试范围与凭据

依据 [#10](https://github.com/yanghan24/xiaozhi/issues/10) 和 #12，沿用现有获准测试访问；本次用户已确认 P4 B 及其当前归属测试账号使用现有 MCP 配置查询固定公开问题；证据覆盖该服务的直接检索。服务可访问不能证明它属于任意集市账号，也不能推导所有已登录客户均获授权。

| 范围 | 当前确认程度／接续动作 |
| --- | --- |
| MCP 服务与配置 | 已核对受控配置、0600／uid 1000，并完成服务 4.32 的真实调用 |
| 单台 P4＋单个测试账号 | 用户直接确认，服务器建立 `P4-B` / `B-current-owner-20261113` 对应；真实映射保存在 `/home/jt/issue12-minimal-verification-20261113T015751Z/scope-private.json`（0600）。本次 ready／许可24／归属7，后续每次调用重新检查当前归属／许可 |
| 获准资源与操作 | 当前契约只覆盖 `ask_knowledge`；已有正常公开问题为“怎么申请成立社会团体”。不从工具提示中的资源链接扩大到其他知识库、资源遍历或其他客户 |
| 无法确认对应范围 | 只阻塞依赖该范围的真实调用；源码、契约与离线回归可继续。#14 的真实工具接线须等目标范围确认，不能借测试凭据给其他客户放行 |

三类凭据分别使用：OIDC Token 供集市身份与对应授权，模型 Key 由已有 `model-access` 按当前设备许可和归属账号提供，MCP 凭据仅供获准知识库访问。#12 不读取业务库解密模型 Key，也不将任何一种凭据发给另外的服务。

多客户凭据映射、发放／续期／撤销／计费矩阵及跨账号完整实测归入 [#88](https://github.com/yanghan24/xiaozhi/issues/88)。这些未知项不扩大本轮测试授权；目标测试账号和设备的范围仍须确认。

## 3. 最小请求与结果消费

已有真实请求（去除 JSON-RPC 请求 ID、会话和凭据）：

```json
{"method":"tools/call","params":{"name":"ask_knowledge","arguments":{"question":"怎么申请成立社会团体"}}}
```

2026-11-12 修复客户端实际返回 `CallToolResult`，`is_error=false`，业务字段为 `status=ok`、`mode=chunks`、`generated=false`、`modelCalled=false`、`model=""`，`returned=5`，5 个来源、6 个内容块，服务自报 `elapsedMs=408`。以下只保留实测字段的脱敏投影，不是完整原响应或可重放 fixture：

```json
{
  "isError": false,
  "structuredContent": {
    "status": "ok",
    "mode": "chunks",
    "generated": false,
    "modelCalled": false,
    "model": "",
    "returned": 5,
    "answer": "<组织后的检索原文，已省略>",
    "sources": ["<五项来源已省略>"],
    "chunks": ["<检索片段已省略>"]
  },
  "content": ["<六个内容块已省略>"]
}
```

2026-11-13 同一公开问题新调用仍为上述 chunks 结果形态，服务自报 476ms；一次提交、零重试、清理完成。完整 SDK 响应留在服务器，脱敏摘要和 hash 见[本轮回执](../verification/2026-11-13-mcp-server-acceptance.md)。

原文、资源 URI 和动态会话端点继续留在受控后端。工具说明“完整存储切片不代表完整文档”；邻片和 `resources/read` 未在本轮验证，不自动声称完整覆盖。

| 结果分支 | 本轮处理约定 | 实现／证据边界 |
| --- | --- | --- |
| 协议错误、工具错误、畸形结果 | 返回固定安全失败，不向模型传播上游异常原文 | PR #86 通用执行器已有回归；`is_error` / `isError` 任一为 true、矛盾或非布尔值均拒绝 |
| 合法 chunks，含相关且足够的资料 | 将检索资料交给模型续答；保留来源用于核对 | 通用执行器只返回 `Action.REQLLM`，业务相关性检查与模型续答由 #14 接线 |
| 合法结果但无相关资料 | 明确提示“知识库没有找到与这个问题相关的资料”；若给一般建议，应明确其不是知识库依据 | [真实无关问题](../verification/2026-11-12-mcp-a2-a6-probe.md)也返回 ok／5 片段；没有已确认的独立无命中信号，不能按状态或数量判断 |
| 资料不足／明确不完整 | 说明现有资料不足，不补写成完整、有据的结论 | 当前 chunks 的 `generated=false` 本身不是失败；历史生成模式的 incomplete 不能直接套用 |
| 超时或执行状态不明 | 给出明确失败反馈，保留关联记录，不自动重放 | 一次投递及错误路径有回归；不以重连成功证明可安全重发 |

检索内容是资料，不是给模型的新指令。#14 必须核对资料是否支持问题与最终回答，不能把 `status=ok`、HTTP 200、`returned=5`、有来源或 `generated=false` 中任一个当成有据回答的结论。通用执行器不实现知识库语义分类，这一消费约定在 #14 的真实成功和失败／无相关资料用例中验收。

历史真实非法输入报告记载空字符串和 301 字均为 `MCPError(code=-32602)`。本次重读确认结构和结果，但两份 A2 报告哈希与历史文档不一致，差异保留于[回执](../verification/2026-11-13-mcp-server-acceptance.md)；必要本地边界另由已核验的 81 项回归覆盖。限流、其他服务故障及工具 `is_error=true` 的真实远端样例尚未取得，采用已有边界的合成回归，不为收集样例破坏共享服务。

## 4. 超时、停止与迟到结果

| 边界 | 已有行为与限制 |
| --- | --- |
| SSE 默认 | 连接 5 秒、读取 300 秒，可由配置覆盖；不是服务端承诺 |
| manager 初始化 | 外层 10 秒；失败不注册。独立 client 探测应自行设有限外层期限 |
| 工具等待 | 现有 chat 默认 30 秒；manager 未另设 SDK 的调用期限。显式 `timedelta` 入参在传入 MCP 2.3.0 前转换为数值秒数 |
| client／manager 清理 | client 等 15 秒，再取消并等 2 秒；仍 pending 则保留原 task／attempt 并报未完成。manager 外层 20 秒，未清理的引用保留供回收 |
| 本地停止 | 关闭后拒绝新提交，取消继续传播；超时段的迟到结果不追加到该段结果列表，有局部回归 |
| 产品轮次和输出 | #14 沿用许可、归属代际、当前轮次和停播闸门；旧结果不得进入新模型上下文或播放。完整 TTS／设备链不由上述局部回归证明 |
| 远端取消 | 能力和执行终态未知；本地停止、取消通知、远端确认、费用分别记载，未知不写成“不支持”或“已停止” |

账号、设备、连续对话／轮次、许可版本／归属代际和单次调用标签只用于本地隔离，不加进工具 Schema。一次超时、异常或连接中断后，执行状态不明的请求不自动重放。

## 5. 交付与验收入口

- [#14 接线交接](../handoff/2026-11-12-mcp-knowledge-to-issue14.md)：产品入口缺口、调用和输出检查点。
- [服务器执行单](../handoff/2026-11-13-mcp-minimal-server-check.md)：执行方法；本次已完成，结果见[服务器签收记录](../verification/2026-11-13-mcp-server-acceptance.md)。
- [2026-11-13 交付记录](../verification/2026-11-13-mcp-minimal-delivery.md)：保留服务器接续前的交付与审查记录，最新结论见服务器签收记录。

#12 交付服务配置、Schema、结果消费约定、直接工具证据和本地边界处理；#14/#19 一处验证模型自动调用、依据资料续答和实际音箱播放。多客户扩展、完整远端契约和服务升级规则由 #88 跟进。
