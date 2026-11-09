# 设备控制 v1 首个切片：Ubuntu 验证

## PR #26 修改意见复验（最新）

2026-11-09：业务修复提交 `8b9f497f5bf89ace8f8b3caa6b53d49b34d441b5`，对应 [审查意见](https://github.com/yanghan24/xiaozhi/pull/26#issuecomment-6735842564)。以下为最新结果，后文 218 项记录保留为首轮历史证据。

- P1：使用递进时钟复现响应时间差 1 ms 和截止边界零寿命请求，两个失败用例修复后通过；同一响应只采样一次时间，过期请求返回 410。
- P2：复现成功 ACK 在二维码或许可版本变化后重放失败。新增历史成功表，与最新 ACK 分离；重放不回写最新证据，未知旧 ACK 仍拒绝。针对性回归先出现 3 项失败，修复后全部通过。
- 额外发现全局 Long 序列化把协议整数变成字符串。真实 HTTP 数字类型断言先失败；控制接口改用整数 JSON 节点后通过，管理接口序列化保持原行为。
- Ubuntu 完整 Java 回归 **223 项，0 失败、0 错误、0 跳过**；其中设备控制 HTTP 用例 17 项。集成测试守卫和 API package 均通过。日志：`/home/jt/device-control-v1-build/pr26-full.log`。
- 使用独立 Compose 项目 `xiaozhi-control-review-tests` 的 MySQL/Redis。导出的 created、retried、state、ack 四份真实 HTTP 响应全部通过仓库 JSON Schema；请求时间差严格一致且为正。报文位于构建目录 `main/manager-api/target/device-control-compatibility/`，仅含合成测试数据。
- 两路独立复核（规范轴、规格轴）未发现本轮修复的可操作问题。

本轮 API JAR SHA-256：

```text
014f7554ac4daadf9b252d10878b2261242ec3b2a48c197309a4db71d88c0739
```

### 固件原始模型兼容性

仓库固件分支 `feat/13-device-binding-v1` 的固定提交 `16f6fd5204927ebb8509a7adde1b2edf4efed2df` 已取回，未修改其模型或测试源码。Ubuntu 临时容器使用 GCC 13.3.0、C++17 和 `-Wall -Wextra -Werror`，运行原有 device_control、controlled_audio_access、processing_audio_buffer 三个主机测试，**3/3 通过**。

依赖选用符合固件 `idf >=5.5.2` 声明的 ESP-IDF v5.5.2 portable HTTP parser，以及其 cJSON 子模块提交 `c859b25da02955fef659d658b8f324b5cde87be3`。这里只构建主机测试，没有构建完整 ESP-IDF 固件。

额外 C++ 驱动直接调用未修改的 `Model::ApplyState`、`Model::ApplyRequest`、`ValidateAckResponse`：上述四份真实服务器 HTTP 报文全部接受，blocked 始终不授予语音；将 state 的 expires_in_ms 减少 1 ms 后，原模型仍返回 Invalid。兼容性结论来自实际解析执行，而非放宽固件验证或只检查 JSON Schema。

源码快照、驱动、依赖及日志保存在 Ubuntu `/home/jt/pr26-firmware-check/`，日志为 `host-tests.log`。本轮临时构建容器及专用数据库/Redis 容器、卷已清理；服务端构建日志和报文保留。本轮没有改动部署目录、重启现有服务或刷机；公网 HTTPS、OIDC 和真机交互仍未验收。

## 首轮记录

日期：2026-11-09，Asia/Hong_Kong。基础提交 `9ea32edf536ff510d92f58e77555796ed2d2c49b`（PR #25）；本次业务代码提交 `4df93e5ed159d019aede1a3c83768124e292b5ac`。交付范围见[运行说明](../development/device-control-v1.md)。本记录不代表 #13 完成或产品已上线。

## 环境与结果

通过现有私有 SSH 配置连接 Ubuntu `192.168.168.114`。源码在独立 `/home/jt/device-control-v1-build`，MySQL/Redis 使用本轮创建的 `xiaozhi-baseline-tests` 项目及独立卷。运行环境 `/home/jt/xiaozhi-ubuntu-test` 的配置和数据卷未修改；五个小智容器保持原版本运行，未部署新 API、代理或固件。

以已存在的 Java 测试工具镜像及 Maven 依赖缓存编译当前源码，不把旧镜像内的旧代码作为本次结果。完整回归先从空测试库执行新增和历史 Liquibase 迁移，后续修复在同一独立测试库复验。

最终执行：

```text
mvn -o -B -ntp test -DskipTests=false
Tests run: 218, Failures: 0, Errors: 0, Skipped: 0
BUILD SUCCESS

bash scripts/check-integration-tests.sh
DeviceTest: 2; ModelConfigPersistenceTest: 3; loginControllerTest: 3
集成测试守卫通过

mvn -o -B -ntp package -DskipTests
BUILD SUCCESS
```

最终 API JAR 保存在 Ubuntu 构建目录，SHA-256：

```text
c16b75dda9ffdbc66fe3f8f42bdb5bdc44b07774c2dc9bb4f71a37b49ec8d8a1
```

没有修改前端或 Python 语音服务，本次没有重跑它们的完整套件。实际 HTTPS 证书、代理映射及手机/音箱仍未验收。

## 新增 HTTP 测试覆盖

12 个 `DeviceControlHttpTest` 用例从真实 Tomcat/Shiro HTTP 入口执行，连接真实测试数据库；仅时钟可控。

- 未登记自报标识、错误凭据、跨设备声明、凭据撤销均拒绝；有效凭据读到 blocked 且无 lease/voice。
- 创建/复用请求保留身份及 600 秒绝对期限，重试倒计时减少；创建请求不增加 permission_version。
- 到期旧操作返回 410；新逻辑操作取得新码，过期状态推进 revision。
- ACK 幂等、blocked 要求停播、旧 revision 拒绝，ACK 不创建许可或租约。
- 长轮询等待变化；两套独立 Spring/Tomcat 应用实例共用测试库，八个并发请求只取得一个当前请求；关闭并重建第二实例后读回原请求，旧幂等操作仍指向原结果。此为应用实例重建，不是主机重启或设备断电。
- 幂等键冲突、过时许可版本、设备禁用、resume/account 用途替换均拒绝不适用的旧操作。
- 非整数版本、额外/重复字段、尾随 JSON、非法查询参数、URL 凭据和未知协议版本返回协议错误。
- 缺失/错误 Content-Type 返回 HTTP 400 和字符串 INVALID_REQUEST；超过 4096 字节的分块正文在发送者尚未结束上传时被拒绝。

预登记工具在 Ubuntu 临时私有目录验证：输出目录 0700、文件 0600、设备秘密不出现在 SQL、非法标识拒绝、重复输出拒绝且原文件保持不变。未导入真实设备或写入硬件。

## 失败到修复证据

- 初始无凭据接口返回上游 HTTP 200/数值 401；专用认证过滤器改为 HTTP 401/INVALID_DEVICE_CREDENTIAL。
- 未实现请求/ACK 时没有契约业务结果；逐步实现后，对应真实 HTTP 用例通过。
- 未实现长轮询时立即返回旧快照；实现后能等待并观察事务提交。
- 首次完整回归因辅助应用关闭共享日志系统，导致三个既有日志断言失败；隔离辅助实例日志生命周期后通过，未修改既有业务断言。
- 审查发现缺失 Content-Type 落入上游 HTTP 200/数值 500；新增失败用例复现后，过滤器提前校验，现为 HTTP 400。
- 未结束的超大 chunked 请求原先导致测试读取超时；在 XSS/MVC 之前限制读取字节后，无需等待尾块即可收到 HTTP 400。

## 双轴审查

规范轴：未发现仓库规范或 ADR 违反；一个正文大小限制问题已修复并复核。保留一项非阻塞维护建议：后续状态扩展时可将内部 Map 状态改为有类型的记录。

规格轴：一个 Content-Type 错误封装问题已修复并复核；本次已约定范围内无剩余阻塞项。两路审查均为独立只读复核，执行证据来自上述 Ubuntu 回归。

正式 OIDC、手机确认、allowed 许可/语音令牌、旧语音入口封堵、量产凭据写入和真机停播仍待后续切片；#11 依赖与 #13 未验收项保留。
