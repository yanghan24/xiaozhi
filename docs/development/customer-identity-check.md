# 身份验收只读检查工具（#36）

该工具供运维核对明确指定的身份验收轮次。它不属于产品登录或退出路径，不发起登录、通知、撤销或模型请求，不修改业务数据，不重启服务。历史脚本、私有记录、现有配置均保留。

> 当前为本机开发版本：离线测试使用合成数据及模拟容器；实际私有记录格式、Ubuntu 环境及旧查询对照尚未验证。不得把本文示例或离线通过写成服务器验收通过。

## 前置条件与执行边界

- Ubuntu 宿主机 Python 3.8 或更高版本，标准库，无需 pip 依赖；Docker 与 MySQL 8 客户端位于现有环境。
- 在服务器内运行和关联原始身份，普通输出只包含 A/B、F1/F2 等标签、布尔值、计数、状态和时间。原始 sid/sub、身份哈希、请求句柄、Cookie、Token、数据库密码和来源 IP 不输出、不提交 Git。
- 先按[当前部署入口](../deployment/current.md)及其链接证据核对实际环境；[Ubuntu 历史部署说明](../deployment/ubuntu-isolated-test.md)仅供追溯。最新部署记录表明旧身份服务已停止，不可直接使用 Issue 创建时的容器名作为当前目标。
- 本工具不配置 sudo。执行 sudo 前必须按 #36 要求读取既有私有 README，并通过获准的加密凭据 runner 执行。没有该能力时记录受阻，不用堡垒机密码替代服务器凭据，不改 docker 组。
- 浏览器堡垒机只提供访问途径，登录成功不代表获得 sudo 或文件上传权限。终端可以断开，服务器和服务不要停止。
- 工具正式采集只允许在 Linux 及获准 runner 的 root 执行上下文中运行。本机可运行帮助、离线测试和合成输出生成，不连接本机业务数据库。
- 在审查过的独立工具目录运行，私有记录放在另一个目录。建议 `python3 -I /独立工具目录/check-identity.py ...`，防止当前私有目录中的同名 Python 文件被导入。

## 文件与输入

参考 [`env.example.json`](../../scripts/customer-identity/env.example.json)。示例中的容器、库名、issuer 和路径都是演示值，须由运维显式核对后另存为私有环境文件。程序不自动寻找“最新轮次”，不扫描整个私有目录。

| 环境字段 | 用途 |
| --- | --- |
| `issuer` | 本轮可信 issuer，HTTPS，逐字匹配，不发起 Discovery 请求 |
| `record_root` | 允许读取测试记录的绝对路径根目录 |
| `db.container` / `db.database` / `db.network_alias` | 明确的数据库容器、库名和网络别名 |
| `identity_container` | 明确的身份服务容器 |
| `entry.container` | 明确的入口容器 |
| `entry.notify_log` | 容器内通知专用日志绝对路径 |
| `entry.config_file` | 宿主机上挂载至入口容器且实际生效的 Nginx 配置文件，由运维核对 |
| `entry.marker` | 可选的配置块名称，对应 `# BEGIN 名称` / `# END 名称`；不填时定位唯一的精确通知 location |

当前支持单一精确通知 location、可唯一解析的代理主机和容器声明的 TCP 端口。身份容器与入口须共享可唯一解析目标的网络，JDBC 使用默认 3306；MySQL 主版本仅支持 8。环境不符合这些约定会停止，而非猜测目标。仅证明文件被 bind mount 不能证明它被 Nginx include；`config_file` 是否为实际生效配置仍须第二阶段人工核实，未使用的挂载文件不属于已验证布局。

工具核对运行状态、共享数据库别名的唯一性、JDBC 指向的主机与库、代理目标、MySQL 版本及所需 schema。环境不匹配是执行错误，不是登录或通知失败。遇到不能明确解析的配置或多个候选时停止，不猜目标。

### 测试记录 v1（当前支持的明确格式）

```json
{
  "schema": "xiaozhi-identity-round-v1",
  "request_handle": "synthetic-request-handle-only"
}
```

