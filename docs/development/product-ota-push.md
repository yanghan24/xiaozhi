# 后台在线 OTA

管理后台的「产品 OTA」在已校验固件目录下显示产品设备分页列表。可选择单台、跨页多台，或由服务器枚举全部当前在线兼容设备；选择一个已导入固件，检查升级工具与真实系统信息，查看排除原因后明确确认立即推送。升级可能打断语音并重启，没有等待空闲或离线补发。

## 配置

管理 API 使用以下 Spring 配置（也可通过对应大写下划线环境变量提供）：

| 配置 | 含义 |
| --- | --- |
| `product.ota.bridge-url` | 私有语音地址，完整路径 `/internal/product-ota/v1/control`，例如 `http://voice:8097/internal/product-ota/v1/control` |
| `product.ota.bridge-key` | 独立的至少 32 字节维护服务密钥，与语音 `PRODUCT_OTA_BRIDGE_KEY` 相同 |
| `product.ota.download-origin` | 无尾斜杠的固定 HTTPS origin，与语音 `PRODUCT_OTA_BRIDGE_DOWNLOAD_ORIGIN` 一致 |
| `product.ota.download-key` | 独立的至少 32 字节 HMAC 下载密钥，不是设备或语音服务凭据 |

语音配置与持久目录要求见 [私有桥接](product-ota-voice-bridge.md)。桥接只能在唯一语音进程的私有 Docker 网络监听，不得映射主机端口或加入公网代理。管理 API 也不得通过公共入口直接开放管理路径。密钥在服务重启后保持，桥接日志目录和 MySQL 必须持久化；更换下载密钥会立即使旧链接失效，但不会触发重发。

TLS 反向代理使用 `server.forward-headers-strategy=native`，并将 `server.tomcat.remoteip.internal-proxies` 限定到实际受信任代理地址。非默认 Docker 子网可能不在 Tomcat 默认范围内；此时代理后的 HTTPS 下载会安全拒绝为 `HTTPS_REQUIRED`，应修正部署信任范围，不能关闭 HTTPS 检查或信任任意来源。

TLS Nginx 模板只新增精确格式的 `/xiaozhi/ota-artifacts/{operation}/{sha256}.bin` GET 下载路径，禁用访问日志与包含查询参数的错误日志。Java 再检查 HTTPS、唯一签名参数、10 分钟到期时间、持久操作、固定产物和实际字节 SHA256。不会重定向到 Release/latest，也不会返回永久下载地址。非 TLS 测试入口没有这个路由。本次实现不表示模板已部署。

## 管理接口

全部管理接口使用现有运营管理员 Bearer 身份，并再次检查有效超级管理员档案。客户、设备和烧录工具凭据不能使用。路径前缀 `/admin/product-ota`：

- `GET /devices?page=1&limit=50`：来自产品档案的分页设备与当前会话状态；历史 last_seen 不作为在线证据。
- `POST /preview`，仅 `device_id` / `firmware_id`：返回兼容性、排除原因和两分钟有效的持久预览。技术会话身份保留在后端。
- `POST /operations`，仅 `preview_id` / `request_key`：绑定当前管理员与预览，保存操作后至多尝试一次下发。同请求键或同预览的重复确认只返回原记录。
- `GET /operations`：持久操作记录，包含尚待核对的历史操作。
- `GET /operations/{operation_id}`：刷新记录，并可查询新授权会话的目标运行证据；该路径绝不下发升级。
- `POST /batch-preview`，仅 `mode` / `device_ids` / `firmware_id`：`selected` 使用所选 ID；`all_online` 要求空 ID 数组，由服务器覆盖全部产品档案分页。立即返回 `preparing` 回执和 `preview_id`，此时没有推送。
- `GET /batch-previews/{preview_id}`：轮询至 `ready` 或 `failed`；就绪结果包含每台兼容性/排除原因、可推送数及统一的预览到期时间（全部预检完成后两分钟）。首次状态不可用会使本次列表安全关闭；其他状态查询最多八路、每次两秒，兼容性检查最多四路。
- `POST /batches`，仅 `preview_id` / `request_key`：在有效窗口内一次性确认持久清单，立即返回 `started` 批次回执。确认后工作器排队不再消耗预览确认期限；每台发送前仍重检管理员、原会话/许可与兼容性。服务器最多四路处理逐台既有预留/重检/下发路径，不会把预览时排除、后来上线或新登记设备加入本批。
- `GET /batches` / `GET /batches/{batch_id}`：查询当前管理员确认的批次与逐台真实结果。进行中为 `started`，结束为 `finished`；完成的是本次下发处理，不是固件安装。页面最多轮询两分钟，此后可手动刷新；所有轮询只有 GET，不重新提交。

异步预检和确认避免把长时间工具调用留在管理代理的 30 秒请求窗口内。每次桥接调用具有完整响应期限（状态两秒，其他操作 45 秒，包括响应体读取）。预览清单固定设备与固件；确认只消费此清单，发送前还须通过原会话/许可和兼容性重检。

