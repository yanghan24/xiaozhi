# #42 服务端补发回归与 P4 接续

2026-11-11，Asia/Hong_Kong。从最新 `origin/main` `cdd24a5595d7c04ded3ce8b8547a27b59b846cce` 建立 `codex/42-device-identity-recovery`，受测代码提交 `d10157d`。后续提交只整理本文及协议说明。

本轮完成管理员授权补发服务端切片。用户明确将实机阶段留到 P4 所在电脑继续，#42 保持未验收，PR 保持 Draft；合并前仍须管理员针对 PR 明确确认。

## Ubuntu 验证

执行主机 `jt@192.168.168.114`，源码和私有运行日志位于 `/home/jt/issue42-recovery-cdd24a5`。测试 Compose project 为 `xiaozhi-issue42`，独立 MySQL/Redis 和新生成测试配置，使用已有离线 Maven 依赖。没有在现场身份库执行回归或修改真实设备。

| 项目 | 结果 |
| --- | --- |
| 第一条补发测试，未实现前 | 原 16 项交付测试通过，新增补发测试失败；未知路径旧通用响应虽为 HTTP 200，但缺少协议 data，断言正确发现未实现 |
| 写回回执测试，未实现前 | 新增 complete 预期 409 却得到未知路径响应 200，失败；随后实现通过 |
| 聚焦 HTTP 回归 | DeviceDeliveryHttpTest 20 项；CustomerVoicePermissionHttpTest 19 项，全部通过 |
| 完整 Java 回归 | **357 项，0 failures / errors / skipped** |
| 集成测试守卫 | 通过，确认真实集成套件执行 |
| 隔离 Nginx `nginx -t` | 通过；首次夹具遗漏 customer-routes.conf 挂载，补齐已有文件后通过，未修改现场入口 |
| 源码对应 | 8 个变更源码/测试/迁移/入口文件的 LF 规范化 SHA-256 与提交 checkout 全部一致 |
| 测试资源 | 回归 MySQL、Redis、API 测试容器均已停止；保留隔离卷及日志供追溯 |

完整回归命令为 `mvn -o -B -ntp test -DskipTests=false && bash scripts/check-integration-tests.sh`，在隔离测试容器执行。XML 位于 `.runtime/issue42/reports-20261111T062851Z`；先前聚焦结果位于 `reports-20261111T062606Z`。机器可读[脱敏证据](evidence/2026-11-11-issue42/regression.json)记录完整套件计数与受测文件校验值。

真实 Tomcat/Shiro/MySQL/Redis 测试覆盖管理员补发、普通账号/匿名/短期及长期工具凭据拒绝、同一操作重放、六路不同操作竞争、六路同操作重试、连续轮换、跨设备前驱拒绝、旧交付重放/迟到完成拒绝、匹配硬件验证和写回回执续作。客户测试通过正式 HTTP 绑定两个设备，补发后原账号查询保持一致，旧语音令牌和已打开会话在内部 HTTP 闸门被拒绝；退出后再次补发不会恢复本台许可，迟到扫码恢复被拒绝，另一台仍 allowed。

外部集市在回归中由受控边界替身提供。没有连接真实集市客户账号，没有在本轮启动 Python WebSocket 传输测试，也没有采集物理停播；不能将内部语音 HTTP 闸门结果扩大为真实 WSS、ASR/LLM/TTS 或扬声器验收。重试及持久记录经过 HTTP 回归，但本轮未执行补发后的专门服务重启验收。

## 现场与回退边界

只读实查现场 API 仍为 `xiaozhi-product-api:1468eeb`，语音为 `xiaozhi-product-voice:prompt-7f10cbd`。本轮没有部署候选，没有重启现场服务或变更公网地址，因此[当前部署入口](../deployment/current.md)保持原状态。

补发一旦执行，旧凭据即失效；回滚代码不能恢复它。旧 API 不理解补发记录，会使已补发设备的 device-check 失败。暂停新增补发应撤回 recovery 路由，保留支持新凭据的 API、数据库及原加密密钥，详见[协议与恢复边界](../development/device-credential-recovery.md#部署与回退)。

## 独立审查

**Standards：0 项。** 未发现违反仓库规范的变更或值得改动的代码异味；新增迁移、领域用语、后台归属职责及回退边界符合规范。

**Spec：0 项服务端缺陷。** 授权、原档案/硬件/归属保持、原子轮换及旧材料拒绝符合本切片要求。审查指出补发 device-check 可验证禁用设备，但控制/语音准入继续拒绝；已在协议中明确该差异。两名审查代理仅审查源码，测试结果以本报告机器证据为准。

## 尚未完成

- 固件正常维护身份保留实现、实际分区兼容/迁移、NVS 修复清理范围及秘密物理保护核查。
- 实际固件/烧录工具补发写回与中断续作的集成。
- 真实 P4 的应用重刷、OTA、重新配网、恢复出厂、异常丢失、补发写回、旧凭据拒绝及重启验收。
- 真实 WebSocket 传输、物理停播与补发后的服务重启证据。

下一阶段直接从[设备凭据恢复说明](../development/device-credential-recovery.md#p4-接续与维护边界)继续，固定实际 `00896ab` 固件 checkout、sdkconfig、烧录布局和产物后实施；历史独立 NVS 命名空间记录不等于已证明可抵御整分区清理。未验证 eFuse、Flash/NVS 加密、Secure Boot 或不可提取保障。
