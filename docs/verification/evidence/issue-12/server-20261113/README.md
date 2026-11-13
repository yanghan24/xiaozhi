# Ubuntu 服务器回执

见[验收说明](../../../2026-11-13-mcp-server-acceptance.md)。本目录只含可共享证据；真实账号／设备对应、MCP 凭据和完整工具正文保存在 Ubuntu。

| 文件 | 来源与边界 |
| --- | --- |
| `audit.json` | 本次只读核验输出；`new_mcp_calls=0` 仅指核验阶段，后续查询另见 live-summary |
| `installed01-result.json`、`installed02-result.json` | 两份历史 Ubuntu 原件的逐字节副本，各 81 项；本次未重跑 |
| `live-summary.json` | 09:59 新查询的脱敏投影，明确不是原始报告，包含原件路径及 hash |
| `audit.snapshot.py` | 成功核验的脚本快照；保留来源，不作为通用重跑工具 |
| `live-once.snapshot.py` | 实际执行一次的探针快照；真实客户端／SDK，日志屏蔽，正文只存后端 |
| `archive-sha256.json` | 本目录证据文件的原字节 SHA-256 |

服务器目录 `/home/jt/issue12-minimal-verification-20261113T015751Z`。历史 A2 哈希与实读值的差异保留于 audit.json。`scope-private.json` 保存本次用户确认和真实 B 归属的对应，未取回或提交。