客户端不提交下载 URL、MCP 方法、会话 ID 或许可版本。兼容性从当前授权会话的 `self.get_system_info` 读取：P4 板型、芯片型号和修订范围、物理 Flash 容量、六个分区的类型/子类型/地址/大小、当前 OTA 槽、应用版本与 ELF。32MiB 物理容量可以承载目录声明的 16MiB 布局；应用槽必须匹配 0x3f0000。相同 ELF 不作重装，同版本不同 ELF 不混为同一固件。缺失信息不猜测。

### 精确板型与固件上报的系列名称

固件清单和目录仍使用精确板型 `waveshare-esp32-p4-wifi6-touch-lcd-4.3`。固件源码 `12f2ff1b3e224ff012bdce95799ebf19740dcd11` 的 `main/CMakeLists.txt` 让 4B、4.3、7B、3.4C、4C、7、8、10.1 多种屏幕共用 `esp32-p4-wifi6-touch-lcd`，所以该系列名称不能无条件视为 4.3 寸别名。

设备上报系列名称时，预检和真正下发前均独立读取该设备的交付登记：只有 `dc_delivery_operation.board_type` 精确为 4.3 寸、`status=delivered`，且 `device_verified_at_ms` / `completed_at_ms` 已记录，才允许继续芯片、revision 和分区检查。缺失、未完成、未知板型及其他屏幕登记均拒绝。设备直接上报精确 4.3 寸名称时保留原有能力，但不能覆盖已有登记中的不同板型。预览中的允许结果不能代替下发时重查登记。

此约束使用已经完成实机核验的交付信息，不修改设备身份、交付记录、原固件目录或 SHA 去重规则，也不要求重刷固件。目标镜像 `7cb0c36077d42d9b64f09727e176c83f6ca2f281abb04d988982dc82e30b8f8c` 的真实安装证据见 [#18 验收](../verification/2026-11-13-issue18-ota-preparation.md)；线上某台设备是否满足登记要求仍以当前数据库和设备会话为准。

## 结果与恢复边界

操作先在 MySQL 保存固定设备、固件 SHA/ELF、管理员、预览、会话快照、请求键及时间。唯一活动设备约束与语音端 FULL 同步日志共同阻止并发、双击、网络中断和重启导致重发。服务重启不启动后台调度器；重新上线也不会领取旧任务。

批次预检/执行仅由当前请求启动的内存工作器执行，不是离线任务队列。当前部署要求单活动管理 API 和单语音实例；扩容前需要独立设计执行租约与会话路由，不能直接运行多个批次执行器。管理 API 启动时将遗留 `preparing` 标为失败、遗留 `started` 标为 `interrupted`，绝不恢复下发。没有单台预留的目标显示 `not_sent / interrupted_before_send`；已持久预留而无最终结果的操作变为 `result_unknown / process_interrupted` 并保持设备锁。批次读取直接关联原单台操作，因此不会因批次最终汇总丢失而丢失单台结果。

`not_sent` 是已知没有调用升级发送的结果；`dispatch_reserved` 是已持久保存、等待核对的窗口；`accepted_unconfirmed` 仅代表工具接受，`result_requires_verification` 表示设备回复需核对；`result_unknown` 保留发送或回执不确定性。这些均不能称为安装完成。

语音端已持久预留后，即使最后一次会话/许可保护在真正发帧前拒绝，当前实现也保守保留 `result_unknown`。这类操作没有自动重试；后续批量入口也不能清除该锁。没有新会话目标运行证据时，仍需人工核对或后续明确的恢复策略。

当前固件没有操作级持久安装回执，也没有新增固件侧 SHA256 比对。保障来自目录校验的固定字节、受限 HTTPS 下载与现有镜像合法性检查。新授权会话读取到准确目标版本和 ELF 才新增 `observation_status=target_running` 和观察时间，并释放后续发布锁；原传输结果保持。该证据表示目标产物运行，不宣称收到固件操作 ACK。没有足够证据时保持待核对，不提供自动重试或绕过权限的恢复按钮。

数据库变更为新增预览/操作/批次表，无设备身份、归属或许可更新。回退服务前先停止新发布并保留数据库表、原密钥和语音日志；不能删除日志来“恢复”待核对设备。实机下载、写入、重启及身份保持由 #99 验收。

### 空库迁移顺序

Ubuntu 空库回归发现五个 OTA changeSet 曾倒序插在 master changelog 顶部：`202611131600` 的 `ALTER TABLE` 先于 `202611131200` 建表，且 OTA 建表先于依赖的 `dc_device` / `sys_user`，导致应用上下文在 Liquibase 初始化阶段失败。修复将这五个 changeSet 移到现有 `202611131100` 之后，依次执行 `202611131200`、`202611131300`、`202611131310`、`202611131500`、`202611131600`。其他历史顺序不调整，changeSet 的 ID、author、SQL 路径与内容不变，不清除已有校验和或重新执行已记录迁移。空库及既有库的实际启动结果由后续 Ubuntu 回归核验。
