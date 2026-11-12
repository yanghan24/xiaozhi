# PR #79 修复部署与语音重启核验

2026-11-12（Asia/Hong_Kong），服务器 `jt@192.168.168.114`。管理员在本会话明确同意合并 [PR #79](https://github.com/yanghan24/xiaozhi/pull/79) 后，12:57:57 squash 合并为 `7864e254a69167c309b59256afe72de3c5a77c90`。合并文件树与已复审、受测的 `f830a8c34393d4befd13263d2ca7b809ac24252f` 完全一致。

12:58 执行部署，语音容器于 12:58:41 启动，13:00:08 首次只读核验通过。维护通道的异常／取消结果分类与最终会话检查修复已部署；随后用户确认 B 已唤醒、原账号正常并完成一次完整回答，13:08:39 服务器确认当前唯一产品会话、MCP 就绪和设备状态保持。本轮没有再次下发 OTA，没有重启产品 API 或 Ubuntu 主机。

## 受测与运行产物

| 项目 | 固定值 |
| --- | --- |
| 原语音镜像 | `xiaozhi-product-voice:p4-maintenance-20261112` |
| 原镜像 ID | `sha256:3e347b8397c4bf8a19d62cd29fb169a0dd4eeb6afdb9395a15c66833cdc974b9` |
| 受测及运行镜像 | `xiaozhi-product-voice:pr79-f830a8c` |
| 受测及运行镜像 ID | `sha256:19070d27ddd2332d43799c3caff669a6a131f180f98d500ef37824c3cc1bd7ca` |
| `core/product_maintenance.py` SHA-256 | `d5544dc41dbc62047b3da581ccb74b038a7b2c16ae4a82e598e598d414a91277` |
| `core/product_voice.py` SHA-256 | `c131613f028e89e2adba0ae4b4781b624ea3c3ae1e891dc2d43acfb797c3a57c` |
| 产品 API | `xiaozhi-product-api:1b50709`，本轮保持原容器与启动时间 |

12:53 从实际运行镜像派生候选，同时覆盖上述两个修复模块，保留原 WebSocket 接入代码、系统提示词及配置。两份模块统一为 UTF-8/LF，与[失败回归及修复证据](evidence/2026-11-12-p4-ota-server/maintenance-review-fixes.json)的 hash 一致。

候选镜像内直接执行 `python -m unittest discover -s /review-tests -p test_product_maintenance.py -v`，15 项通过，0 失败／错误／跳过。测试使用 root 容器以覆盖真实 Unix socket，容器网络为 `none`、根文件系统只读，测试文件只读挂载；两份被测模块从候选镜像 `/app/core/` 导入，未用源码挂载覆盖。语法、导入路径、源码 hash 和原 WebSocket 代码保持均核对通过，见[候选准备证据](evidence/2026-11-12-pr79-deployment/candidate-preparation.json)。这些测试没有挂载现网配置或向设备发送命令。

## 实际部署与核验

部署前重新保存 Compose、服务详情、设备状态和原 OTA 预留，并成功执行现有 `backup.py`。备份和原始快照位于服务器私有目录，未进入 Git。脚本核对现网配置、原镜像 ID 和原预留与准备时一致后，只修改 Compose 中 `product-voice` 的镜像，使用 `--no-deps --no-build --pull never` 重建语音服务。

[部署结果](evidence/2026-11-12-pr79-deployment/deployment.json)与[13:00 只读复核](evidence/2026-11-12-pr79-deployment/post-deployment-audit.json)确认：

- 实际语音镜像 ID 与受测候选一致，运行中的两个修复模块 hash 正确，Compose 与已检查的候选配置一致。
- 产品 API、MySQL、Redis 和 TLS 入口保持原容器 ID 及启动时间；只有语音容器重建。
- API 内部健康返回 200，本机连接 Ubuntu 443、使用正式域名及正常证书校验的产品发现返回 200。这不是新的公网 18443 探测。
- 维护 root Unix socket 能处理真实 status 请求；13:00 返回 `no_unique_session`，当时没有可供本通道使用的唯一活跃 B 产品语音会话。该结果证明 status 通道响应，不能据此声称设备已重连或完整语音通过。
- B 仍为 `ready / admitted=1 / local_enabled=1 / permission_version=19 / ownership_version=7`，有归属，当前凭据匹配唯一 delivered 补发记录。
- 部署前后全部已记录设备身份／凭据／归属／许可字段、补发记录和硬件映射快照完全一致，包含 A；原 `e029d00a3ade4616adebfce862108b73` OTA 预留逐字节保持。

本次对比以部署前新采集快照为基线，证明当前设备状态在这次语音重建后保持，不补成历史补发瞬间的归属／许可证据。服务健康与 status 响应也不代替硬件扫码和完整语音确认。

## 部署后 B 功能确认

用户在请求审查 PR #83 时直接确认：“我已唤醒b，确认原账号，并完成一次完整回答”，见[现场确认记录](evidence/2026-11-12-pr79-deployment/post-deployment-functional-confirmation.json)。实际操作时间未提供，不用服务器采集时间代替；服务器端没有独立采集 UART 或音频。设备为原 B，板型及最后确认的目标固件沿用 [B OTA 记录](2026-11-12-p4-ota-server.md)，本轮没有新增固件写入或 Flash 读回。

13:08:39 的[服务器只读核对](evidence/2026-11-12-pr79-deployment/post-functional-server-state.json)返回维护 status=`current`、`mcp_ready=true`，确认 B 当前唯一产品语音会话及 MCP 就绪。B 仍为 ready／许可19／归属7，当前凭据匹配 delivered 补发记录；全部已记录设备状态、补发、硬件映射及原 OTA 预留与部署前一致，服务镜像和启动时间与部署后相同。服务器证据的 `physical_voice_acceptance_performed=false` 表示自动核对本身未执行实机动作；完整回答来自上述用户确认。

13:00 的 `no_unique_session` 保留为当时观察，由后续当前会话和用户现场反馈补齐本次部署后的功能确认。这些结果支持 B 在新语音部署后保持原账号并完成一次完整回答，不代表额外的固件、停用保持或异常恢复验收。

## 回退与剩余验收

私有部署记录位于 `/home/jt/issue42-pr79-deploy-f830a8c/deployment-20261112T045829Z`，包含 `compose-before.json`、状态快照、原预留副本和备份／Compose 执行日志。原镜像及其 ID 见上表。回退仅恢复本次语音镜像字段并重建 `product-voice`，先比较当前配置以保留后续其他修改；保留原环境、密钥、持久目录、设备状态和全部操作记录。脚本具备失败时恢复原语音配置的处理，本轮部署成功，没有执行回退，也未演练数据库恢复。

原 [B OTA 与正常恢复出厂验收](2026-11-12-p4-ota-server.md)结论保留，11:20 实时下载／写入／自动重启日志缺口也保留；不为此次修复重新开放下载或重发 OTA。新部署后 B 的当前会话、原账号及一次完整回答已按上述来源确认。产品 API／Ubuntu 主机重启、后台停用后的恢复出厂保持、异常身份丢失后完整恢复及中断／迟到请求边界不属于本轮通过项；#42／#13 保持打开。