上面只是合成示例。实际句柄只保存在服务器私有文件中，不通过终端参数或环境变量传递。文件必须是受保护的普通 JSON 文件、位于 `record_root` 内，不能为符号链接，最大 1 MiB。Linux 检查属主及组/其他用户权限（0600 或更严）。未知结构、重复 JSON 字段、非法句柄直接拒绝。

**现有服务器轮次记录（第二阶段已核实）**：Ubuntu 上既有的轮次 JSON 由旧验收流程写入，字段为 `url`、`handle`、`created_at`、`expires_at`，部分带 `baseline_receipts`、`baseline_log_lines`。工具接受只含这些字段且包含 `handle` 的文件，只取 `handle`，其余字段不参与判定。旧的 `baseline_*` 不是本工具的基线，不能代替 `--baseline`。含 `device_id` 等其他字段的文件（例如设备请求记录）会被拒绝。这些文件属主为 root、权限 0600，因此要以 root 运行。工具不修改原文件，也不为了适配生成新的业务请求。

Windows 的权限位不等于 Linux 权限，Windows 离线测试跳过 POSIX 权限断言。真实采集必须在 Linux 核实。

## CLI

```bash
python3 -I /工具目录/check-identity.py <场景> --env /私有目录/env.json [参数]
```

| 场景 | 必需输入 | 可选输入 |
| --- | --- | --- |
| `login` | `--record` | `--flow F2`、`--baseline` |
| `logout` | `--record`、`--window-start`、`--window-end` | `--flow`、`--baseline` |
| `two-account` | `--record-a`、`--record-b`、`--window-start`、`--window-end` | `--flow-a`、`--flow-b`、`--baseline`；**缺前基线允许采集，但只能判证据不足** |
| `idle` | `--record` | `--flow`、`--baseline`、`--browser-evidence` |

其他参数：
- `--phase baseline|check`，默认为 `check`。采集前基线时指定 `baseline --save-state 路径`，不传时间窗口、旧基线或浏览器证据。
- `--save-state 路径`，保存私有观察状态；`--output 路径`，保存脱敏结果 JSON。文件必须不存在，0600 创建，不覆盖原文件。
- `--format json|summary|both`，默认 `both`：JSON 写 stdout，摘要写 stderr，方便重定向后仍可解析 JSON。
- `--timeout 1..60`，每个外部命令超时秒数，默认 20；采集还有总时限，输出有大小上限。

时间必须包含时区及秒。内部用整数毫秒，UTC 和香港时间都保留三位毫秒；不足毫秒截断。窗口为 `[start, end)`，建议按整秒选边界。窗口未结束不得提前宣称通知没有发生。

`F1`、`F2` 是同一个请求内按 `creation_order` 排序的序号，不是真实 flow ID。所有 flow 都会报告；多个候选而未指定目标时返回证据不足，不默认取第一行。

### 登录

```bash
python3 -I /工具目录/check-identity.py login --env /私有目录/env.json \
  --record /私有目录/round-a.json --format both
```

分别呈现存储状态、推导状态、sid 是否存在和当前独立授权状态。`AWAITING_IDENTITY` 或 `EXCHANGING` 到期限时推导为 `FLOW_EXPIRED`；历史 `IDENTITY_VERIFIED` 并不表示浏览器仍能访问。`REFRESHING` 是正在刷新，不能自动认定授权失败。身份确认不等于已完成设备绑定。

### 退出通知

```bash
python3 -I /工具目录/check-identity.py logout --env /私有目录/env.json \
  --record /私有目录/round-a.json --flow F2 \
  --window-start '2026-11-12T09:30:00.000+08:00' \
  --window-end '2026-11-12T09:35:00.000+08:00'
```

日期只用于演示，不是固定运行窗口。工具在数据库内按实际 issuer 关联目标：收据有 sid 时仅按 sid，没有 sid 才按 issuer+sub；还核对创建、签发和接收时间范围。

分别报告本 issuer 的历史总量、窗口增量、目标匹配量、接收时间、入口 204/400 数量、会话和独立授权状态。204 不是通过验签的充分证明，历史总量不是本轮增量。不同 jti 的多条收据可以匹配同一目标，不擅自解释为协议错误；同 jti 的重复到达已被服务端持久化去重，本工具不重发通知验证幂等。

