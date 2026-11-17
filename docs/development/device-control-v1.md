# 设备控制 v1：准入与手机身份接入

> 2026-11-10 后续实现：恢复许可、语音闸门和直连集市模型见[产品语音说明](customer-product-voice.md)。本文保留前一切片的范围和证据。

依据 [设备控制契约](../contracts/device-binding-v1.md) 和 [#13](https://github.com/yanghan24/xiaozhi/issues/13)。本切片交付设备准入、`state`、`requests`、`state-acks` 三个 HTTPS 接口，并将正式发码接到 #29 手机身份模块。控制功能默认关闭，尚不用于产品上线。最新集成结果见[接入验证](../verification/2026-11-09-device-identity-integration.md)。

Ubuntu 执行结果见[验证记录](../verification/2026-11-09-device-control-v1.md)。正文在 XSS/MVC 读取前限制为 4096 字节，包含分块传输；设备写接口只接受 application/json，错误响应沿用本协议的 HTTP 状态和字符串 code。

## 范围

- 凭据使用 32 字节安全随机数；服务器只保存 SHA-256 校验材料，以唯一哈希定位设备，再核对 Device-Id。设备标识不作为秘密。准入档案与上游 `ai_device.user_id` 分开，避免把运营账号当作集市归属账号。
- 状态、请求、幂等结果和最新 ACK 保存在新增 `dc_*` MySQL 表。修改以设备行锁串行化，业务结果与幂等记录同事务提交。请求原期限为 600000 ms，同用途复用；错误重试、换用途和版本变化不能恢复旧码。操作结果目前不自动清理，保留超过契约要求的 24 小时；后续清理必须保留最低期限及请求引用完整性。
- 长轮询最多请求 20000 ms；每 200 ms 读取权威数据库，过期时推进 revision，跨实例无需依赖可能丢失的 pub/sub。每实例最多 64 个等待者，超出返回 429 和 Retry-After。等待期间不占数据库事务；凭据撤销在后续读中重新验证。该方式是首个切片的有限规模实现，未做 #19 容量验收。
- 此切片仅支持 `blocked` 状态：未绑定、单台已退出、需重新授权、设备禁用。数据库约束不允许写入 `ready`；没有签发语音令牌或授予 allowed 的路径。首次手机确认绑定与原账号设备页已接通，详见[绑定说明](customer-device-binding.md)及[单台退出说明](customer-device-session.md)。
- 首次 ACK 必须匹配当前 revision 和 permission_version，且 blocked 时必须 audio_stopped=true。曾成功的相同 ACK 从独立历史表返回原成功结果，即使当前版本已变化，也不覆盖最新 ACK、时间或当前许可；未曾成功的旧 ACK 返回 409 STATE_CHANGED。客户操作查询将 ACK 的许可版本与操作及设备当前版本一起比较；只有本次版本的停播证据可报告完成，旧版本历史 ACK 不会确认新操作。
- 每份状态/请求响应使用同一时刻计算 server_time_ms 和 expires_in_ms，保证剩余时间严格等于 expires_at_ms 减 server_time_ms；到期请求不返回零寿命二维码。协议整数按 JSON number 输出，不沿用管理接口的 Long 字符串序列化。

正式请求现在在同一事务建立 `customer_device_request` 关联，二维码可进入手机页和 OIDC 流程；不再依赖 #29 的测试预置请求。产品请求引用 `dc_request`，记录凭据校验材料快照，不创建上游管理员设备归属。每次打开、创建流程、回调及查询都会核对当前请求、准入、凭据和许可版本；撤销、禁用、替换或版本变化使旧码不可继续。过期按原 600 秒期限处理。

`/customer/v1/binding-flows` 的 JSON 创建与流程查询采用 `version/code/data`，共用 #29 身份服务、浏览器 Cookie 和 CSRF。手机表单使用同一 v1 入口；原无版本入口保留兼容，固定 OIDC 回调和通知地址不变。Nginx 仅增加精确的创建/查询路径。身份确认不修改设备归属或许可。

首次 bind confirm、open-account、单台 sign-out 与操作状态已交付控制实现；可信身份但业务授权不可用可绑定并保持 blocked。resume、已有设备授权 pending 的传播、allowed 租约/语音令牌、OTA 发现扩展、旧 OTA/语音入口封堵、语音输出闸门/固件停播，以及正式凭据写入/补发工具尚未交付。本切片不能作为完整产品准入或关闭 #13 的依据。

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

主线 #29 已使用 `202611091900` 建立身份请求与浏览器表；保留该已合并迁移原文和校验和。PR #26 未发布的 ACK 历史迁移改用 `202611092000`，`202611092010` 扩展身份请求来源，并使集成前尚无手机关联的旧控制请求失效，设备下次申请取得新码。历史 ACK 和设备归属不删除。

本次使用新的独立集成测试数据库，保留旧控制回归库及已有身份/部署数据。曾应用旧 PR #26 `202611091900` 的实验库存在编号冲突，不能直接按主线升级，也不能通过清空校验和绕过；需要单独核对并迁移 changelog 记录后才能复用。该实验分支从未部署到现有服务，本次也不部署。

回退应用时保留新增表、关联及私有密钥；Git revert 不恢复已失效二维码。回退到集成前控制代码时关闭 `device-control.enabled`，避免旧代码继续发出没有手机关联的新码。新增凭据和状态仍需纳入私有数据备份。
