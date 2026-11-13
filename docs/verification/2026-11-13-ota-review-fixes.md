# OTA 集成审阅修复验证

2026-11-13，修复提交 `5d5a992`；与集成分支合并后的候选源码为 `4beb8cf64091de7f5f4f9e8d52cf6b2bb1cd3163`。后续合并仅增加验证文档和准备脚本，`main/` 与 `deploy/` 没有变化。对应[独立审阅结论](2026-11-13-ota-management-review.md)。

## 行为修复

批次在全部预检结束后统一获得两分钟确认窗口，同时更新其单台预览期限。在窗口内接受的批次一次性消费确认授权；工作器排队不会再次触发预览过期。批次发送入口核对持久批次仍为 `started`、当前管理员及原固定清单中的可推送目标，再进入原设备预留和发送前检查。单台独立确认仍检查预览期限；原会话、许可、管理员和硬件兼容性检查保持。重启只分类遗留结果，不恢复执行。

目录和推送页面复用带 `AbortSignal`、HTTP 状态和业务错误码的 OTA 客户端，具体中文提示仍由页面决定。Flash 和芯片修订范围的二进制解析与验证移入应用镜像解析器，目录只消费解析结果。

## 验证

全部后端执行位于 Ubuntu `jt@192.168.168.114`，源码目录 `/home/jt/ota-review-api`；完整回归使用独立 Compose 项目 `ota-review-final`，协议验收使用独立项目 `ota-review-protocol`。没有变更 `ota98-api` 页面夹具或现网服务，没有物理设备推送。

[XML 汇总](evidence/2026-11-13-ota-review-fixes/test-results.json)保留每次运行的报告目录、数量及失败测试名称。

| 检查 | 结果与证据 |
| --- | --- |
| Java 全回归、真实 MySQL/Redis/HTTP | 387 项通过，0 失败、错误或跳过；集成测试守卫通过；`.runtime/reports-1794548377` |
| 既有停用语音权限测试独立复核 | 1 项通过，0 跳过；`.runtime/voice-disabled-reports-1794548516` |
| 真实 Java → Python 私有桥接 → WebSocket 模拟设备 | 3 项通过，0 失败、错误或跳过；覆盖两端重启、独立结果和六台批次中断后四台未知／两台未发送，重复确认不重发；`.runtime/protocol-reports-1794548516` |
| 前端现有检查 | 91 单元、10 契约、13 快照通过；组件测试使用真实共享客户端和模拟 HTTP 传输 |
| 前端生产构建 | 通过；仅既有包体、字体预缓存和浏览器兼容数据库提示 |
| 候选 Nginx 配置 | 镜像内 `nginx -t` 通过；无网络容器仅将 `api` 主机名映射为 loopback，未启动管理服务 |

两个新增 HTTP 回归均先观察到产品缺陷：预检过程中时钟推进 121 秒后，原批次就绪时已过期；确认期限前一秒接受五台目标，首台发送期间再推进两秒后，原第五台仅因排队成为 `not_sent / preview_expired`。修复后均通过，重复确认不增加发送次数。使用既有 `deviceControlClock` 注入可控时间，不通过数据库修改期限或查询内部状态来代替 HTTP 行为断言。

原始红灯记录分别保留于 `.runtime/reports-1794547698`、`.runtime/reports-1794547868`；第一项初次绿灯为 `.runtime/reports-1794547777`。首次综合运行发现新测试的 `all_online` 断言误计其他测试留下的离线档案，改为明确选择五个目标，记录保留于 `.runtime/reports-1794548045`。随后完整运行中既有 `CustomerVoiceDisabledHttpTest` 出现 `device_disabled` / `market_auth_required` 差异，记录保留于 `.runtime/reports-1794548222`；未修改相关生产代码，全新数据库完整运行及其独立复核均通过。

协议初次复用完整回归数据库时，原验收断言也计入了其他测试遗留的离线档案：实际六台目标仍为四台 `result_unknown`、两台 `not_sent`，总未发送计数却为 96。失败证据保留于 `.runtime/protocol-reports-1794548388`，改用独立协议数据库重新验证，没有修改协议测试或产品行为。

## 固定候选产物

[候选镜像清单](evidence/2026-11-13-ota-review-fixes/candidates.json)记录源码、镜像 ID、基线镜像 ID 和产物 SHA256。仅构建候选镜像，没有替换运行中的容器。

- API：`xiaozhi-product-api:ota-review-4beb8cf`，直接复制完整回归后打包的 `/home/jt/ota-review-api/.runtime/api.jar`；SHA256 `d075b8867f3b6396f43527be3c11060cbd0904fb23c031ddfa5d38b0e72165e4`。
- 管理页面：`xiaozhi-manager-web:ota-review-4beb8cf`，来自已验证的 `/home/jt/ota-review-api/web-dist.tar`；SHA256 `f88fe3e33de41cede0df3e43d90f73429b83c85da71dddbfed0f6f17e0c44df2`。
- 两个镜像分别以既有 `asr-switch` API/页面镜像的固定 digest 为基础；页面包含当前仓库管理 Nginx 配置。
- Runner：`/home/jt/ota-review-test.sh`、`ota-review-protocol-test.sh`、`ota-review-voice-disabled.sh`、`ota-review-candidate.sh`；原始日志和 XML 保留在上述源码目录的 `.runtime/`。

打包阶段跳过已完成的测试，不作为测试跳过；完整回归 XML 的 skipped 为 0。候选配置准备及最终浏览器复核由主任务接续；物理设备安装、重启与身份保持仍属于 #99 的独立验收。

主任务最终浏览器复核：在 `8cf0da9` 的真实组件及共享客户端上，重新加载已有 `ota98-api` 隔离夹具，目录、98台设备、已持久化批次和两台独立结果正常显示。GitHub Release列表限流时仍可选择已导入固件、刷新历史批次；刷新后升级帧计数仍为1/1/0。该复核验证最终前端与既有API契约，不冒充以最终API重新执行整套浏览器推送；最终后端与协议证据为上表独立回归。期间恢复了临时工作树共享的开发依赖，源代码及Ubuntu候选产物未变更。
