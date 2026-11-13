# 产品 OTA 屏幕校时修复

2026-11-13，Ubuntu `jt@192.168.168.114`。初始基线为 `b4707e327280bf1c3c3e1d4bd5e0c98a298e93ae`。19:28 已完成合并后部署，见下方接续；屏幕实机复验仍待用户确认。

## 原因与修改

用户报告屏幕不显示时间、提示等待服务器同步。Ubuntu 经正常 TLS 主机名访问现网 OTA POST，携带模拟 Device-Id，HTTP 200 的顶层只有 `device_control`，没有 `server_time`。现场留存固件的 `ota.cc` 仅在收到数值型 `server_time.timestamp` 时调用 `settimeofday`；仪表盘在年份无效时隐藏时钟数字并显示“等待网络校时”。本次没有设备串口，未独立读取当前实体设备固件。

产品控制启用时，OTAController 提前返回发现地址，绕过旧路径的校时响应。补回数值型 UTC 毫秒 `timestamp`、数值型分钟 `timezone_offset` 和 IANA `timeZone`；保持控制发现、no-store 及不签发旧语音凭据的边界。

现网 API 容器时区只读核验为 UTC。新增 `device-control.time-zone`，默认 `Asia/Shanghai`，避免依赖容器时区造成八小时偏差；支持 `DEVICE_CONTROL_TIME_ZONE` 配置，按响应时刻计算偏移。没有数据库迁移或固件改动。

## Ubuntu 验证

独立目录 `/home/jt/ota-clock-20261113`，Compose 项目 `ota-clock`，独立 MySQL/Redis 及卷。复用已有离线 Maven 依赖镜像，但移除镜像中的源码和 target，复制当前完整源码重新编译。

- 修复前：`mvn -o -B -ntp test -Dtest=ProductOtaTimeTest -DskipTests=false` 的四个时区用例均在缺少数值型 timestamp 的断言失败（4 failures，0 errors）。首次夹具运行遇到静态 TimeZone mock 影响 Jackson 初始化；修正夹具后才取得上述缺字段证据，最终测试通过显式配置时区，无静态 mock。
- 修复后：`mvn -o -B -ntp test -DskipTests=false`，405 tests，0 failures / errors / skipped。
- `ProductOtaTimeTest`：香港、UTC、印度、纽约四种时区通过，核对毫秒单位、偏移符号/分钟单位、固件加偏移后的本地时间及发现响应边界。
- `CustomerVoicePermissionHttpTest`：25 项通过，其中真实 HTTP 产品发现测试核对数值时间戳、默认 `Asia/Shanghai` / 480、no-store、顶层字段及旧激活拒绝。
- `bash scripts/check-integration-tests.sh` 通过；`mvn -o -B -ntp package -DskipTests` 构建通过。
- JAR SHA-256：`d5339d5ebe1ffc61c4d07e730e11d56bdf786c38c3f1112d8b907b6cda06e66f`。
- 测试结束已停止并移除本任务测试容器和网络，保留隔离卷、日志与构建产物。没有重启或更换现网服务。

源码及原始日志保留在上述 Ubuntu 目录，JUnit XML 和 JAR 位于 `evidence/`。部署后需要设备重新执行 OTA 检查（例如重启），再由用户确认时间恢复；不能把本次回归当成屏幕验收。回退 API 产物不涉及数据迁移，但会再次停止向设备提供校时数据。

## 19:28 合并后上线

用户明确授权审核合并 PR #115。按 code-review 技能执行独立 Standards / Spec 两轴审核，均为零发现；固定审核 head 为 `23641409767e5dc2366514af73e03b232cce0303`。经管理员 squash 合并为 `e708ba21f6d36e6b3dde5879572d57b6dd466eef`。

期间 PR #114 的在线/待机 OTA 已先行合入并部署，因此没有部署原 405 项基线的旧 JAR。完整合并源码在 `/home/jt/ota-clock-merged-20261113` 的独立项目 `ota-clock-merged` 重新执行全套回归：436 tests，0 failures / errors / skipped，集成守卫与 JAR 构建通过。测试容器及网络已停止移除。

- API 镜像：`sha256:d5bf10d9296716476009324f0e82c812d580e7003c13e6dd876139360b968265`。
- 受测 JAR 与镜像内 JAR SHA256：`900400d3437141ca5563bdec5d19e62f2c2b20f308a181f93fcbfbdd879d67e9`。
- 备份：`/home/jt/ota-clock-merged-20261113/rollout/backup`，包括必要身份、固件、操作账本及配置，排除对话和录音；同主机备份不代表异地灾备。
- 只重建 API，平滑重载两个 Nginx。语音 `sha256:578524ae8454c0d69334844a563af35df2c6c7bd73ed550865155fd09176b686`、Web `sha256:f801eab396d85cc78b5dc0e442aaaeaf19816e1c145e133215d3e0d2f53d9db2` 及依赖容器身份保持。
- API 环境、私有配置、设备身份/归属/许可、交付映射、固件及 OTA 操作记录前后摘要一致。没有活动 OTA 任务或新 OTA 下发。
- 就绪检查通过；Ubuntu 本机网卡 443、正常域名证书校验的 POST 发现返回 200 / no-store、数值时间戳在请求时段内、`timezone_offset=480` / `timeZone=Asia/Shanghai`。管理与内部入口、无签名产物下载仍为 404。这是本机 TLS 入口验证，不冒充本轮外网 18443 或物理屏幕验收。

机器证据见 [deployment.json](evidence/2026-11-13-product-ota-clock/deployment.json)。回退参考为 rollout 内保存的原 Compose，仅将 API image 恢复为 `sha256:0af7ce75dc8af5b1f5d21e74913dc90cb032867b473190fc421e2a0c79f90eb5`，核对没有后续发布并重载代理；保留数据库和操作账本。此次校时不新增迁移。

用户决定自行在后台对 B 执行远程 OTA。已确认 B 为 online / voice offline / online_requires_wake，后台候选 `1761d154409f4214b66bd605d70428b7` 的 SHA/ELF 与 [b471c10 构建及目录证据](2026-11-13-powered-device-ota.md)一致。本任务未重复编译相同固件，也未下发升级。首次安装需唤醒旧固件 B；之后目标运行、VALID、身份保持及屏幕时间由实际升级后确认。
