# 联网烧录与设备交付 v1

对应 [#41](https://github.com/yanghan24/xiaozhi/issues/41)，依赖 PR #26 的设备准入和客户控制实现。
PR #48 只归档 `885918d` 的公网部署；它没有实现本文接口。新功能默认关闭。
本切片不关闭 #41：Ubuntu HTTP 回归、真实 P4/C6 流程及第二台设备隔离尚待验收。

管理员先授权工具；工具分别读取 P4 工厂 MAC 和 C6 Wi-Fi MAC，后台生成稳定设备标识和独立凭据。
登记后先禁止产品准入；工具写入并读回凭据，恢复产品镜像，由设备自身通过公网验证，最后启用准入。
交付完成仍为未绑定、语音 blocked；客户再通过设备正式二维码确认绑定。交付操作不会建立或替换客户归属。

```text
管理员登录 → 一小时工具授权
实物备份 → 产品镜像读取 P4/C6 → 幂等登记（禁止准入）
→ 维护镜像提交并读回 → 恢复产品镜像 → 设备公网验证
→ 工具确认交付（启用准入） → 设备正式二维码
```

## 身份与授权

- `hardware_id`：P4 eFuse 工厂 MAC，12 位小写十六进制。
- `network_hardware_id`：C6 STA MAC，同样为 12 位小写十六进制；不能与 P4 标识混用。
- `device_id`：后台生成的稳定标识，与上述两个 MAC 分别记录。
- 管理员授权复用**本产品 API 数据库**中已有的正常超级管理员登录。普通客户 OIDC 身份、旧管理 API 的另一套 token 都不能代替它。
- `/authorize` 将正常管理登录换成仅用于本交付协议的随机工具 token，期限一小时。每次使用还检查运营账号仍启用且仍为超级管理员。
- 工具授权过期后由同一管理员重新登录、授权，再使用原 journal 续作；不重建登记操作，不更换设备身份。
- 本切片未发布管理登录页面或绕过 SM2/验证码。管理员通过既有受保护的管理入口登录，工具从私有 token 文件读取；密码和 token 不作为命令行参数。

## HTTP 接口

所有路径以 `/xiaozhi/device-delivery/v1` 开头，要求 HTTPS，无 query string；JSON 请求上限 4096 字节。
成功响应为 `{"version":1,"code":"OK","data":{...}}`，数字保持 JSON 整数，所有响应 `Cache-Control: no-store`。
拒绝重复 JSON 字段、额外字段、重复身份请求头及不支持的版本。无效输入返回 400；公网 Nginx 也可能先以 413 拒绝超大请求。

| 方法和路径 | 认证 | 请求 | 响应 data |
| --- | --- | --- | --- |
| POST `/authorize` | 正常管理员 `Bearer` | `{"version":1}` | `tool_token`、`expires_at_ms` |
| POST `/operations` | 工具 `Bearer`，`Idempotency-Key` | `version`、`hardware_id`、`network_hardware_id`、`board_type` | 操作元数据及六字段 `identity` |
| GET `/operations/{operation_id}` | 同一管理员的工具 `Bearer` | 无 | 操作元数据；不含凭据 |
| POST `/device-check` | 设备独立 `Bearer` 和 `Device-Id` | `version`、`hardware_id`、`network_hardware_id` | `device_id`、`status`、`verified:true`、`server_time_ms` |
| POST `/operations/{operation_id}/complete` | 同一管理员的工具 `Bearer` | `version`、`write_operation_id` | 操作元数据 |

`operation_id` / `Idempotency-Key` 及 `write_operation_id` 均为 32 位小写十六进制。
工具在第一次请求和 USB 写入**之前**持久化这两个 ID，响应丢失时继续使用相同值。
操作元数据包括 `operation_id`、`device_id`、两个硬件标识、`board_type`、`status`、
`device_verified_at_ms` 和 `completed_at_ms`；时间未发生时为 null。状态仅有 `pending` 和 `delivered`。

登记响应的私有 identity 精确为：

```json
{
  "version": 1,
  "hardware_id": "020000000001",
  "device_id": "device_test_example",
  "credential": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
  "ota_url": "https://speaker.example:18443/xiaozhi/ota/",
  "control_origin": ""
}
```

以上为测试值。`ota_url` 来自服务端受信公网 origin；固件拒绝工具或响应临时更换 origin。
返回 `pending` 的设备检查不会授予产品准入；固件继续保持关闭，等待工具完成。
USB 回执由获授权的现场工具核对，设备检查由独立设备凭据认证；它们不构成硬件远程证明。

## 事务与恢复

创建操作和禁止准入的 `dc_device` 在同一事务写入。两个硬件字段和稳定设备 ID 分别唯一。
同一管理员、操作号和硬件参数的重试返回同一 identity；不同操作不能为相同硬件创建新档案。
并发重复插入发生冲突时，临时设备行一并回滚。其他管理员无法读取或续作既有操作。

完成操作锁定设备和交付记录，要求匹配当前凭据、设备已验证、USB 写入操作号有效、设备仍未绑定且仍为初始禁止准入状态。
首次完成原子启用准入并增加 revision / permission_version；同一回执重试不会重复增加。
之后禁用设备、轮换凭据或改变待交付设备状态，会阻止旧操作重新启用。
正常交付不改变已有归属；已登记身份保留和授权补发属于 #42，工具不会以新身份自动修复冲突。

工具 token 只存 SHA-256。设备凭据同时保留 SHA-256 校验材料和 AES-256-GCM 密文，后者只用于获授权操作的原样续作。
密文 AAD 绑定操作号、设备 ID 和管理员 ID，不能挪到其他记录解密。常规 GET、日志和共享报告不含秘密。
保留交付操作及对应密钥，才能在中断后取回同一凭据；不能通过删除记录或替换加密密钥来“恢复”。

## Ubuntu 配套部署

先按 [Ubuntu 手册](../deployment/ubuntu-isolated-test.md)核对现有服务。只在 Ubuntu 构建、运行服务及 Java 回归，
部署连接使用现场指定的**公网通道**；现有 HTTPS 设备端口不等于 SSH 或管理 API。
保留账号、数据库、OIDC 密钥及既有语音服务状态。先备份必要数据和密钥，不备份对话正文或录音。

1. 部署含本文实现的产品 API，Liquibase 只新增 `dc_delivery_tool` 和 `dc_delivery_operation`。
2. 在私有目录生成专用 32 字节随机密钥，以 base64 保存为 0600 文件。文件不得是符号链接；不存在时才创建，后续重启复用同一文件。
3. 将文件只读挂载进 API，设置 `DEVICE_DELIVERY_ENABLED=true`、`DEVICE_DELIVERY_ENCRYPTION_KEY_FILE=/run/secrets/device-delivery-key`；
   保持 `DEVICE_CONTROL_ENABLED=true` 以及正确的 `DEVICE_CONTROL_PUBLIC_ORIGIN=https://bridge.hncig.cn:18443`。
4. 在已有入口中合入 `deploy/device-entry/nginx-tls.conf` 的精确 delivery 路由，保留实际容器 upstream、证书、既有客户路由和可信代理地址。
   不用模板整体覆盖当前私有入口配置。`nginx -t` 后重载；新 API 和代理共同就绪后才签发实物凭据。
5. 核对正常管理员能换取工具授权、普通/匿名请求被拒；`state` 无凭据仍为 401，管理页和内部 API 仍为 404。

在**独立测试数据库**运行完整套件和守卫，不能对现场身份库运行测试清理：

```bash
docker compose -p xiaozhi-baseline-tests -f compose.tests.yaml build api-tests
docker compose -p xiaozhi-baseline-tests -f compose.tests.yaml up --abort-on-container-exit --exit-code-from api-tests
```

新增 `DeviceDeliveryHttpTest` 使用真实 Tomcat、Shiro、MySQL、Redis，覆盖授权、过期/撤权、两设备、跨凭据拒绝、
幂等/并发/孤立行、完成回执、禁止重启用/改归属、状态围栏、JSON 边界及密文 AAD。
补充媒体类型回归后共 12 个测试方法；缺少/错误 Content-Type 在 MVC 匹配控制器前按协议返回 400，
避免落入旧通用异常处理器的 HTTP 200 / code 500。前置过滤器仅作用于交付接口。
套件产出 `target/device-delivery-compatibility/{pending,delivered}.json`，可交给固件解析器测试。
目前这些新增 Java 测试尚未在 Ubuntu 执行，不能沿用 PR #26 的 321 项通过结果。

## 当前公网核对

PR #48 的[部署记录](https://github.com/yanghan24/xiaozhi/blob/aa730ccea7403112f4d0ed35daed3fadb35fd776/docs/verification/2026-11-10-pr26-server-deployment.md)
确认 PR #26 `885918d` 已部署。2026-11-10 初次本机公网探测：无凭据设备状态 401，新 delivery authorize 路径 404。
后续用户报告 PR #52 `d06f459` API、路由和迁移已部署、13 项公网检查通过，备份保留。
本机复查正常 JSON 请求已返回 v1 鉴权错误；缺少/错误 Content-Type 的部分 POST 仍返回旧 HTTP 200 / code 500，
修复及新增媒体类型回归须在 Ubuntu 验证后更新。产品库管理员配置由服务器执行者负责，工具授权验收尚未完成。
以下无凭据脚本可在 Ubuntu 从公网入口复现并验收修复；它不会注册设备或签发凭据：

```bash
python3 scripts/check-delivery-rejections.py --origin https://bridge.hncig.cn:18443
```

共 20 个正常/异常媒体类型与鉴权响应检查，要求正确 HTTP 状态、v1 错误码和 `Cache-Control: no-store`。
其余无需管理员的公网预检使用 `python3 scripts/check-public-device-preflight.py`，24 项已通过；
包括公开发现响应、旧数字激活关闭、鉴权及入口隔离、WSS 关闭和请求限制，见[结果与固件产物核对](../verification/2026-11-10-online-delivery-preflight.md)。
该结果不代表待部署的 Content-Type 修复已通过公网复测。
未签发本台新凭据，未刷写候选，也未执行手机/扬声器验收；PR #26 继续等待真机验收后再合并。

暂停新交付时先关闭 delivery 功能或撤回对应路由，保留新增记录和密钥。已经 delivered 的在线固件启动时也需要 device-check，
因此长期撤回该接口会让这些设备保持关闭；恢复时复用同一数据及密钥。回退应用不会撤销已经签发的凭据或已发生的客户绑定。
