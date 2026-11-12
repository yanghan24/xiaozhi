# 2026-11-12 Issue #12 阶段三：真实 SSE 与工具协议验证

状态：**已完成真实初始化/工具发现和一次直接工具请求；没有完成真实模型工具链、历史不完整或多账号验收。**契约见 [MCP知识库契约](../contracts/mcp-knowledge-contract.md)，阶段二固定源码回归见[离线验证](2026-11-12-mcp-offline-development.md)。

> 本文保留阶段三当时7/18及D-05完成的历史状态。后续[阶段四审查](2026-11-12-mcp-delivery-review.md)复现清理引用2条失败、重新打开D-05，同时完成R-01本地交付；总数仍7/18，构成已改变。实际协议观察不受此历史标注影响。

## 1. 本次范围与方法

用户要求继续阶段三。先通过`gh`只读核对Issue #12，正文仍为`2026-11-11T12:14:29Z`版本，OPEN、无评论；读取GitHub main部署入口及已有部署证据，不切换分支、不覆盖未提交源码。

在Ubuntu `jt@192.168.168.114`经JumpServer网页终端执行：

1. 限定检查已有MCP脚本与其直接引用的私有配置元数据，不遍历整份私有目录，不输出凭据值。
2. 找到有效的知识库SSE配置，标准库协议探测在新目录进行一次纯发现，再进行一次指定公开问题的直接工具调用。
3. 每次有限超时、无自动重试；状态不明时仅检查结果文件，没有重发工具请求。网页终端多次画面/输入延迟，不能将截图超时当成工具失败；最终从已有报告读到响应。
4. 原始报告保留服务器，本地文档记录观察、Schema和去敏形状，不伪造完整原始JSON副本。终端结构核对时曾显示部分知识原文和资源标识；本地交付不附这些完整原文、标识或截图，凭据值未输出。

**方法限制：**宿主Python 3.8.10，使用标准库HTTPS/SSE与JSON-RPC探测，不是产品`ServerMCPClient`或MCP SDK真实服务集成。阶段二安装SDK的75条回归仍为合成传输；本次协议成功不能倒推修改后的产品链路已真实验收。

## 2. 配置、旧脚本与执行边界

| 项目 | 核对结果 |
| --- | --- |
| MCP受控配置 | `/home/jt/.private-mcp/config.json`，0600、uid1000，键为headers/url，Authorization已设置；未输出值 |
| 目标 | 与Issue指定`https://aimarket.hncig.cn:4191/mcp/service-669a396c73/sse`准确匹配 |
| 旧入口 | `/home/jt/xiaozhi-mcp-probe.py`、`xiaozhi-mcp-model-probe.py`及包装sh，另有`mcp-retest.py/sh` |
| 旧包装器限制 | 会向运行voice容器复制脚本/配置并chmod、清理临时文件；本轮没有运行这些包装器，不改现网容器 |
| 旧模型Key加载器 | `load-live-model-key.py`包含业务库查询及解密，再写入临时Key；只读核对逻辑，没有执行，不因文件存在扩大数据库或账号访问授权 |
| 已知模型Key引用 | `/run/issue13-live-model-key`不存在；不读取其他账号或从业务库取Key来凑真实模型测试 |
| sudo | 非交互缓存不可用；本轮未输入密码、未sudo、未创建Docker容器或安装依赖 |
| 当前部署 | GitHub入口仍记录旧语音标签；阶段二曾观察其他操作更换现网。本轮未提权重查实际容器，不把文档值当本次现网实查 |

配置存在和单凭据可用不证明凭据主体、账号映射、全客户访问权、费用或生命周期。其余真实账号/模型测试仍需受控测试账号的配置/接口，不使用业务库解密旁路。

## 3. 真实初始化与工具发现

独立根：`/home/jt/issue12-stage3-discovery-20261112/`。

- 原发现脚本：`probe.py`，SHA-256 `838a136946b8bd4ab3194875d3b29b77005a79d0cc9823e6389e9ac2eefa00b2`，上传后核对一致；8,298字节。
- 纯发现报告：`discovery01/discovery.json`，2,045字节，UTC `2026-11-12T04:57:03.415745+00:00`～`04:57:13.470965+00:00`。
- 对外行为：一次SSE会话，initialize、notifications/initialized、tools/list；没有tools/call或模型请求。
- 40秒总预算、每段最多10秒，外层50秒；禁用代理和跨站重定向，动态端点要求同源且同一服务路径。实际私有端点/查询串不写报告。

| 项目 | 实际结果 |
| --- | --- |
| SSE HTTP | 200，Content-Type text/event-stream |
| JSON-RPC POST | initialize、notifications/initialized、tools/list均202；以SSE对应响应为结果，202本身不算协议完成 |
| 协议版本 | 2024-11-05 |
| 服务自报 | name=service-669a396c73，version=4.31 |
| 能力 | tools.listChanged=false；resources.subscribe=false、listChanged=false |
| 工具 | 一个ask_knowledge |
| 独立工具版本 | 定义未暴露；4.31是服务自报版本，不是推定的工具版本 |

### 实际输入Schema（从tools/list观察转录，非伪造原文件）

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

注解：readOnlyHint=true、destructiveHint=false、openWorldHint=false；未声明默认值。工具描述明确“不调用大模型”，返回完整知识切片、Markdown表格和图片资源，引用resources/read及neighbors，并写明“结果是原文而非模型结论”。本轮没有调用resources/read或继续读取邻片，不能签收这些能力。

## 4. 一次真实直接工具请求

