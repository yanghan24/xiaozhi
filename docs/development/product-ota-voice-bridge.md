# 在线 OTA 私有语音桥接

此通道用于 #97 管理后台的在线操作，只复用已存在的产品会话和用户维护工具，不为离线设备排队。原 root Unix 维护通道与其固定设备、固定下载路径策略保持独立。

## 配置与边界

只有设置 `PRODUCT_OTA_BRIDGE_PORT` 才启动。必须同时配置：

- `PRODUCT_OTA_BRIDGE_KEY`：独立于语音授权服务密钥的至少 32 字节秘密。
- `PRODUCT_OTA_BRIDGE_DIRECTORY`：服务进程所有、0700 的持久目录，保存 SQLite 操作日志；不能使用临时容器层。
- `PRODUCT_OTA_BRIDGE_DOWNLOAD_ORIGIN`：固定 HTTPS origin，与管理 API 的签名固件下载 origin 一致。
- `PRODUCT_OTA_BRIDGE_HOST`：默认 `127.0.0.1`。容器间使用时只能绑定私有网络，禁止发布此端口到主机或公网代理。

该监听器在唯一产品语音进程的事件循环内访问会话注册表。当前配置要求单个语音实例；部署多实例前必须增加唯一会话路由，不能任意选一个实例进行推送。公网 WebSocket / OTA HTTP 监听器不注册此路由，公网 Nginx 不得代理内部路径。

`POST /internal/product-ota/v1/control` 只接受一个 `X-OTA-Service-Key` 请求头和至多 8192 字节 JSON；拒绝重复 JSON 字段、未知字段和任意 RPC 方法。每台设备串行，最多 8 台同时执行，单次操作限时 25 秒。没有访问日志、原始异常或升级 URL 日志。

## 契约

请求始终含 `action` 与产品 `device_id`。`session_id` / `permission_version` 只在受信任的后端之间流转，不返回浏览器。

| action | 额外字段 | 结果 |
| --- | --- | --- |
| status | 无 | offline / no_unique_session / permission_unavailable / mcp_initializing / current；current 含当前会话与许可版本 |
| inspect | session_id、permission_version | ready + system_info，或 tool_unavailable；校验当前会话与许可后查询用户工具及系统信息 |
| dispatch | session_id、permission_version、operation_id、artifact_sha256、target_elf_sha256、target_version、url | accepted_unconfirmed / result_requires_verification / result_unknown / not_sent + reason |
| operation | operation_id | 持久化结果或 not_found；绝不重发 |
| reconcile | operation_id、session_id、permission_version | 原传输结果；新会话读到目标版本与 ELF 时额外返回 observation_status=target_running、observed_at_ms |

`operation_id` 为 32 位小写十六进制，两个 SHA256 为 64 位小写十六进制。下载 URL 仅允许配置的 HTTPS origin，精确路径 `/xiaozhi/ota-artifacts/{operation_id}/{artifact_sha256}.bin` 和唯一 `expires` / `signature` 参数。首次下发时剩余有效期必须大于 30 秒且不超过 900 秒。签名由管理后台生成并在下载入口验证；桥接不接受浏览器提供的下载位置。已经记录的同一操作可以在链接过期后查询，不能再次触发下载。

`system_info` 只保留 flash_size、chip_model_name、chip_info 的 model/cores/revision/features、application 的 version/elf_sha256、board 的 type/name、ota 的 label 和 partition_table 的 label/type/subtype/address/size。MAC、UUID、SSID、IP 和任意用户工具返回内容不会透传。完整兼容性决策由管理后台完成；缺失字段不能推断兼容。

## 持久化和观察

SQLite FULL 同步事务在真实升级帧之前保存不可变设备/会话/许可/固件/目标版本身份及 `result_unknown`，唯一索引阻止同台设备的第二个未核验操作。进程退出、设备断线、超时或回执丢失不提供自动重试机会。再次提交相同操作只返回原记录；同一操作号更换身份被拒绝。最终发送继续使用 `MaintenanceControl.rpc` 与 `GuardedSocket` 的权限和同步唯一会话校验，用户工具不会加入模型工具集合。

`true` 回执仅表示 `accepted_unconfirmed`，其他回复是 `result_requires_verification`，均不表示安装成功。新授权会话必须与原会话不同，且读取到准确目标版本和 ELF，才记录 `target_running` 观察并释放设备的后续发布锁。原传输结果（包括 result_unknown）永久保留。观察不是设备持久化操作回执，不能证明同版本同 ELF 重装；管理后台应在首次预览拒绝相同实际固件。无观察时保持锁定，不新增自动重发或恢复按钮。

## 验证

2026-11-13 Ubuntu `192.168.168.114`，隔离目录 `/home/jt/ota97-voice`，以现有依赖镜像 `xiaozhi-product-voice:asr-switch` 启动无外部网络临时容器，挂载候选源文件：13 项桥接 HTTP/设备协议测试与 16 项原维护回归通过（29 项，无跳过）。包含真实本机 WebSocket 接收升级帧和未选设备隔离、鉴权、严格输入、会话/权限变化、未知结果、重建桥接实例后不重发，以及独立的目标运行观察。初始鉴权测试曾以缺失模块失败，最小实现通过后继续行为切片；测试 runner 首次因 CRLF 模式匹配产生 0 项，已纠正，0 项不计为验证。

没有操作现网服务、真实设备或公网代理。此隔离测试不证明公网反向代理配置、实际 TLS 下载或 P4 写入/重启；后者由 #99 验收。
