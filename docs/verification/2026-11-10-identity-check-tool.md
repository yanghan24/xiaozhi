# #36 身份验收只读检查工具：Ubuntu 现场验证

日期：2026-11-10，22:07–22:57（香港时间，UTC+08:00）。范围：在 `jt@192.168.168.114` 对现有环境做有界只读检查，并与旧脚本交叉核对。没有发起登录或通知，没有修改数据、配置或服务，也没有重启。

## 版本与环境

| 项目 | 值 |
| --- | --- |
| 工具提交 | `e75490fc67ce6671dbaa3aa4152b77e62be2f25a`（在 `50363e3` 上增加现有轮次记录读取） |
| 上传包 | `git -c core.autocrlf=false archive` 打包 `scripts/customer-identity`，tar.gz SHA-256 `1aa43006f67d01d5703380c178302963267b511a439c0b6c9b7fd6bae8d5de93`，两端一致 |
| 解压文件 | 41 个；按 `LC_ALL=C` 排序后，各文件 sha256 清单的汇总哈希为 `5d27ea6a995bcde84d3562ba82194cd38a597a72e08c5769da68a4fd0a9904b0`，两端一致 |
| 服务器目录 | `/home/jt/xiaozhi-identity-check/e75490f/`（700），先检查不存在再创建，不覆盖旧文件 |
| 访问方式 | 堡垒机网页终端，以 `jt` 登录；需要 root 的步骤由负责人同意后用 `sudo` 执行，未把密码写进命令参数或文件 |
| 系统 | Ubuntu 20.04，Python 3.8.10，MySQL 8.0.41 |

服务器离线测试（`e75490f`）：`python3 -B -m unittest discover -s tests -t . -v`，共 261 条，261 通过，0 跳过，0 失败。Windows 上跳过的 5 项 POSIX 权限检查（T29）在这里实际运行并通过。本机 CPython 3.12.10 和 3.8.20 各 261 条，256 通过，5 跳过。审阅修复后的 `1794370` 为 269 条，见下文复跑一节。

## 现场核实的环境事实

| 项目 | 结果 | 和 Issue 写法的差别 |
| --- | --- | --- |
| 身份服务 | `xiaozhi-pr26-api`（运行中），JDBC 指向 `mysql:3306/xiaozhi_esp32_server`，暴露 8002/tcp，在 `issue29-test` 网络上的别名为 `product-api` | Issue 写的 `issue11-identity` 已经停止（exited），当前身份服务是 PR #26 产品 API |
| 数据库 | `issue29-db`，`issue29-test` 网络别名 `mysql`，库名 `xiaozhi_esp32_server`，应用账号非 root | 一致 |
| 入口 | `xiaozhi-ubuntu-test-public-entry-public-entry-1`，只读挂载 `nginx-tls.conf` 为 `/etc/nginx/conf.d/default.conf`；`/xiaozhi/customer/oidc/backchannel-logout` 只有一个精确 location，`proxy_pass http://product-api:8002` | 文件中已经没有 `BEGIN/END ISSUE11 REAL IDENTITY` 标记，该 location 也不再配置 `access_log` |
| 通知日志 | 容器内 `/var/log/nginx/issue11-notify.log` 仍存在：11 行，ISO 秒级时间，最早 `2026-11-09T14:13:20Z`，最后 `2026-11-10T01:32:01Z`；共 POST 204 三次、POST 400 四次、GET 403 四次 | 文件是联调期间留下的，当前配置已不再写入 |
| 私有记录 | 目录属主 root，权限 700，共 25 项。轮次 JSON 为 0600，字段为 `url`、`handle`、`created_at`、`expires_at`、`baseline_receipts`、`baseline_log_lines`；句柄格式均合法 | 已在 `e75490f` 中适配，只取 `handle` |
| 表结构 | 六张客户身份表的字段与 changelog 一致；当时 `customer_logout_receipt` 共 35 条，本 issuer 3 条，其他 issuer 32 条 | — |

