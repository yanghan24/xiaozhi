# 阶段四清理/SDK修复证据（81条，保留变体）

> 该目录保留另一版脚本和报告字节，不是当前工作区的最终依据。当前脚本已统一并在两端复验，唯一当前入口为[verified-sdk81-20261112](../verified-sdk81-20261112/README.md)；本目录下的轮次/哈希不得和本轮verified81容器结果拼接使用。以下为保留的既有记录，本轮未将其原报告下载或重新运行。

日期2026-11-12。本轮修复两项可证问题并补齐回归，**产品源码字节已变**，不能再用旧75/79条源码哈希声称已验证。

| 问题 | 修复 |
| --- | --- |
| 清理取消/截止后丢失未完成worker引用 | 仅当worker已结束才清空attempt/task；取消后二次等待仍pending则抛TimeoutError，manager不误报成功 |
| SDK数值超时报文被包装，测试只看错类型 | 产品把`timedelta`转`total_seconds()`再交SDK；回归接受`TimeoutError`或`MCPError(code=-32001, "…timed out")`两种超时形状 |

## 运行结果

| 环境 | 轮次 | 结果 |
| --- | --- | --- |
| 本机CPython3.12.10 / SDK stub | local-fixed03 | 81运行/81通过/0失败/0错误/0跳过 |
| 同上前提 | local-fixed04 | 同上，case与source一致 |
| Ubuntu Python3.10.22 / 安装MCP2.3.0（合成IO） | installed02 | 81/81 |
| 同上 | installed03 | 81/81，case与source一致 |

Ubuntu容器`issue12-sdk81-installed02/03`：`--network none`、`--read-only`、`--cap-drop ALL`、`no-new-privileges`、非root、无私有配置/生产凭据/数据库挂载；固定镜像`sha256:a0ce61af…a31e92c5`。两容器均`Running=false`、`ExitCode=0`、`NetworkMode=none`、`ReadonlyRootfs=true`。

## 哈希

| 对象 | SHA-256 |
| --- | --- |
| 本机两轮`result.json`（相同） | `502dea858d6d27d9cd8c4c2f1e475d0227a50ee18a65bbb5aa58631f24710436` |
| Ubuntu两轮`result.json`（相同，原件留服务器） | `d0f227832a4b5da5fdf3289c72cb3506c5ff791a51194e0343b22a6b2930baea8` |
| 最终14文件包`issue12-sdk-fix81-final.tar.gz` | `95eaf8297d342f66271deafc2bbebef72f5a8fff780fef983481af063af6bfc9` |
| 诊断脚本 | `3e4d5b762b3d9dfb8db512993b67b93d40d423a1dd4d4aeebbf7d1b04ed1b2dc` |
| 归档附件 | 见`archive-sha256.json` |

Ubuntu原报告未取回本机，本地只保存现场观察和哈希；不伪造服务器原件。本机报告与`source-manifest.json`逐文件一致。

## 边界

仍为合成传输/会话，非真实SSE集成；无模型/TTS/账号授权/远端取消证据。产品API与语音在本轮未重建（仍是部署时镜像与启动时间），没有业务数据库或现网配置变更。
