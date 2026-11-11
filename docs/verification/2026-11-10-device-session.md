# #13 原账号设备页与单台退出验证

日期：2026-11-10，Asia/Hong_Kong。基线 `34d631c`，实现 `84359ee`。沿用 Ubuntu `192.168.168.114` 的 `/home/jt/issue13-integration` 和 `issue13-*` 独立测试容器、实际 MySQL/Redis；保留已有部署与私有配置，不操作生产数据库。

## 交付

原账号 account 请求最终校验与消费、设备页面、单台退出事务、幂等重放和操作 stored/device_acknowledged/superseded 查询。页面和 API 都不把退出保存等同于音箱已停播。具体范围及阶段限制见[设备页面说明](../development/customer-device-session.md)。

## 红绿与执行证据

- 首条 sign-out HTTP 测试在实现前无法取得 v1 退出结果，失败；新增接口、事务及持久回执后通过。
- open-account 原账号边界测试在实现前错误落入旧管理 HTTP 200 响应，预期 403 的断言失败；实现后其他账号被拒绝，原账号消费请求而不改变许可版本。
- 手机页面测试在实现前找不到“账号与绑定”入口；补齐页面/表单后通过。测试改用不自动跟随重定向的 Java HTTP 客户端核验 303，再读回退出页面。
- **完整 Java 308 tests / 0 failures / 0 errors / 0 skipped**，2026-11-10 01:04:20 UTC 完成。执行 `mvn -o -B -ntp test -DskipTests=false` 及 `bash scripts/check-integration-tests.sh`；守卫通过。CustomerBindingHttpTest 共 39 项，CustomerBindingRestartTest 共 2 项，相比前一切片增加 19 项。
- Ubuntu Chromium 经隔离 Nginx 实际点击绑定、设备页、退出按钮并刷新，全部通过。结果见[浏览器 JSON](evidence/2026-11-10-device-session/browser-result.json)。已检查截图文字及布局。

![退出已保存，等待设备确认](evidence/2026-11-10-device-session/device-signed-out.png)

完整日志保留 `/home/jt/issue13-integration/.runtime/full-tests.log`，浏览器证据保留 `.runtime/session-browser-evidence`。

## 核心覆盖

- 原账号与用途校验；请求过期、替换、许可变化或凭据失效时不能进入设备页；成功 account 重放不再次消费或改变许可。
- 同账号两台设备，退出 A 保留 A 归属和 B 状态、共享授权；不因旧页面或登录回调隐式恢复设备。
- 原浏览器、CSRF、精确 Origin、30 分钟闲置、后通道注销、当前准入/凭据/归属代际验证；设备页和操作 GET 不延长闲置期限。
- 同键并发只有一个许可版本推进；真实 MySQL 回执写入失败使退出整体回滚；修复后同键可重试。
- 当前版本停播 ACK 才报告 device_acknowledged；版本变化后为 superseded，旧 ACK 重放和历史退出重放均不能改变后来状态。
- 另一应用实例的状态请求可观察退出；应用关闭重建后同一归属、退出状态及幂等回执保留，原二维码过期不使有效客户页面失效。

## Standards

无硬性规范违反或已确认安全问题。1 项非阻断建议：CustomerV1Controller 的 confirm/open-account/sign-out 重复空 JSON 校验与数值响应封装，可后续提取公共方法。当前行为已通过同一 HTTP 过滤链验证。

## Spec

本切片无可行动缺陷或范围扩张。原账号、许可版本、原子消费/退出、重放和 ACK 规则符合协议；语音恢复与跨服务输出保护仍在明确的后续范围内。

双轴复核结论：Standards 0 硬性问题、1 非阻断建议；Spec 0 缺陷。复核为独立只读源代码审查，执行结果由主验证提供。

## 未验收范围与回退

所有设备仍 blocked，不签发 voice/lease；本轮没有实际语音链路、实体音箱、真实手机或公网部署。ACK 使用认证设备 HTTP 模拟，不能替代音箱清队列和物理停播证据。resume、当前共享授权变化传播、语音输出保护及旧入口封堵继续待实现，#11/#13 不关闭。

保留新增 202611101000 迁移及操作表；应用回退需关闭 device-control，不能用 Git revert 撤销持久退出或恢复二维码。测试结束仅停止 issue13-proxy/runner/redis/db，保留数据和日志。