通知前已有目标匹配收据、日志与数据库来源不一致或基线不连续时，返回证据不足。授权保持 `ACTIVE` 不代表退出未生效；会话退出与共享授权撤销分开。

### 双账号

先在两者同时有效时采集**同一快照**：

```bash
python3 -I /工具目录/check-identity.py two-account --env /私有目录/env.json \
  --record-a /私有目录/round-a.json --record-b /私有目录/round-b.json \
  --phase baseline --save-state /私有目录/two-account-before.json
```

然后由负责业务验收的人在本工具之外完成原定 B 退出操作。工具不发起退出。使用明确时间窗口读取后状态：

```bash
python3 -I /工具目录/check-identity.py two-account --env /私有目录/env.json \
  --record-a /私有目录/round-a.json --record-b /私有目录/round-b.json \
  --baseline /私有目录/two-account-before.json \
  --window-start '2026-11-12T09:30:00+08:00' --window-end '2026-11-12T09:35:00+08:00'
```

A/B 必须身份不同，前基线中必须同时为 `IDENTITY_VERIFIED / ACTIVE`，且绑定同一轮次、目标和环境。只输出是否不同，不输出身份哈希。缺基线或 A 本已结束时是证据不足，不能拿“现在 A 还是结束状态”证明隔离。还要求本 issuer 收据计数与基线后查询连续；A 当前独立授权若已变为非 ACTIVE，先判为证据不足，不能在无法解释授权变化时宣称隔离通过，也不直接归因于 B 退出。

### 闲置

```bash
python3 -I /工具目录/check-identity.py idle --env /私有目录/env.json \
  --record /私有目录/round-a.json --flow F1
```

输出服务器观察时间、最后活动时间、`最后活动 + 1_800_000 ms` 的截止点、授权状态。到截止点的那一毫秒即为超时。数据库时钟应落在查询前后宿主机时间区间内，容差 1 秒。

数据库超时不等于原浏览器已经访问被拒。没有浏览器证据时只报告数据观察，总结论为证据不足。浏览器证据由获准的验收流程在服务器私有文件中整理，不保存 Cookie 或完整 URL：

```json
{
  "schema": "xiaozhi-browser-evidence-v1",
  "kind": "captured",
  "observed_at": "2026-11-12T10:48:45.042+08:00",
  "http_status": 401,
  "error": "IDENTITY_REQUIRED",
  "original_browser": true,
  "record_fingerprint": "<对应记录文件的 SHA-256，仅放私有文件>",
  "flow": "F1",
  "last_activity_ms": 1794453422611
}
```

通过 `--browser-evidence` 读取。必须确认原浏览器、记录/flow/活动时间一致、响应不早于截止点且不晚于本次观察。`kind: user_reported` 只能作为辅助证据。证据文件的可信来源仍由运维负责；手工把字段改为 `captured` 不能替代真实响应，工具不宣称为外部证据提供真实性认证。

## 基线与输出文件

- 基线包含记录文件摘要、环境指纹、flow 顺序和状态、授权状态、观察时间、收据计数和日志状态；这些指纹只用于服务器内跨次关联，不进入公开输出或 Git。
- 基线必须在窗口开始之前采集，与当前同环境、同记录、同目标。没有历史基线时不从数据库现状倒造。
- 文件创建使用排他方式，已有文件拒绝。以 runner 运行时将新文件属主交还发起 sudo 的用户（若提供合法的 `SUDO_UID/SUDO_GID`）。
- 输出字段采用白名单，底层 stderr、SQL、完整 inspect、私有路径和异常原文不直接输出。敏感串检查只是最后防线，不能代替只查询受限字段和白名单序列化。
- stdout 是单个 JSON 文档，摘要不会夹在其中。输出 schema 为 `xiaozhi-identity-check-v1`。