记录以排序后的序号标识（R13–R19），文件名不进入公开记录。按 `created_at` 对应关系：R14 为首账号退出轮（旧脚本 `logout-retest2-*` 引用），R15 为第二账号退出轮，R17/R18 为双账号 A/B（旧脚本 `two-account-*` 引用），R19 为闲置轮（旧脚本 `idle-retest-status` 引用）。

## 四场景结果

以下命令均以 root 运行，环境文件中的身份容器为 `xiaozhi-pr26-api`。

```bash
python3 -I -B check-identity.py <场景> --env <私有目录>/env.json --record <记录> [窗口] --format summary --output <私有目录>/<名称>.json
```

| 场景 | 输入与窗口（UTC） | 结论（退出码） | 依据 |
| --- | --- | --- | --- |
| 登录 | R17 | 通过（0） | F1 `IDENTITY_VERIFIED`、授权 `ACTIVE` |
| 登录 | R13 | 通过（0） | 同上 |
| 登录 | R14、R15、R18 | 证据不足（3） | 目标已是 `SESSION_ENDED`（这几轮后来做了退出），不能再证明登录状态；R15 另有一个 `FLOW_EXPIRED` 的 F1，被逐个列出 |
| 登录 | R16 | 未满足（1） | F2 已确认身份，但授权为 `REFRESH_UNCERTAIN`；F1、F3 为 `FLOW_EXPIRED` |
| 退出通知 | R14，01:03:20–01:04:00 | 证据不足（3） | 窗口内匹配收据 1 条，接收于 01:03:30.245；窗口前没有匹配收据；会话 `SESSION_ENDED`；入口日志未配置（`LOG_NOT_CONFIGURED`） |
| 退出通知 | R15，01:13:00–01:14:00 | 证据不足（3） | 匹配收据 1 条，接收于 01:13:31.274，归属 F2（F1 为 `FLOW_EXPIRED`）；会话已结束；日志未配置 |
| 双账号 | A=R17，B=R18，01:31:30–01:32:30 | 证据不足（3） | A、B 身份不同；B 匹配收据 1 条，接收于 01:32:01.114，B 已结束；A 没有匹配收据，仍为 `IDENTITY_VERIFIED`，两个授权都是 `ACTIVE`；没有工具生成的前基线（`BASELINE_REQUIRED`），日志未配置 |
| 闲置 | R19 | 证据不足（3） | 最后活动 02:17:02.611，截止 02:47:02.611，数据库已超时；没有原浏览器响应（`BROWSER_EVIDENCE_MISSING`） |

时钟差全部为 0，没有结果被截断。三条退出通知的收据时间与 [身份验收汇总](2026-11-10-identity-acceptance-summary.md) 的历史记录一致，精确到毫秒。

双账号和闲置结论为“证据不足”，这是预期结果，见实施方案 6.4：历史轮次没有工具生成的前基线，也没有捕获浏览器响应。工具正确地报告了数据观察，没有宣称端到端通过。

两个退出通知场景差在入口日志这一项。当前入口配置已经不再写通知日志，工具因此判定日志证据“未配置”，没有改用联调期间留下的旧文件。只要恢复日志配置并重做一轮，就能得到完整结论。这属于 PR 审阅要确认的 D1：数据库证据完整但日志不可用时，结论应该是什么。

## 错误路径

| 情况 | 结果 |
| --- | --- |
| 记录文件不存在 | 执行错误 `INPUT_UNAVAILABLE`（4），不输出路径 |
| 输入是设备请求记录（多了 `device_id`） | `RECORD_FORMAT_UNSUPPORTED`（4） |
| 环境指向已停止的 `issue11-identity` | `CONTAINER_NOT_RUNNING`（4） |
| 双账号 A、B 用同一份记录 | `RECORDS_NOT_DISTINCT`（4） |

## 与旧脚本交叉核对