原发现脚本保持不变，新增`probe-call2.py`。第一次生成副本时出现字符串转义SyntaxError，未生成可运行副本、未对外调用；修正生成脚本后内存compile通过并在新文件创建，不覆盖原发现证据。

运行形式：

```text
timeout --signal=TERM 50s python3 -I -B <独立根>/probe-call2.py --config <受控配置引用> --output <独立根>/tool01 --question "怎么申请成立社会团体"
```

每轮只投递一次，禁用重试；没有模型和TTS调用。最小实际请求（会话/凭据去除）：

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "ask_knowledge",
    "arguments": {"question": "怎么申请成立社会团体"}
  }
}
```

现场报告`tool01/discovery.json`：34,687字节，UTC `2026-11-12T05:07:18.552153+00:00`～`05:07:29.017695+00:00`。以下为原报告中观察值的**人工摘要，不是原始响应或服务器JSON副本**：

| 层次 | 观察 |
| --- | --- |
| 探测终态 | tool_response_received；tools_call_submissions=1、model_requests=0、retries=0 |
| MCP结果 | 顶层content、structuredContent、isError；isError=false，content为6块 |
| 业务结构 | structuredContent.status=ok、mode=chunks、generated=false、modelCalled=false、model为空字符串 |
| 来源 | sources为5项；来源标题《社会组织工作资料汇编（2026年）.docx》；正文含相关成立条件和登记材料原文 |
| 结构保留 | answer为字符串，sources/chunks为列表；returned=5、topK=5、cacheHit=false、elapsedMs=398（服务自报，不是端到端耗时） |
| 明确限制 | note写明“返回完整存储切片；不代表完整文档。跨片条款或表格请沿neighbors读取，复杂图表以原图核对。”本轮未执行这些后续读取 |
| 原文定位 | 来源含text、ordinal、page/start/end、revision、doc/chunk/kb、context、neighbors、uri等；原始标识/URI未导出 |
| 完整性 | 只证明原文检索响应已收到，不是最终模型回答完成；business_complete在探测中保持未知 |

### 重要版本/模式差异

**generated=false不能脱离版本和mode单独解释。**当前4.31工具声明不调用大模型，实际mode=chunks且modelCalled=false，返回原文与此一致；不能将它一律判成历史生成失败，也不能计为完整知识库问答。历史2026-11-10摘要的status=incomplete/generated=false、之后ok/true仍缺原始版本/模式记录，本次ok/chunks不能覆盖历史失败或证明其根治。

## 5. 证据保留与计数

原报告和脚本只在本任务新目录保留，配置留原受控路径，未复制进入Git或聊天。原始JSON未取回本机；本地记录为网页终端观察和哈希。SSE纯发现报告现场SHA-256已核对：

`a8ac41feb034101973e42ab07a83de232f25eb40dadaab06e36ff04ce8dd6d10`

后续稳定只读复核的工具原报告SHA-256：

`33227ae4d1519e74149a874aa31770489fcd76373014a50f61d7bdf5a5b09941`

实际服务器`probe-call2.py` SHA-256：`dce149cb670c44ff1e52cde9892e613519ea509d62cf6fce7c10e41e77c6ee67`。此脚本由原发现脚本在服务器生成，与本地后来调整的探测脚本不是相同字节，复现时必须核对对应版本。工具报告权限0600。

两份哈希均是现场原文件读取值，本地未持有原件；不能重建一份JSON后赋予这些原报告哈希。仓库内只归档了明确标识的[人工观察索引](evidence/issue-12/stage3-20261112/README.md)，该索引有自己的本地哈希，不混同原件。

| 计数 | 本轮 |
| --- | ---: |
| 真实SSE会话 | 2（纯发现1、单次工具会话1） |
| tools/call投递 | 1 |
| 模型 / TTS | 0 / 0 |
| 自动重试/未知重放 | 0 |
| 数据库/部署/现网配置变更 | 0 |

上述是客户端操作和报告记录，不是上游计费账单；没有查询费用，不宣称免费。未对无命中、非法参数、拒绝、限流、远端取消或第二主体追加请求。网页终端输入转义/画面延迟与脚本生成错误单列，不计为服务失败。

## 6. 任务结论与剩余条件

- **D-01完成：**实际初始化、唯一工具定义、必填/约束、版本来源与最小真实请求/响应形状已取得；独立工具版本未暴露如实记录。A1证据已采集，仍待审阅签收。
- **D-02部分：**4.31原文检索语义已有观察；无命中、历史incomplete、工具/协议错误的真实形状及最终模型回答仍不齐。
- **V-01部分：**阶段二Ubuntu固定源码/SDK离线回归完成，MCP配置本轮已定位；实际模型配置/账号授权及当前部署实查仍缺。
- **V-02部分：**直接工具响应不能代替qwen3.8-flash实际选择工具并依据结果续答；没有运行依赖业务库解密的旧加载器。
- **V-03～V-06未完成：**历史不完整原记录、多账号允许/拒绝、生命周期、计费/保留、限流/取消终态与升级规则仍待可靠证据。

当前任务完成7/18，阶段二4/5、阶段三0/6（已有部分进展）；六条最终验收0/6签收。不提交/推送、不发布PR、不关闭Issue、不移除#14依赖。

交付核对：阶段二受测14源码文件和7附件哈希保持一致，6个相关Python文件内存编译通过；12份Markdown相对链接目标存在，`git diff --check`通过；限定4类秘密形状扫描无命中。原报告未取回，不把这些检查写成服务器原件本地复验或完整秘密审计。
