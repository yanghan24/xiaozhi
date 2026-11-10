# 当前部署与公网入口

本页是查找小智服务端地址、部署版本和运行方式的固定入口。最近部署核验：2026-11-10（Asia/Hong_Kong），依据 [PR #26 Ubuntu 部署记录](../verification/2026-11-10-pr26-server-deployment.md)。这是最近确认的部署状态；操作服务器前仍须检查实际运行状态。

## 手机与音箱访问

| 用途 | 当前地址 |
| --- | --- |
| 公网 HTTPS origin | https://bridge.hncig.cn:18443 |
| 音箱 OTA 产品发现 | https://bridge.hncig.cn:18443/xiaozhi/ota/ |
| 设备控制状态（GET） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/state |
| 创建绑定/恢复请求（POST） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/requests |
| 停播确认（POST） | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/state-acks |
| 语音 WebSocket | wss://bridge.hncig.cn:18443/xiaozhi/v1/ |
| 手机绑定页 | 设备发码返回的 `https://bridge.hncig.cn:18443/xiaozhi/bind/{handle}` |

手机扫描音箱生成的有效二维码进入绑定页，通过集市确认身份后完成设备操作。公网根路径返回 404 是预期行为；没有可代替设备二维码的通用绑定首页。设备控制需要独立接入凭据，语音连接需要当前设备许可签发的产品令牌。OTA 地址用于产品发现，不代表已经提供固件下载。

## 部署版本与运行方式

| 项目 | 最近确认值 |
| --- | --- |
| Ubuntu / SSH | `jt@192.168.168.114` |
| 源码 | PR #26，`885918d24d69f0b2ed87114f1937292727111d4e` |
| 部署目录 | `/home/jt/pr26-deployment-885918d` |
| Compose 文件 | 部署目录内 `private/compose.json` |
| 产品 API | `xiaozhi-pr26-api` / `xiaozhi-product-api:885918d` |
| 产品语音 | `xiaozhi-pr26-voice` / `xiaozhi-product-voice:885918d`，单实例 |
| 公网入口 | 既有 TLS Nginx，公网 18443 映射到 Ubuntu 443 |

服务、回归和设备协议验证在 Ubuntu 执行。授权 sudo 操作前按私有凭据说明使用已有凭据通道；本页不存放密码或密钥。在服务器部署目录查看状态：

```bash
cd /home/jt/pr26-deployment-885918d
sudo docker compose -f private/compose.json ps
```

启停或恢复前先读部署记录的[实际部署](../verification/2026-11-10-pr26-server-deployment.md#实际部署)、[备份与运维](../verification/2026-11-10-pr26-server-deployment.md#备份与运维)和[暂停与恢复原则](../verification/2026-11-10-pr26-server-deployment.md#暂停与恢复原则)。该记录还列出外部数据库、Redis、旧管理配置 API、代理可信地址与持久失效通知目录等依赖；仅启动产品 Compose 不能代替这些依赖的检查。JetLinks 保持停机。

PR #26 本次部署已完成服务端回归、公网协议检查和容器服务重启验证；其真实手机最终确认、产品固件麦克风到扬声器闭环、物理停播 ACK、Ubuntu 主机重启和并发容量尚未由该部署记录验收。另有 [P4 基础固件真实语音与恢复记录](../handoff/2026-11-10-device-firmware.md)，对应测试智能体和当时固件，不能直接视为 PR #26 产品绑定协议的验收。

## 本地资料缺失或过时

本地分支/worktree 不随 GitHub 合并自动更新。找不到本页、入口仍指向历史报告，或文档状态互相矛盾时，先核对 GitHub 最新 `main` 的[本页](https://github.com/yanghan24/xiaozhi/blob/main/docs/deployment/current.md)和它引用的部署证据，再判断资料是否缺失。

无需切换工作分支即可只读获取：

```bash
gh api -H 'Accept: application/vnd.github.raw+json' 'repos/yanghan24/xiaozhi/contents/docs/deployment/current.md?ref=main'
```

随后用相同方式读取本页引用的证据文件。GitHub 暂时不可访问时，说明当前只能依据哪一版本地记录，恢复访问后再核实；保留原分支与未提交修改。

## 维护与历史边界

部署地址、版本或运行方式改变时，在归档日期报告的同一个文档 PR 中更新本页，链接新的验证证据，并复查 README 与 AGENTS.md 能到达本页。只有完成这条导航链核验，才算完成部署文档交付。日期报告保留当时事实；当前状态统一在本页维护。

[2026-11-06 独立测试部署流程](ubuntu-isolated-test.md)用于复现旧基线，其中旧镜像、端口及“公网映射未开通”等描述仅属于当时环境。历史文档与本页不一致时，以本页所链接的较新部署证据为准，并在执行前核对服务器。