五个旧脚本已经逐个阅读：都只用 `START TRANSACTION READ ONLY` 执行 SELECT，以及 `docker exec ... cat` 读取日志，没有写操作。旧脚本用的是数据库 root 账号，新工具用应用账号。在同一分钟内（14:51–14:52Z）运行，结果对照如下：

| 对照项 | 旧脚本 | 新工具 |
| --- | --- | --- |
| 首账号退出：状态 / 授权 / 匹配收据 | `SESSION_ENDED` / `ACTIVE` / 1 条，01:03:30.245 | 相同 |
| 第二账号匹配收据 | （`logout-retest2-match` 只覆盖首账号） | 1 条，01:13:31.274 |
| 本 issuer 收据总数 | 3 条，最新 01:32:01.114 | 3 条；另报告其他 issuer 32 条 |
| 双账号 A / B | A `IDENTITY_VERIFIED`，B `SESSION_ENDED`，账号不同，两授权 `ACTIVE` | 相同；B 匹配收据 1 条，01:32:01.114 |
| 闲置 | 最后活动 10:17:02.611（香港），截止 10:47:02.611，已闲置约 754.9 分钟 | 02:17:02.611Z / 02:47:02.611Z，数据库已超时 |
| 通知日志 | 读取旧文件，列出 01:03:30 和 01:13:31、01:32:01 的 POST 204，以及 01:03:45 的 POST 400 | 先检查配置；配置中已没有这个 `access_log`，判为未配置，不读取旧文件 |

除日志以外，数据库观察结果完全一致。日志的差别来自新工具多做的配置核对：旧脚本不检查日志当前是否还在写入。

### 复用对应表

审阅要求列出：每个旧脚本对应新工具的哪部分、复用了什么、行为差在哪、证据在哪。新工具没有照搬旧脚本里写死的文件名、句柄和 root 账号，复用的是查询语义和既有数据。

| 旧脚本及作用 | 新工具对应 | 复用或适配方式 | 行为差异及原因 | 验证证据 |
| --- | --- | --- | --- | --- |
| `logout-retest2-status`：一轮的 flow 状态、sid 是否存在、创建/确认时间、授权状态；本 issuer 收据总数和最新时间；读取通知日志 | `login --record` 和 `logout --record --window-*`；`sql.py` 的 flow 行和 `receipts` 行；`source.py` 读取日志 | 复用同样的表关联（`customer_identity_flow` 左连 `customer_authorization`，按 `subject_key`）和只读事务；记录文件直接读旧格式，只取 `handle` | 一个请求有多个 flow 时逐个列出，不取第一行；收据分成历史总数、窗口新增、其他 issuer 三个数；读日志前先核对 `access_log` 是否仍配置，未配置时不读旧文件；用应用账号而不是 root | 首账号状态和收据时间一致；收据总数一致；日志差异见上表 |
| `logout-retest2-match`：按 `issuer + sid` 和 `received_at >= created_at` 匹配收据 | `logout` 的 `matched_in_window` / `matched_before_window` | 复用 sid 匹配语义 | 新工具与服务端代码一致：有 sid 按 sid，无 sid 才按 issuer+sub，并加上 `created_at_ms <= LEAST(issued_at_ms+999, received_at_ms)` 条件；窗口前已有匹配收据时判证据不足 | 01:03:30.245 一致；T05–T09 离线测试 |
| `two-account-status`：A/B 两轮状态、两者身份是否不同（只输出计数）、收据总数、日志 | `two-account --record-a --record-b --window-*` | 复用在 SQL 内比较 `subject_key`、只返回布尔值的做法 | 判定隔离通过还需要前基线：A/B 同时 `IDENTITY_VERIFIED / ACTIVE`，且浏览器都在 30 分钟内。旧脚本只看当前状态 | A/B 状态与“账号不同”一致；缺基线判证据不足（预期） |
| `two-account-b-match`：B 的匹配收据 | `two-account` 中 B 的 `receipt_matched_b`；A 的 `receipt_absent_a` | 复用 sid 匹配语义 | 另外检查 A 没有收到自己的通知、A 仍保持会话 | 01:32:01.114 一致 |
| `idle-retest-status`：最后活动、30 分钟截止点、已闲置分钟数、授权 | `idle --record` | 复用 `customer_browser.last_activity_ms + 1800000` 的截止计算 | 截止时刻当毫秒即判超时，与服务端 `<=` 一致；没有捕获的原浏览器响应时只报告数据观察，判证据不足 | 最后活动和截止点一致 |

