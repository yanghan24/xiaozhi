# 2026-11-12 Issue #12 缺口修复与真实客户端验证

**当前结论：**清理引用缺陷已修复；当前源码对应的本机两轮、Ubuntu两轮均81/81。修复后的ServerMCPClient使用真实MCP SDK完成初始化/工具发现和一次直接工具调用。模型实际选择工具并续答、多账号与远端契约仍未验收。

用户要求改按缺口推进，不再按阶段等待。旧阶段文档保留原范围与字节，当前实现/测试以本文和[当前证据索引](evidence/issue-12/verified-sdk81-20261112/README.md)为准。

## 1. 本轮解决了什么

| 缺口 | 实际处理 | 验证 |
| --- | --- | --- |
| caller取消/清理截止后worker引用丢失（B1） | worker仍pending时保留attempt/task，取消后二次等待未结束则抛固定TimeoutError；下次cleanup继续追踪同一worker；仅完成后释放引用 | 原失败2条纳入正式回归，覆盖关闭期间禁止调用/初始化、重复回收、释放后成功清理与新会话 |
| manager的pending集合残留 | 仅清理成功后删除相应pending引用；失败保留并可重试；兼容已注册不可hash测试对象 | 成功删除/失败后重试2条正反例 |
| MCP2.3.0超时类型 | 保留现有timedelta入口，传给SDK前用total_seconds转数值秒数，初始化与工具执行都转换；没有更改SDK参数名 | 新增初始化转换及实际SDK＋anyio内存流超时测试 |
| 原测试只替换会话，漏实际SDK请求时钟 | 安装模式保留真实ClientSession用于一个无外部IO的内存流测试 | 数值秒数产生准确初始化超时；拒绝TypeError和无关MCPError，不用任意异常盖绿 |
| 产品MCP客户端只做过合成回归 | 新只读根文件系统、非root测试容器中加载实际客户端及安装的SSE/ClientSession，有限初始化和一次公开工具请求 | bridge连接失败单列；host网络下发现及工具请求成功、清理成功、退出0 |

未修改`product_voice.py`、WebSocket产品装配、完整chat或TTS行为；未部署、未变更现网配置或业务数据库，未读取/解密业务库中的模型Key。

## 2. 当前源码与最终81条回归

- Git基线：`b448aa080ba057f8e48383adf6501daa4df32011`；分支`feat/12-mcp-knowledge-contract`；所有改动尚未提交/推送。
- 当前14文件包：`issue12-sdk-verified81.tar.gz`，57,200字节，SHA-256 `e5b36b27d8ddb17f1ad038abf4921b6497f62eddab9f98554a01cbe418ab9916`。
- [源码清单](evidence/issue-12/verified-sdk81-20261112/source-manifest.json)SHA-256 `74034812c32566f09eda845bebd9c4a9fa04eaf083037c0a678d318b63d6c8c8`；不含私有配置。
- 当前诊断脚本SHA-256 `8faad45fdbc5b5139aa8e4d1cdb9a05bbb802d3c2da1ff6539a0421290db7612`。
- 产品client SHA-256 `d3687e7c25de8600022c80b05bbe9c78df9f1c64d948491076a98c1f086f120d`；manager `2b563e95d1fa9b81e8bf65f4fa171df59ce67f9b435ed90e81ba10ad8ce62f13`；executor字节未因本轮再次变化。

| 环境 | 原运行目录 | 运行/通过/失败/错误/跳过 |
| --- | --- | --- |
| Windows CPython3.12.10，SDK stub | `issue-12/evidence/sdk-verified-local05` | 81/81/0/0/0 |
| 同上重复轮 | `sdk-verified-local06` | 81/81/0/0/0 |
| Ubuntu Python3.10.22，安装MCP2.3.0 | `/home/jt/issue12-sdk-verified81-20261112/results/installed01` | 81/81/0/0/0 |
| 同上重复轮 | `results/installed02` | 81/81/0/0/0 |

重复轮次分别case/source相同，14文件0不一致，结果内source_sha256与清单相同，无skip/xfail。81个方法=原75全部保留+6个新增；不把四轮累计为324种测试。

