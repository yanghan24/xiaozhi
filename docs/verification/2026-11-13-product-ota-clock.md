# 产品 OTA 屏幕校时修复

2026-11-13，Ubuntu `jt@192.168.168.114`。基线为 `b4707e327280bf1c3c3e1d4bd5e0c98a298e93ae`。本记录是源码及隔离回归结果，尚未部署或完成屏幕实机复验。

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