既有真实验收结论（登录、两次退出通知、双账号隔离、闲置）仍以 [身份验收汇总](2026-11-10-identity-acceptance-summary.md) 和原报告为准，本次没有推翻，也没有要求重新登录、退出或等待 30 分钟。历史上没有捕获的浏览器响应和工具基线不能倒造，所以新工具对这两项输出“证据不足”。

## 审阅意见的修复（2026-11-11）

PR #51 审阅提出 4 项 P2 判定问题。每项先补一条能复现原问题的回归测试，确认修复前失败，再修复；同时保留合法输入能通过的正向用例。

| 问题 | 修复 | 回归测试 |
| --- | --- | --- |
| 双账号基线不检查浏览器有效期：活动时间缺失或刚好到期仍判通过 | 采集基线和使用基线时都检查 `last_activity_ms + 1800000 > 基线观察时间`；缺失或到期判证据不足（`BASELINE_BROWSER_EXPIRED` / `ACTIVITY_UNKNOWN`） | `test_baseline_browser_must_not_be_idle_expired`、`test_baseline_phase_rejects_idle_expired_or_missing_browser`；正向：差 1 毫秒到期时可用 |
| marker 回退跳过通知路由校验：换成登录回调路由仍判 `proxy_target=ok` | 标记块里没有通知 location 时，标记必须位于精确的通知 location 内部 | `test_marker_fallback_requires_the_enclosing_notify_location`；正向：`test_marker_nested_inside_notify_location_is_accepted` |
| 命名 upstream 先按默认 80 端口校验，合法配置被拒 | 先解析 upstream 的唯一成员，再校验成员端口；upstream 引用带端口时拒绝（nginx 本身也不接受） | `test_named_upstream_without_port_uses_member_port`、更新后的 `test_explicit_upstream_resolution` |
| 代理别名没有唯一性检查：另一个容器在入口网络上用同一别名仍判通过 | 解析出的主机名在入口容器所在网络上只能由身份容器使用，否则判 `PROXY_TARGET_AMBIGUOUS` | `test_duplicate_proxy_alias_on_entry_network_is_ambiguous`；正向：别名只出现在入口看不到的网络时仍通过 |

这些都是新工具对合成边界输入的处理问题，不代表真实账号或当前 nginx 配置出过这些故障，也不影响历史业务结论。当前现场配置是直连 `product-api:8002`、没有 marker，不会触发这几条路径。

### 修复后的版本和现场复跑（2026-11-11 09:05–09:14 香港时间）

| 项目 | 值 |
| --- | --- |
| 工具提交 | `1794370`（`bbcb4b3` 修双账号基线，`1794370` 修入口代理目标三项） |
| 上传包 | tar.gz SHA-256 `e12c783893247949c98de3b46e109e3a54a3802efc4a9b192af900ed2915e0a7`，两端一致 |
| 解压文件 | 41 个，汇总哈希 `c4b6f2153d0656c86ee75ac0b352b59a2547c18aedba8274f147bf8a594ef09c`，两端一致；目录 `~/xiaozhi-identity-check/1794370/` |
| 离线测试 | 服务器 Python 3.8.10：269 条，全部通过，0 跳过；本机 3.12.10 / 3.8.20 各 269 条，264 通过，5 跳过（POSIX 权限检查） |