| 退出码 | 结果 | 含义 |
| --- | --- | --- |
| 0 | `pass` | 本次可验证的场景判据通过，不扩展为设备或整体验收通过 |
| 1 | `unmet` | 证据足够，但场景条件未满足 |
| 2 | 参数错误 | 参数不符合要求，使用 `--help` 查看，不回显敏感实参 |
| 3 | `insufficient` | 缺基线、浏览器证据、日志或唯一目标等；也用于基线采集，`baseline_ready: true` 表示采集就绪而非业务通过 |
| 4 | `error` | 文件、容器、schema、环境、超时、输出安全检查等执行错误 |

CI 或 shell 使用基线采集时，按 JSON 的 `baseline_ready` 检查，不把其退出码 3 当作采集失败；缺条件时不写出可用基线。

## 查询和日志边界

- SQL 通过 stdin 传入数据库客户端，值编码为十六进制字面量。只生成固定的只读查询模板，执行前再检查写操作关键字。
- 事务为只读、一致性快照，不使用 `FOR UPDATE`、共享锁、DDL 或整库导出。连接、查询、进程和输出均有限制；查询时限 5 秒，元数据锁等待时限 5 秒，整轮外部命令总时限 60 秒。终止 `docker exec` 客户端不能保证远端查询瞬间退出，数据库端时限仍须保留。
- 单请求最多 50 个 flow、单窗口最多 500 个收据时间明细；超出返回证据不足，不把被截断的数据当作完整结果。
- 数据库账号来自容器已有应用账号变量，不打印变量，不从客户端参数传密码，不改账号权限。实际变量及可用性须服务器复核。
- 只读事务和受限模板是主要保证。前后行数、最大时间不变只是辅助观察，不能独自证明没有任何写入；其他用户及后台刷新产生的变化需分别解释。
- 通知日志读取上限 8 MiB。支持明确的 ISO 时间/带时区 Nginx 时间、来源字段、方法、状态、上游状态格式；未知格式返回证据不完整，不自动猜。
- 日志缺失、空日志、未知行、窗口开始未被覆盖、轮转/截断、读取期间变化、时间逆序或边界秒有歧义时，不能得出“没有通知”。不修改日志配置，不读取请求体，不诊断 400 根因。
- 当前完整性检查是保守观察，不能证明操作系统之外没有丢日志。inode/大小不能检测所有历史篡改；不要把日志计数当成与某个 jti 的一一关联证明。

## 离线测试与复查

仓库根目录运行：

```bash
python -m unittest discover -s scripts/customer-identity/tests -t scripts/customer-identity -v
```

用 Python 3.8 再执行同一命令。Windows 中 `python` 可能命中应用商店占位程序，应使用实际 Python 的完整路径。仅语法解析或 Windows 测试不能替代 Linux 权限测试。

测试使用合成数据覆盖其他 issuer、多 flow、重复/无收据、sid 优先、时间边界、缺基线、闲置证据边界、日志缺失/轮转、注入防护与正常/失败输出脱敏。结果记录实际条数、失败和跳过，不把主题数量当测试条数。

服务器复查顺序：
1. 确认 sudo 合规流程、审查旧脚本、私有格式和环境字段。
2. 经批准上传到新建的独立目录，核对文件哈希；不覆盖旧工具，不部署产品。
3. 服务器 Python 3.8 运行离线测试，补验 Linux 权限分支。
4. 对同一明确窗口运行新工具和经过审查的旧查询，比较受限字段。
5. 如缺历史基线或浏览器响应，验证“证据不足”输出，不要求伪造通过。
6. 在独立验收报告记录环境、工具版本、哈希、命令、结果、差异及受阻事项，再由管理员审核 PR。

常见错误：`RUN_ON_SERVER_WITH_APPROVED_RUNNER` 表示执行位置或权限不满足；`RECORD_FORMAT_UNSUPPORTED` 表示记录既不是 v1 格式，也不是上述现有轮次格式；`OUTPUT_FILE_EXISTS` / `OUTPUT_INPUT_COLLISION` 表示拒绝覆盖；环境、schema、超时及 MySQL 错误只显示受限代码，需按获准运维流程在服务器内查明，不为了排错开启原始凭据输出。