本机两份JSON哈希相同：`92edc5ae3d8940e83e99ea61b0c592ddbf661ccba7eabaf2e101e8030cbc8d00`。

Ubuntu两份原JSON现场哈希相同：`1bbdf293608726c71967c1ad434fc34b4147e971221379d3b3747697fca2340f`，各13,185字节；原件留服务器，本地没有伪造副本。

Ubuntu两个最终容器`issue12-sdk-verified81-01/02`固定旧依赖镜像`sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5`，使用network none、read-only、user1000:1000、cap-drop ALL、no-new-privileges、768MiB、1CPU，仅挂受测源码和新结果目录。均exit0、running=false，保留退出容器/日志供取证，未拉镜像或安装生产依赖。

### 失败/版本差异没有覆盖

1. 原77条75通过/2失败是实际修复前红灯，保留在[审查失败归档](evidence/issue-12/stage4-review-20261112/README.md)。
2. 中间cleanup79两端各两轮79/79有效，但未包含后加SDK真实内存流检查，不能用其证明数值超时兼容。
3. SDK81首轮Ubuntu80通过/1失败：数值秒数确已产生超时，但测试误要求内建TimeoutError；实查SDK返回MCPError，code=-32001、消息`Request 'initialize' timed out`。修正严格断言后复验，不删除该测试。
4. 同期工作区出现另一脚本变体及文档哈希，已原字节保存；没有把另一组本机/服务器哈希拼作本轮结果。最终脚本统一后重跑两端两轮并重新归档。旧`sdk81-20261112`、`cleanup-sdk-20261112`属于别的固定字节，保留但不当作当前清单。
5. 网页终端上传期间有转义SyntaxError、压缩内容错误；在写目录前校验失败，没有据此执行未知脚本，也未覆盖历史。最终新目录14文件均校验通过。

## 3. 真实SDK＋产品客户端：失败和成功分别记录

独立实测源码：`/home/jt/issue12-sdk-final81-20261112/src`。其中client/manager/executor与当前字节相同，诊断脚本为统一前`e829...`版本；真实探测没有执行该离线脚本。不能用此目录声称最终离线脚本字节已测，最终离线证据另见§2。

真实探测加载实际ServerMCPClient、真实安装的SSE transport/ClientSession/结果模型；为避免音频/配置启动副作用，替换无关logger/config bootstrap，并AST抽取实际sanitize函数。阶段诊断只包装SDK方法以记录开始/完成与错误类型/函数行号，不改其协议实现；不是完整设备或模型集成。固定已有MCP受控配置只读挂载，密钥值/动态会话URI不输出。根文件系统只读、非root、无Docker socket或数据库挂载，最多55秒后终止。

| 容器/报告目录 | 网络 | 结果 | 清理/工具计数 |
| --- | --- | --- | --- |
| `issue12-cleanup79-live01`；cleanup79根下`results/live-discovery01` | bridge | 初始化失败，exit1；当时无细分阶段 | cleanup=true，工具0 |
| `issue12-sdk-final81-live02`；final81根下`results/live-discovery02` | bridge | 初始化失败，exit1 | cleanup=true，工具0 |
| `issue12-sdk-final81-phase03`；`results/live-phase03` | bridge | `transport_started`后ConnectTimeout，尚未SDK initialize，exit1 | cleanup=true，工具0 |
| `issue12-sdk-final81-host04`；`results/live-host04` | host | transport ready、initialize及list_tools完成，ask_knowledge存在，exit0 | cleanup=true、连接false、worker引用已清，工具0 |
| `issue12-sdk-final81-tool05`；`results/live-tool05` | host | 实际产品client一次call_tool返回真实CallToolResult，exit0 | 工具1、cleanup=true、连接false、worker引用已清 |

桥接失败和host成功对照支持“容器bridge出站连接受阻”，不能把前三次失败伪称是SDK参数名或timedelta所致；实际参数名仍为read_timeout_seconds/client_info。SDK秒数类型问题由无凭据内存流TypeError与修复后的准确超时另外证明。没有自动重放工具请求；三次失败均未发现工具/投递tools-call。

