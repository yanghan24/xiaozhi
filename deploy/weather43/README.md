# #43 隔离验收环境

在现有 Ubuntu 服务器复用资源，验证无天气 API key 的自建计算、结果缓存及容量。用户于 2026-11-10 将后续环境请求并发改为 **50 路**；350／1,000 台记录为历史实验。产品鉴权、设备城市偏好持久化和固件接入属于后续交付；这里的端口只能监听回环，不添加公网 Nginx 路由。

当前业务部署及恢复边界先读 [current.md](../../docs/deployment/current.md)。不得启动旧身份／语音测试栈，不停止业务服务，JetLinks 保持停止。命令的工作目录必须是新建隔离试验根目录；Compose 模板通过 `--project-directory` 将相对挂载解析到该根目录。

固定镜像与许可见[运行版本清单](../../docs/planning/issue-43-runtime-notices.md)。数据路线为 Ubuntu 自身 Mihomo → 匿名 S3；模型引擎通过专用桥接地址上的只读字节转发读取，结果缓存通过 HTTP 查询真实引擎。

部署前需要宿主可用内存至少 10GiB、磁盘至少 100GiB、AVX2、Docker，以及已验证的官方镜像。`private/acceptance.env` 只写固定镜像 ID、独立 Docker bridge 的 gateway/subnet 与预热数量，不含节点密钥。私有 Mihomo 配置位于 `/home/jt/.config/xiaozhi-weather43/config.yaml`，权限 600，父目录 700；不从仓库下载通用免费代理，不开放 LAN 代理或控制器。安装 [systemd 单元](xiaozhi-weather43-egress.service)前核对用户名、路径、经官方校验的二进制和私有配置。只启用本次单元；配置内容及订阅留在秘密存储。

```bash
# 例：在隔离根目录已有所需源码、城市样本和 private 目录后执行。
# 专用 bridge 的地址须从实际 Docker network inspect 得到，不能套用别人的网段。
sudo docker compose --env-file private/acceptance.env \
  -p xiaozhi-weather43-acceptance --project-directory "$PWD" \
  -f deploy/weather43/compose.acceptance.yaml config --quiet
sudo docker compose --env-file private/acceptance.env \
  -p xiaozhi-weather43-acceptance --project-directory "$PWD" \
  -f deploy/weather43/compose.acceptance.yaml up -d
curl --fail http://127.0.0.1:18086/health
```

模型目录对实际镜像 UID 可写；结果目录及 relay 日志对 65532 可写。各脚本和城市文件逐文件只读挂载。模型盘缓存 8GB，模型内存上限 8GiB／2 CPU；缓存网关 256MiB／0.75 CPU；relay 192MiB／0.5 CPU；宿主出站程序 256MiB／0.5 CPU。磁盘缓存与内存预算分开计算。日志有大小上限，结果库只接受固定城市目录；主缓存继续保留历史 350 地点、700 项结果以观察跨模型更新；50 路压测选择前 50 地点，独立冷夹具只建 50 地点、100 项结果。历史 1,000 地点夹具保留。两份 8GiB 模型引擎不重叠运行。当前模板明确将引擎请求截止设为 30 秒；早先主缓存默认 120 秒的结果保留原参数。

空缓存请求返回 HTTP 202，字段为 `null` 并标记 loading／unavailable；后台从真实模型填充。已有结果立即返回，天气与 AQ 各自带来源发布时间、起报时间、当前有效小时及状态。TTL 300 秒只安排复核，同一模型的成功复核不会改写 `fetched_at` 或起报时间。源有新批次、源失败、恢复后尚未检查或超出发布时间年龄时返回 stale。源已发布新批次后留 600 秒稳定期；元信息与引擎结果均校验后才落 SQLite。模型只支持滚动数据，报告会比较相同有效小时的值，不能仅凭滑动预报窗口声称模型升级。

`WEATHER43_PREWARM=0` 用于明确的完整冷缓存试验；上线候选可按已选城市设 35 或 350，由 `--prewarm` 在没有请求时主动填充，所有已访问／已恢复城市继续后台复核。持久库恢复无需等待源可达，但首次复核前明确为 stale。没有缓存且源离线时不会补零或默认城市。

压测只能在 Ubuntu 执行。先 `ulimit -n 8192`；`acceptance_load.py` 保存全部请求，包括未就绪、失败和超时，fresh/stale/loading 分开计数。当前 50 路计划：1／10／50 地点同时请求各三轮；热缓存和完全冷缓存分别以 `--devices 50 --cities 50 --scenario waves --period 5 --duration 1800` 运行 30 分钟，每 5 秒同步发起 50 请求，每组计划 18,000 次，信号量限制在途数不超过 50，并记录实际峰值。历史 `steady` 场景每台 300 秒一次；两类计划的平均请求率不同，不能直接比较吞吐。压测器与服务共机，延迟含本机调度，不作为终端公网延迟或语音并发容量。

[acceptance_50.py](../../scripts/weather43/acceptance_50.py) 采用已完成且成功的新账号 10 次基线，检查相同账号、授权代次、设备及语音源码，再执行 50 路矩阵和热负载中／负载后／冷负载期间问答。热和冷负载串行，完全冷阶段使用新模型及新结果目录，保留主目录。语音每阶段 10 次串行问答，首个失败停止该阶段，独立天气检查继续；不自动重试整个矩阵，不改动业务部署。冷填充可能早于 10 次问答完成，[coexist_report.py](../../scripts/weather43/coexist_report.py) 从实际时间戳重算重叠数、分位数和比值。

