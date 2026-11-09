# 设备控制 v1：服务器首个切片

依据 [PR #25 的契约](../contracts/device-binding-v1.md) 和 [#13](https://github.com/yanghan24/xiaozhi/issues/13)。本切片交付设备准入，以及 `state`、`requests`、`state-acks` 三个 HTTPS 接口。默认关闭，尚不用于产品上线。

## 范围

- 凭据使用 32 字节安全随机数；服务器只保存 SHA-256 校验材料，以唯一哈希定位设备，再核对 Device-Id。设备标识不作为秘密。准入档案与上游 `ai_device.user_id` 分开，避免把运营账号当作集市归属账号。
- 状态、请求、幂等结果和最新 ACK 保存在新增 `dc_*` MySQL 表。修改以设备行锁串行化，业务结果与幂等记录同事务提交。请求原期限为 600000 ms，同用途复用；错误重试、换用途和版本变化不能恢复旧码。操作结果目前不自动清理，保留超过契约要求的 24 小时；后续清理必须保留最低期限及请求引用完整性。
- 长轮询最多请求 20000 ms；每 200 ms 读取权威数据库，过期时推进 revision，跨实例无需依赖可能丢失的 pub/sub。每实例最多 64 个等待者，超出返回 429 和 Retry-After。等待期间不占数据库事务；凭据撤销在后续读中重新验证。该方式是首个切片的有限规模实现，未做 #19 容量验收。
- 此切片仅支持 `blocked` 状态：未绑定、单台已退出、需重新授权、设备禁用。数据库约束不允许写入 `ready`；没有签发语音令牌或授予 allowed 的路径。已绑定状态的测试数据只用于验证 resume/account 请求规则，不代表 OIDC 绑定已实现。
- ACK 必须匹配当前 revision 和 permission_version，且 blocked 时必须 audio_stopped=true；重复 ACK 返回相同结果。旧 ACK 返回 409 STATE_CHANGED，不改变当前许可。后续客户操作查询须将 ACK 的许可版本与操作及设备当前版本一起比较，不能仅凭 ACK 行存在认定停播。

手机绑定页、OIDC、allowed 租约/语音令牌、OTA 发现扩展、旧 OTA/语音入口封堵、固件停播，以及正式凭据写入/补发工具尚未交付。生成的 verification_uri 固定了协议格式，但手机页面仍不可用。本切片不能作为完整产品准入或关闭 #13 的依据。

## 独立验证配置

在 Ubuntu 的独立构建/测试目录运行；现有 `/home/jt/xiaozhi-ubuntu-test` 保留私有配置和数据卷。固定提交源包或独立 Git checkout 用于构建，构建目录不覆盖运行目录。

Spring 配置：

```properties
device-control.enabled=true
device-control.public-origin=https://实际域名:实际端口
```

`PUBLIC_ORIGIN` 环境变量可作为 public-origin 的后备来源。origin 不含路径、尾斜杠、用户名、查询或 fragment；必须是 ASCII HTTPS origin，且生成二维码 URL 不超过 512 字节。开启功能但地址不合法会拒绝启动。生产域名、证书、OIDC 登记仍由 #11 落实。

接口检查 `request.isSecure()`。直接 TLS 终止或受信代理均可；代理终止时，设置 `server.forward-headers-strategy=native`，将 `server.tomcat.remoteip.internal-proxies` 限制到实际代理 IP 的精确正则，并由代理覆盖 X-Forwarded-Proto。后端仅允许可信代理访问，不能把任意客户端声明的转发头当作 HTTPS 证明。本次 HTTP 集成测试仅在测试 JVM 的回环地址模拟可信代理，不是公网 TLS 验收。

测试环境启用该控制路径不等于允许开放现有产品 OTA/WSS 入口。上线须先完成全部语音准入闸门和真实验收；当前 LAN Nginx 模板没有增加新路径转发。

## 预登记准备

仅由有数据库管理权限的运营/交付人员，在 Ubuntu 私有目录执行：

```bash
umask 077
python3 scripts/prepare-device-credential.py --device-id speaker-test-001 --output /私有目录/speaker-test-001
```

工具拒绝覆盖已有输出目录，生成权限 0600 的 `device.json` 和 `registration.sql`，目录权限 0700。SQL 仅包含校验哈希，INSERT 冲突会失败，不会替换已有设备。通过已有私有数据库管理通道导入 SQL，确认成功后才交付该设备对应的 JSON；不把文件放进 Git、普通日志或客户网页，不上传通用固件。此工具不连接数据库、不烧录设备，也不是批量交付或补发凭据方案。

撤销校验材料会使旧凭据认证失败；正式补发必须在受控事务内保留归属、更新凭据和许可代际、推进 revision 并使旧请求失效。尚无对外运营补发接口，不能自行把新 INSERT 当作恢复旧设备。

## 测试

沿用 [Ubuntu 测试约束](../deployment/ubuntu-isolated-test.md)。`compose.tests.yaml` 使用独立 `xiaozhi-baseline-tests` 数据库和 Redis，不能指向正在运行的管理数据库。新源码构建 api-tests，已有离线依赖可用于构建加速，但必须编译本次源码。

容器中的针对性命令：

```bash
mvn -B -ntp test -DskipTests=false -Dtest=DeviceControlHttpTest
```

完整 Java 检查：

```bash
mvn -B -ntp test -DskipTests=false
bash scripts/check-integration-tests.sh
```

测试从真实 HTTP/Tomcat/Shiro 入口观察结果，使用真实 MySQL/Redis，只有时钟可控。两套独立 Spring 应用实例连接同一测试库，验证并发创建、跨实例长轮询，以及关闭/重建第二实例后的请求读回。数据库写入仅用于预置登记、归属和撤销测试条件，不将这些夹具当作手机或运营功能已实现的证据。

新增 Liquibase changeSet 只建表，不修改上游表或历史迁移。回退代码时保留新表及设备数据，旧版不读取它们；Git revert 不删除已登记凭据。新增凭据和状态仍需纳入私有数据备份。没有运行数据破坏性迁移，也没有自动更新现有服务的流程。