host网络仅用于此独立进程出站，不启动监听或改服务器路由/防火墙；它不是禁网测试，须与§2两轮network none区别。

### 一次真实工具响应（只留安全摘要）

问题仍为已有公开样例“怎么申请成立社会团体”，不额外访问别的账号或资源：

- 实际SDK结果`CallToolResult`，`is_error=false`。
- structured labels：status=ok、mode=chunks、generated=false、modelCalled=false、model为空字符串；returned=5、topK=5、cacheHit=false、服务自报elapsedMs=408。
- sources=5，content块=6；原文/资源标识未写入本次安全摘要。
- business_complete保持未知：只证明检索响应，不等于qwen3.8-flash选择工具/续答或最终回答完整。
- `live-host04`原报告3,533字节，SHA-256 `20d53b3175406c8bde415cb62c7f7b7eb6fdece8c1eaa76336d2ca65c6f79053`；`live-tool05`4,028字节，SHA-256 `311426c20915fcc074b90be4ae2072107ec47a17497a3b520033573168f7ba8f`。原件留服务器，哈希为现场观察，不是本地持有的原JSON。

本轮有5个真实初始化/连接尝试：3个bridge未成功、2个host成功；工具投递1，模型0、TTS0。前一轮标准库协议探测的2个成功SSE会话、工具1仍单列。因此本任务累计成功发现会话4、bridge失败连接尝试3、工具投递2、模型0/TTS0。客户端计数不是服务端计量，未查询费用，不推定免费。

## 4. 现网只读核对与权限收尾

本轮测试前后：

| 服务 | 镜像 | StartedAt（前后相同） |
| --- | --- | --- |
| API | xiaozhi-product-api:1b50709 | 2026-11-12T03:13:39.656388819Z |
| voice | xiaozhi-product-voice:pr79-f830a8c | 2026-11-12T06:50:32.021169588Z |

GitHub main部署入口已更新到13:20并链接[PR79部署证据](https://github.com/yanghan24/xiaozhi/blob/6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80/docs/verification/2026-11-12-pr79-deployment.md)。这里记录实际观察，不把别的任务部署归入本次源码。所有新测试容器均已退出，没有部署/重启产品、写现网配置、查询业务库、撤销授权或改网络设置。最后观察到`CACHE_CLEARED`。

## 5. 模型链路缺口：不是只差一个Key

只读核对GitHub main `6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80`：

- `main/xiaozhi-server/core/product_voice.py:213-214`的MarketModel.response_with_functions仍无条件抛`Tools are not enabled for direct market model mode`。
- 产品WebSocket装配强制nointent，连接初始化在nointent时早退，不构造UnifiedToolHandler。因此产品模式MCP模型问答能力尚未接上，补模型Key不能自行解决；设备产品接线属#14，不能为#12验证绕过#13许可或偷偷启用旧管理员工具模式。
- 官方私网`/xiaozhi/internal/device-voice/v1/model-access`依据当前设备许可与归属账号授权返回模型访问能力。通过这个正式服务由它处理数据库/权限，不等于手工查询业务库解密Key；本任务未获取无授权设备令牌或用共享服务Key冒充测试账号。
- #12的独立后端“模型选择工具→工具→模型续答”仍需获准测试账号的受控模型配置/调用接口。旧Key加载器查库解密、旧wrapper写现网voice的方式均未运行。

已确认的MCP客户端和接口可以交接；不能把设备曾回答或维护mcp_ready状态当作该模型工具链证据。历史incomplete、无命中/错误、账号A/B允许拒绝、MCP授权映射/生命周期/计费保留以及远端取消/限流仍待可靠记录或获准最小补验。

## 6. 当前完成与限制

B1及SDK秒数缺口已经代码修复、两端81条复验；产品客户端真实发现/单次工具也已成功。D-05可恢复勾选，R-01材料保持完成；原18项仅作记录索引，后续按实际缺口工作，不要求逐阶段全勾才能行动。六条最终验收仍未签收，不自动关闭Issue。

没有commit/push、PR发布、Ready、合并或部署。后续真实模型链和账号/远端规则按各自受控条件推进，不以sudo权限代替账号资源授权，也不重放已投递工具请求。
