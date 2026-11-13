# 2026-11-12 MCP 阶段二修复与离线验证记录

范围：Issue [#12](https://github.com/yanghan24/xiaozhi/issues/12) 的通用 MCP 安全边界、契约草案和合成回归。契约见 [mcp-knowledge-contract.md](../contracts/mcp-knowledge-contract.md)。

> 本文保留阶段二当时的0真实请求和6/18进度，不作为当前总进度。后续阶段三已取得真实初始化、工具Schema和一次直接工具结果，见[阶段三协议实测](2026-11-12-mcp-stage3-protocol.md)。阶段四新增清理2条失败（77条中75通过/2失败），见[交付审查](2026-11-12-mcp-delivery-review.md)；不改变以下75条历史字节，但不能继续概括当前全部回归通过。

**结论：修复后，本机两轮、Ubuntu 禁网容器两轮，各 75/75 通过，0 失败、0 错误、0 跳过。**这是安装的 SDK 对象兼容和本地代码回归，不是实际 SSE/知识库/模型集成验收。真实 MCP、模型、TTS 请求均为 0；六条最终验收尚未签收。

## 1. 基线与证据类型

| 项目 | 本次值 |
| --- | --- |
| 源码基线 | `b448aa080ba057f8e48383adf6501daa4df32011` |
| 分支 | `feat/12-mcp-knowledge-contract`，独立 worktree；改动尚未提交/推送 |
| Issue 基线 | 最近核对正文更新时间 `2026-11-11T12:14:29Z` |
| 修复前 | run06/run07 各 60：43 通过、17 失败、0 错误/跳过，退出码 1 |
| 修复后 | 本机 final15/16、Ubuntu final15/16 各 75/75，重复轮次不累加为不同测试方法 |
| 固定方式 | 未提交源码不能只引用 Git 提交；由最终包 SHA-256 和 14 文件清单固定实际字节 |
| 上游业务契约 | 实际 Schema、业务状态、账号映射、费用、限流、远端取消仍待核实 |

[归档索引](evidence/issue-12/offline-20261112/README.md)包含修复前 JSON/日志、本机最终两轮 JSON/日志、源码清单和归档哈希。Ubuntu 原始 JSON/日志仍保留服务器；本地记录的是网页终端现场观察和文件哈希，未伪造或重建一份“服务器原始报告”。

## 2. 最小修复范围

| 文件 | 行为变化 |
| --- | --- |
| `server_mcp/errors.py`（新增） | 固定安全异常和文案；专用 ToolNotFoundError 区分未注册工具与上游 ValueError |
| `server_mcp/mcp_client.py` | 每次连接独立 attempt 状态；初始化/发现失败传播；并发初始化复用；清洗后重名拒绝；关闭后受控错误；有界清理与调用取消传播；日志不记录原始外部内容 |
| `server_mcp/mcp_manager.py` | 执行单次提交，不在超时/未知异常后重连并重放；原子校验注册及跨服务冲突拒绝；关闭时禁止新提交和迟到注册；清理失败/取消保留未完成引用；安全日志 |
| `server_mcp/mcp_executor.py` | 固定公开错误；仅专用未注册异常映射 NOTFOUND；校验结果结构和 MCP 错误标记；兼容 snake_case/camelCase，接受合法 structured-only 结果 |
| `scripts/check-mcp-offline.py`（新增） | 合成依赖、离线保护、真实产品模块、实际代码 AST 片段、SDK 模型兼容、75 条回归和逐文件哈希 |
| 契约及本文（新增） | 分开记录源码事实、合成结果、本地设计和上游待确认项 |

没有新增知识库专用 status/generated 分类器，没有按账号选取真实 MCP 凭据的实现，也没有更改模型/TTS、完整 chat、设备固件或部署配置。正常协议对象仍字符串化后返回 REQLLM；这不是业务完整回答的判定。

### 代表性回归

完整方法 ID 和结果见本机最终 [result.json](evidence/issue-12/offline-20261112/local-fixed16/result.json)。

| 边界 | 代表方法/测试组 | 最终结果 |
| --- | --- | --- |
| 初始化失败不报成功 | RequiredBehavior.test_initialization_failure_is_not_reported_as_connected | 通过 |
| 发现失败、同服务名称冲突 | ExtendedRequirements | 通过 |
| 初始化/关闭交错 | FixedRegression.test_cleanup_while_discovery_pending_releases_initialize；test_cancelled_initializer_does_not_publish_partial_tools | 通过 |
| 并发初始化、关闭后新会话 | FixedRegression.test_concurrent_initialize_shares_one_worker_and_ready_result；test_initialize_after_cleanup_builds_fresh_session | 通过 |
| manager关闭围栏 | FixedRegression.test_manager_cleanup_during_initialize_blocks_late_registration；test_closed_manager_cannot_submit_even_when_cleanup_reference_remains | 通过 |
| 清理取消保留回收引用 | FixedRegression.test_cancelled_manager_cleanup_keeps_unfinished_clients_for_retry | 通过 |
| 跨服务同名不覆盖 | FixedRegression.test_same_tool_in_another_server_does_not_overwrite_registration | 通过 |
| 超时/结果未知不重放 | RequiredBehavior及ExtendedRequirements对应分支；ExistingBehavior.test_manager_failure_submits_once_without_retry_wait | 通过 |
| 固定日志/公开回复 | RequiredBehavior参数、异常、progress、通知与回复隐私测试；初始化等扩展分支 | 通过 |
| 错误别名/结构化内容 | FixedRegression.test_snake_case_mcp_error_is_rejected；test_conflicting_error_aliases_are_rejected；test_structured_only_protocol_result_can_continue_without_business_verdict | 通过 |
| 畸形结果、渲染失败 | FixedRegression.test_malformed_protocol_result_and_render_error_are_safe | 通过 |
| ValueError不冒充工具未注册 | FixedRegression.test_upstream_value_error_is_execution_error_not_tool_not_found | 通过 |
| 既有本地许可代码 | LocalVoiceGuard（9条，合成authority） | 通过，不是账号授权实测 |
| 超时后的迟到结果 | ToolTimeoutObservation（1条，实际chat两个循环） | 任务继续执行，迟到结果未追加本段结果列表；不是远端取消证明 |

原 17 个失败方法按根因修复，不等于 17 个独立 Issue 缺陷。原来的“三次重试/错误对象无门控回传”现状断言随策略变化更新；未删除目标要求、未使用 xfail/expectedFailure 或跳过凑绿。新增 15 条 FixedRegression 覆盖生命周期和 SDK 等边界。

## 3. 测试如何加载代码

- 实际导入三个 MCP 产品模块、新 errors、工具基类及真实 Action/ActionResponse。
- AST 抽取生产 sanitize_tool_name；从 ConnectionHandler.chat 抽取工具提交和 future 等待两个原循环，不重写完整 chat。
- 加载真实本地 VoiceGate/GuardedSocket 类，authority、配置、日志与网络会话使用合成替身；不构造真实拒绝记录存储或打开业务数据库。
- `synthetic_knowledge`、`synthetic.invalid` 和所有业务 payload 均为合成样例，不是实际 ask_knowledge 定义。
- stub 模式替换 SDK；installed 模式使用镜像中安装的 SDK 结果模型/接口，**transport 和 ClientSession 仍替换**，没有做真实 SSE 握手。
- Python 层拒绝常见连接/DNS/UDP/子进程，保护异常继承 BaseException，避免被宽泛业务异常处理吞掉。Windows 标准库内部 socketpair 仅在限定线程上下文放行。这不是 OS 沙箱；Ubuntu 另用 Docker 禁网。

## 4. 运行环境、命令与隔离

### 本机开发预检

Windows，CPython `3.12.10`，`--sdk stub --group all`。在 #12 worktree 执行以下形式，输出目录必须全新，不能覆盖历史报告：

```text
python -I -B scripts/check-mcp-offline.py --sdk stub --group all --output <全新输出目录>
```

最终目录位于仓库外 `issue-12/evidence/offline-fixed-local-run15/` 与 `offline-fixed-local-repeat16/`；归档副本见下文。

### Ubuntu 正式离线回归

- 服务器：Ubuntu `192.168.168.114`，`jt`，通过 JumpServer 网页终端。
- 独立根：`/home/jt/issue12-offline-stage2-20261112/`，最终源码 `final-src15/`。
- 固定已有依赖镜像：`sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5`，原标签 `xiaozhi-product-voice:prompt-7f10cbd`。
- Python `3.10.22`；分发元数据：mcp `2.3.0`、mcp-types `2.3.0`、httpx2 `2.14.0`、httpx `0.28.1`、openai `2.8.1`、anyio `4.16.0`。
- 创建全新测试容器 `issue12-offline-final15`、`issue12-offline-final16`；不拉镜像、不安装依赖。

下面为实际隔离参数的等价复现形式，源/结果绝对路径与新容器/轮次名按记录填入；不是建议对生产容器执行：

```bash
docker create --name <新测试容器名> \
  --network none --read-only --cap-drop ALL \
  --security-opt no-new-privileges --pids-limit 128 \
  --memory 768m --cpus 1 --user 1000:1000 \
  --env PYTHONDONTWRITEBYTECODE=1 \
  --tmpfs /tmp:rw,nosuid,nodev,size=64m \
  --mount type=bind,src=<final-src15绝对路径>,dst=/work,readonly \
  --mount type=bind,src=<独立results绝对路径>,dst=/out \
  --entrypoint python \
  sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5 \
  -I -B /work/scripts/check-mcp-offline.py \
  --sdk installed --group all --output /out/<新轮次>
docker start -a <新测试容器名>
```

只挂受测源码和新结果目录，无私有配置、真实凭据或生产数据挂载。最终两容器 `Running=false`、`ExitCode=0`、`NetworkMode=none`、`ReadonlyRootfs=true`。为保留证据未删除已退出的最终容器。结束时清除 sudo 缓存并观察到 `CACHE_CLEARED`。

## 5. 运行结果与失败历史

| 轮次/环境 | 运行 | 通过 | 失败 | 错误 | 跳过 | 说明 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 修复前本机 run06、run07（各） | 60 | 43 | 17 | 0 | 0 | 有效红灯，原始结果保留 |
| 修复后本机 final15、repeat16（各） | 75 | 75 | 0 | 0 | 0 | 用例和14文件源码哈希一致 |
| Ubuntu final-installed15、16（各） | 75 | 75 | 0 | 0 | 0 | 安装的SDK模型、合成IO；两轮用例/源码一致 |

最终测试组：ExistingBehavior 22、RequiredBehavior 9、ExtendedBehavior 11、ExtendedRequirements 8、LocalVoiceGuard 9、ToolTimeoutObservation 1、FixedRegression 15，合计 75。每条测试可能含多个 subTest，不重复计为多个方法。

### 中间故障没有隐去

1. 最早 Windows 31 条全部失败：网络保护误拦 asyncio 内部 socketpair，不能据此归因产品。修保护后有效 run02 为 31 条、22通过/9失败。
2. 扩展 run03 因 AST 匹配不唯一启动失败，0 用例执行；限定真正提交工具的循环后得到有效 60 条。
3. Ubuntu `before-installed` 在 setup 阶段 TypeError、退出码2，未执行用例，不是有效修复前 Ubuntu 60 条结果。普通函数替换 Popen 与 SDK 导入兼容存在问题；改用保留 Popen 类形状的保护后能运行。未取得足够完整 traceback，不宣称精确证明唯一根因。
4. Ubuntu `fixed-installed-run12` 未全通过：72 个方法，69通过、3个方法失败；旧记录器还记有11个子测试失败，不能把11当成不同方法。本次实查 SDK `CallToolResult.model_fields` 为 `meta/content/structured_content/is_error/result_type`；TextContent 为 `type/text/annotations/meta`。camelCase构造参数会被该模型忽略，不能用错误的构造方式声称已经测到错误标记。
5. 依据实际字段改测试构造与通用执行器兼容，增加 snake_case 和冲突别名回归。本机及 Ubuntu 后续两轮74/74；再加关闭manager不能新提交的保护/用例后形成最终75/75四轮结果。中间包和结果未覆盖。

## 6. 哈希与证据索引

| 对象 | SHA-256 |
| --- | --- |
| 最终14文件源码包 `issue12-fixed-final15.tar.gz` | `e406aa57282b717a241c4656d7341ad607b62a79e4a6b614b7b745d74b816db9` |
| 最终逐文件清单（本地归档 source-manifest.json） | `118d7d3d83abb55fd90dfdcf07c690dd071853bd4a62422d79684b27391dd544` |
| 最终诊断脚本 | `a9e0fb86db34777493678edf50940f528bb0a12de5fff7480581410bf70cdac1` |
| 本机 final15、repeat16 JSON（相同） | `58af7ce64167ba2f3fc255e2b6cd2929b324e0b8b55ab570d369e4bea8130d2f` |
| Ubuntu final-installed15、16 JSON（相同） | `9f3e319ededf20b169e1c3f5bbd4f23cb18bf46e0d521b4bb13652a8dd5c553a` |
| 修复前 run07 JSON | `cc4cd0d79246e59789c241d059302909c8ee18b44d87152f2144eaae6160c1a6` |

最终包 55,945 字节，14 个源码文件（不含包内 manifest 自身），不含私有配置。上传后服务器 `final15.tar.gz` 的包哈希相同；最终源码对清单和结果内 source_sha256 检查均0不一致。本机也重新逐文件核对，0不一致。

- 仓库内：[修复前报告](evidence/issue-12/offline-20261112/before-run07/result.json)、[修复前日志](evidence/issue-12/offline-20261112/before-run07/tests.txt)、[本机15](evidence/issue-12/offline-20261112/local-fixed15/result.json)、[本机16](evidence/issue-12/offline-20261112/local-fixed16/result.json)、[源码清单](evidence/issue-12/offline-20261112/source-manifest.json)、[附件哈希](evidence/issue-12/offline-20261112/archive-sha256.json)。
- 仓库外：`issue-12/evidence/` 保留首轮、扩展、中间修复、最终报告、脚本快照和打包资料，不把重复结果算为新增用例。
- Ubuntu：根目录下 `results/final-installed15/{result.json,tests.txt}`、`results/final-installed16/{result.json,tests.txt}`。本次又以不提权只读命令核对最终统计、两轮一致性、14文件和包/报告哈希；原文件未导出至本机。

JSON相同因为没有运行耗时等易变字段，不意味着两次是同一个运行。测试日志含运行时间，文件哈希可以不同。报告中的请求数0是受测构造/范围说明，不是上游计量；`guard_denials={}`仅说明未触发已拦截入口。

## 7. 现网状态与测试版本分开

本任务未执行产品 deploy/restart、修改运行配置或操作业务数据库，但**不能写“整个会话现网未变”**：

| 观察阶段 | API | voice |
| --- | --- | --- |
| 开始 | 镜像 `xiaozhi-product-api:1b50709`；StartedAt `2026-11-12T02:39:25.738003882Z` | 镜像 `xiaozhi-product-voice:prompt-7f10cbd`；StartedAt `2026-11-12T02:39:42.550148611Z` |
| 后续/最终 | API同标签；StartedAt `2026-11-12T03:13:39.656388819Z` | 镜像 `xiaozhi-product-voice:p4-maintenance-20261112`；StartedAt `2026-11-12T03:14:09.377698598Z` |

期间另有操作更新了运行服务，不归因于本次离线测试，也不纳入本次受测源码。最终回归始终固定前述镜像ID和14文件字节；最终15/16期间所记录的运行服务值未再变化。清除 sudo 缓存的最后现场记录为 `2026-11-12T03:55:34Z`。

## 8. 交付质量核对

- `git diff --check` 通过；没有commit/push、暂存、GitHub写入或部署动作。
- 重新核对14个源码文件和7个归档附件哈希，0不一致；本机两轮报告各75个方法且case/source一致。
- 对6个相关Python文件做内存compile/AST解析，不导入产品或触发网络，全部通过。
- 核对10份本地Markdown中的相对文件链接，全部目标存在；网页链接只核对已有来源，不宣称逐个重新访问。
- 对相关源码、文档和归档JSON/日志检查JWT形状、私钥头、模型Key形状和URL内嵌账号密码，未命中；人工核对外部输入不写入新增产品日志/公开错误。历史失败附件中的注入标记为合成数据。本检查是限定模式检查，不承诺所有形式的秘密或SDK全部日志都已覆盖。
- 以上只完成本次差异和证据质量核对；真实验收缺口仍保留，R-02不因这些检查通过而最终签收。

## 9. 未覆盖项与交付状态

- 未取得/调用实际 ask_knowledge Schema、真实初始化、工具与服务版本、正常和不完整原始记录。
- 未发真实模型请求；未验证模型实际选工具、续答完整性、不完整是否被误报成功。
- 许可guard使用合成authority；未验证真实账号矩阵、MCP凭据选择、生命周期/费用/保留。
- 未验证远端取消/终态、限流、失败计费或真实SDK全部日志路径。
- 仅验证实际chat的两个循环，不是完整TTS/轮次/设备隔离；也不承诺任意不响应取消的任务都绝对无残留。
- 无设备播放验收，未修改Edge TTS；#14责任不移入本次离线结论。

阶段二 D-03（设计）、D-04（本地策略和限制）、D-05（最小修复/合成回归）可交付；D-01真实定义、D-02业务语义/完整性仍未齐备。V-01的隔离上传、哈希与Ubuntu离线回归已完成，但私有模型/知识库配置引用仍未定位，整项保持部分完成。文档和证据尚未commit/push，未发布PR，未关闭#12或更改#14依赖。