用同一个检查脚本重跑上面 14 次运行，只换了工具路径。10 个场景结果逐项比较，包括结论、退出码、每个检查项及原因、flow 列表和收据时间：10 个和修复前完全相同，0 个有差异。4 个错误路径的错误码也相同。5 个旧脚本都正常退出。

| 项目 | 前（01:12:20.944Z） | 后（01:12:50.237Z） |
| --- | --- | --- |
| 退出收据：行数 / 最大接收时间 | 35 / 01:32:01.114（11-10） | 相同 |
| identity flow：行数 / 最大创建时间 | 491 / 00:56:59.481（11-11） | 相同 |
| 浏览器：行数 / 最大活动时间 | 508 / 00:57:04.353（11-11） | 相同 |
| 设备请求、授权行数 | 561、292 | 相同 |

和昨晚相比，flow、浏览器、授权各多了 1 行，发生在 00:57（11-11），早于这次复跑，属于正常使用。复跑期间没有变化。脱敏扫描：10 个句柄、26 个文件，句柄原文、十六进制、IPv4、类 JWT、`Bearer` 都是 0 次。

### 第二轮审阅意见（2026-11-11）

PR 转正式后，审阅又提出 2 项 P2，同样先补复现测试再修复：

| 问题 | 修复 | 回归测试 |
| --- | --- | --- |
| 前缀 location 被当作通知路由：nginx 中正则 location 可能抢先匹配，通知实际转到别的后端，工具仍判 `proxy_target=ok` | 只接受 `location = <通知路径>`；前缀和 `^~` 都判 `PROXY_TARGET_MISMATCH` | `test_notify_location_must_be_an_exact_match`（有无 marker 各覆盖前缀、前缀加正则、`^~`）；正向：`test_quoted_exact_notify_location_is_accepted` |
| 数据库别名冲突只查共享网络：身份容器另一个网络上也有 `mysql` 别名时，应用可能连到别的库，工具仍判 `db_target=ok` | 检查身份容器加入的所有网络；其他容器在任一网络上用同一别名，或容器名就是该别名，判 `DB_ALIAS_AMBIGUOUS` | `test_db_alias_on_another_identity_network_is_ambiguous`；正向：别名只在身份容器看不到的网络上时仍通过 |

两处在现场都不会触发：当前通知路由就是 `location =` 写法，`xiaozhi-pr26-api` 只加入了 `issue29-test` 一个网络（见上文“现场核实的环境事实”和侦察输出）。

修复版本 `ba1b998` 的服务器复跑（2026-11-11 11:53–12:05 香港时间）：

| 项目 | 值 |
| --- | --- |
| 上传包 | tar.gz SHA-256 `3cb5b49e5419bec0d26db005d9a7619e160abfa890ed98bb296edaedf789b1dd`，两端一致 |
| 解压文件 | 41 个，汇总哈希 `6fe09e6810b47ead24b11229a64828b9c9a5f8c8cad5a75c8dbc3da05f0c1645`，两端一致；目录 `~/xiaozhi-identity-check/ba1b998/` |
| 离线测试 | 服务器 Python 3.8.10：273 条，全部通过，0 跳过；本机 3.12.10 / 3.8.20 各 273 条，268 通过，5 跳过（POSIX 权限检查） |
| 场景复跑 | 10 个场景与 `1794370` 的结果逐项比较，0 个差异；4 个错误路径错误码相同；5 个旧脚本正常退出 |
| 写入检查 | 前（03:47:06.828Z）后（03:47:37.012Z）快照相同：收据 35、flow 493、浏览器 510、设备请求 562、授权 292，各最大时间不变 |
| 脱敏扫描 | 10 个句柄、26 个文件，命中 0 次 |

与上一次复跑相比，flow、浏览器、设备请求有少量新增，最新一条在 02:31:12Z，早于这次复跑，属于正常使用；复跑期间没有变化。

### 第三轮审阅补齐（2026-11-11）

