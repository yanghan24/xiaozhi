# 当前受测版本：6bf7620 基线的 81 条回归

日期：2026-11-12。Issue #12 于当日 07:50 更新，正文改以 `6bf7620` 作为本项目既有实现的比对提交。本目录是该基线上的回归证据，替代 `verified-sdk81-20261112` 作为当前依据；旧目录保留原字节，不作废。

## 与前一版的关系

- 分支 `feat/12-mcp-knowledge-contract` 已同步到 `origin/main = 6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80`。
- 主线两个提交（#79、#83）只改 `product_voice.py`、`websocket_server.py`、`product_maintenance.py` 及其测试与部署文档，未触及 `server_mcp/` 三个模块。
- 相对上一版受测集，唯一实质变化是 `product_voice.py`：`GuardedSocket.send` 新增可选 `before_send` 参数（默认 `None`，同步回调，无行为改变），不在 9 条本地许可 guard 测试路径上。
- `errors.py`、`check-mcp-offline.py` 的哈希差异经逐字节核对为**仅换行符**不同，内容一致。

按 Issue“已有证据按版本和覆盖范围复用、只补缺口”的口径，不重跑全部历史轮次，只在新基线重做本机与 Ubuntu 各两轮并同步该文件。

## 结果

| 环境 | 轮次目录 | 运行/通过/失败/错误/跳过 |
| --- | --- | --- |
| Windows CPython 3.12.10，SDK stub | `local01` | 81/81/0/0/0 |
| 同上 | `local02` | 81/81/0/0/0 |
| Ubuntu Python 3.10.22，安装 MCP 2.3.0 | 服务器 `installed01` | 81/81/0/0/0 |
| 同上 | 服务器 `installed02` | 81/81/0/0/0 |

- 本机两轮 `same_cases=true`、`same_sources=true`；Ubuntu 两轮同样一致，`guard_denials={}`。
- 81 个方法 = 原 75 个全部保留 + 6 个新增；无删除、无 xfail/expectedFailure/skip。
- 重复轮次不累加为不同用例。

Ubuntu 容器 `issue12-main6bf-01/02`：固定已有镜像 `sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5`，`--network none`、`--read-only`、`--cap-drop ALL`、`no-new-privileges`、`user 1000:1000`、768MiB/1CPU，仅挂受测源码与新结果目录，无私有配置/生产凭据/数据库。两容器 `exit=0`、`running=false`。

## 固定哈希

| 对象 | SHA-256 |
| --- | --- |
| 受测包 `issue12-main6bf81.tar.gz`（57,244 字节，14 文件） | `26aa8a9a7e378e20b3b19362a6d263d0a525a6efc900d61a1e6c368476eb640d` |
| `source-manifest.json` | `014ff459c1d48e0940c9d94d9515efb3275f2ae992e958a5fcda0d27a08ddeb2` |
| 本机两轮 `result.json`（相同） | `dc402dfa51d72f30f7f9ded45a59d57e81694a7858d508362e3c73cff897097d` |
| 附件核对 | 见 `archive-sha256.json` |

Ubuntu 原 JSON 留服务器 `/home/jt/issue12-main6bf-20261112/results/installed01|02`，两轮哈希现场读取相同；本机未持有该值副本，故不抄录。结果内 `source_sha256` 与 `source-manifest.json` 逐文件一致，14 文件 0 不一致。

本地报告可能因检出的换行符与服务器记录不同而字节不同；核对时使用 CRLF→LF 归一化后的哈希，不把换行差异当内容差异。

## 边界

除新增的实际 SDK 内存流超时用例外，传输/会话仍为合成，不构成真实 SSE 集成验收。真实客户端在 host 网络下的一次初始化/发现与一次工具调用成功记录保留在[缺口修复实测](../../../2026-11-12-mcp-gap-fixes.md)。模型选工具续答、A/B 授权、计费、限流与远端取消仍未验证。