`build_locations.py --sample-size 1000` 生成另一个真实 GeoNames 样本，用于 1,000 不同坐标的全冷、热批次和 30 分钟探索负载。GeoNames 的样本包含城市及聚居点，并非去重的地级行政市；不同输入坐标也可能落在同一模型网格，验收报告须另外统计实际引擎返回网格。中文主名／别名覆盖、歧义及未命中均按真实库内容报告。

历史 [run_coexistence.sh](../../scripts/weather43/run_coexistence.sh) 使用旧账号和 350／1,000 计划；本轮旧等待控制器已退役，当前运行入口为上述 50 路脚本。正常产品路径使用设备归属账号的应用凭据，不将外部 Key 注入其他账号。服务器公网端口回环不通时使用 LAN 443，保持公开域名的 Host 与 TLS SNI、证书校验；这不是公网蜂窝或物理音箱测试。

语音对照的主延迟指标预先指定为“发送输入结束到首个可解码音频”，并报告 ASR、首个模型事件及完整播放结束耗时；完整结束耗时还受生成回答长度影响。每阶段 10 次、串行真实问答，逐条核验合成输入的预期识别文本、模型事件、回答文字及音频，不将单连接问答外推为 350／1,000 路语音并发。

旧账号的 1,000 地点冷填充语音因积分不足未运行；用户确认新账号正常绑定后，本轮改为 50 路／50 地点冷填充的真实问答。模型下载、缓存填充及业务容器资源同时采样；逐次标明与真实冷填充重叠区间，不将填充后问答混算为完整冷期样本。临时设备在全部问答尝试结束后清理。

[recovery_checks.py](../../scripts/weather43/recovery_checks.py) 只在持续负载与共存测试结束后执行。它使用专用 relay 的故障文件及新夹具目录，测试实际 TTL 到期、合并刷新、离线、慢源、持久重启、完整冷缓存及无请求预热；`finally` 清除故障并恢复本次引擎。40 秒延迟只作用于试验字节转发；不更改全机 DNS、防火墙或业务端点。模型与结果数据不删除，完整冷缓存使用新目录。出现低于 2GiB 可用内存时停止冷启动扩压并记录样本。

若实际语音账号已返回“积分不足”，可在已完成的 350 台共存天气负载结束后使用 `--weather-only` 继续独立故障／容量测试。该模式必须有真实额度拒绝证据，明确记录冷填充语音未运行，不能生成“语音验收通过”的结论；不充值、不更换为未经授权的模型 Key。

历史 [final_capacity.py](../../scripts/weather43/final_capacity.py) 的两次回归遇到并行 API 换版，版本一致性门槛未通过，原件保留。当前 50 路脚本同样检查业务容器镜像、启动时间、重启和 OOM 状态；业务部署再次变化时记录失败，不回退他人部署。HTTP 完成和真实语音对照分别判定。

[observe_updates.py](../../scripts/weather43/observe_updates.py) 保留首次真实基线，连续记录源与本地结果；必须收齐基线后的两次 AQ 起报批次和至少一次天气更新，再由 [summarize_updates.py](../../scripts/weather43/summarize_updates.py) 核对持久快照、原始响应哈希、相同有效小时的值与过期状态。启动观察程序不能代替已完成观察。

最终容量测试完成后，[network_soak.py](../../scripts/weather43/network_soak.py) 每分钟无重试读取两类真实源元数据及 4KiB 模型 Range，直到第二次 AQ 源／缓存更新被观察到。本轮绝对截止为 2026-11-10 23:30Z（北京时间 11 日 07:30）；到期未完成即记录未通过。记录每次成功／失败、TLS、DNS、服务启用状态及是否存在临时 SSH 端口；这是低速连续可达性观察，不作为生产 SLA 或持续带宽结论。

[metrics_report.py](../../scripts/weather43/metrics_report.py) 从原始逐请求记录重算计数和延迟分位数，并汇总 CPU、内存、盘缓存分配和网卡流量。`--interface` 取实际默认路由的出口；`--disk-device` 取根文件系统所属整盘，避免重复累加分区。磁盘 I/O 取 Linux 原始累计计数的差值，包含其他宿主业务，不是单独的 IOPS 性能基准。

[finish_observation.py](../../scripts/weather43/finish_observation.py) 在服务器等待矩阵完成，调用独立语音重算及 [cleanup_fixtures.py](../../scripts/weather43/cleanup_fixtures.py)，随后在上述截止前等待两次真实 AQ 和主缓存 700 项覆盖，执行更新审计与原始负载重算，形成阶段文件。它不执行 Git 或外发消息。默认只读的清理工具仅在显式 `--execute`、问答尝试结束且探针已退出时删除两个唯一测试设备及其请求／流程／会话，保留账号授权、共享浏览器会话和其他设备。

是否保留隔离天气服务由验收结论决定；暂停用本项目 Compose `stop`，恢复用 `up -d` 并复用私有结果／模型目录。不要用 `down -v` 或全机清理命令回退。