在 PR 头提交 `d247f41` 上补齐身份容器自身的数据库名称冲突检查：不仅检查数据库共享网络，还检查身份容器所有网络别名及容器名。任一名称与数据库别名相同即返回 `DB_ALIAS_AMBIGUOUS`。

新增三项测试覆盖自身在共享网络/第二网络上的冲突、容器名冲突，以及第二网络无冲突的正向场景。先加入测试，在 Ubuntu Python 3.8.10 上运行 276 项，修复前出现 2 项失败；修复后同一完整套件 276 项全部通过，0 失败、0 跳过。

命令：`python3 -m unittest discover -s scripts/customer-identity/tests -t scripts/customer-identity -q`。通过 SSH 在 `jt@192.168.168.114` 独立临时目录执行：修复前 `/tmp/pr51-fix-Jwc86D`，修复后 `/tmp/pr51-fixed-vlxpzz`。本次仅运行合成离线测试，没有查询业务数据库、修改部署配置或执行真实登录/退出；不新增现场端到端验收结论。

## 没有产生业务写入

运行全部工具和旧脚本前后，各做一次只读快照：

| 项目 | 前（14:51:36Z） | 后（14:51:55Z） |
| --- | --- | --- |
| 退出收据：行数 / 最大接收时间 | 35 / 1794274321114 | 35 / 1794274321114 |
| identity flow：行数 / 最大创建时间 | 490 / 1794321504694 | 490 / 1794321504694 |
| 浏览器：行数 / 最大活动时间 | 507 / 1794321504694 | 507 / 1794321504694 |
| 设备请求、授权行数 | 561、291 | 561、291 |

另外两层保证：查询都在 `READ ONLY` 事务中执行，工具执行前还会静态检查 SQL 中有没有写操作。`customer_authorization.updated_at_ms` 会被定时刷新改变，所以没有拿它来比较。大约 30 分钟前侦察时 flow 有 488 行，运行前变成 490 行，这是其他正常使用造成的，发生在本次运行之前；运行期间没有变化。

## 脱敏检查

运行后，用服务器上全部 10 个轮次句柄逐字扫描结果目录中工具生成的 26 个文件，包括 JSON、摘要和环境文件：句柄原文 0 次，句柄十六进制 0 次，IPv4 0 次，类 JWT 串 0 次，`Bearer` 0 次。结果文件保存在服务器 `/home/jt/xiaozhi-identity-check/results-e75490f/`（700，属主 jt），没有提交到仓库。

## 验收标准对照

| 标准 | 证据 | 状态 |
| --- | --- | --- |
| 四种场景都通过参数选择，不改脚本、不写死轮次 | 上面 14 次运行只改参数；T31 静态检查 | 满足 |
| 关键语义有离线脱敏测试 | 两轮审阅修复后 273 条离线测试，本机两个 Python 版本和服务器 Python 3.8.10 都通过 | 满足 |
| 合成敏感值不泄露，输入、SQL、命令不能注入 | T24–T28；现场扫描 0 命中 | 满足 |
| Ubuntu 有界只读检查，与旧查询交叉核对，不产生业务写入 | 本记录四场景、交叉对照、前后快照 | 满足；日志证据因配置已移除而不完整，已如实报告 |
| 缺文件、容器不可用、schema 不符或有歧义时不假装成功 | 错误路径表；多 flow 逐个列出；已结束会话判证据不足 | 满足（schema 不符只做了离线测试，现场 schema 一致） |
| 运行文档、独立 PR、不影响业务 | [运行说明](../development/customer-identity-check.md)；草稿 PR #51；只新增文件 | 待 PR 更新和审阅 |

## 留给审阅人的事项

1. D1：数据库证据完整、入口日志未配置时，退出通知场景是否维持“证据不足”。
2. 是否恢复通知日志配置后再做一轮真实退出，拿到退出通知场景的完整“通过”证据。这需要重新登录和退出，超出本次只读范围。
3. 双账号和闲置的端到端结论，需要以后按运行说明先保存工具基线、再捕获原浏览器响应，本次不能补做。
