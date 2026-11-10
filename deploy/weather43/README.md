# #43 隔离验收环境

在现有 Ubuntu 服务器复用资源，验证无天气 API key 的自建计算、结果缓存及容量。产品鉴权、设备城市偏好持久化和固件接入属于后续交付；这里的端口只能监听回环，不添加公网 Nginx 路由。

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

模型目录对实际镜像 UID 可写；结果目录及 relay 日志对 65532 可写。各脚本和城市文件逐文件只读挂载。模型盘缓存 8GB，模型内存上限 8GiB／2 CPU；缓存网关 256MiB／0.75 CPU；relay 192MiB／0.5 CPU；宿主出站程序 256MiB／0.5 CPU。磁盘缓存与内存预算分开计算。日志有大小上限，结果库只接受固定城市目录；主试验 350 地点最多 700 项结果，另建的 1,000 地点探索夹具最多 2,000 项，不重叠运行两份 8GiB 模型引擎。

空缓存请求返回 HTTP 202，字段为 `null` 并标记 loading／unavailable；后台从真实模型填充。已有结果立即返回，天气与 AQ 各自带来源发布时间、起报时间、当前有效小时及状态。TTL 300 秒只安排复核，同一模型的成功复核不会改写 `fetched_at` 或起报时间。源有新批次、源失败、恢复后尚未检查或超出发布时间年龄时返回 stale。源已发布新批次后留 600 秒稳定期；元信息与引擎结果均校验后才落 SQLite。模型只支持滚动数据，报告会比较相同有效小时的值，不能仅凭滑动预报窗口声称模型升级。

`WEATHER43_PREWARM=0` 用于明确的完整冷缓存试验；上线候选可按已选城市设 35 或 350，由 `--prewarm` 在没有请求时主动填充，所有已访问／已恢复城市继续后台复核。持久库恢复无需等待源可达，但首次复核前明确为 stale。没有缓存且源离线时不会补零或默认城市。

压测只能在 Ubuntu 执行。先 `ulimit -n 8192`；`acceptance_load.py` 保存全部请求，包括未就绪、失败和超时，fresh/stale/loading 分开计数。持续负载每台 300 秒一次，同城与 350 城分别持续 1800 秒；同时启动按 350／1,000 请求、1／35／350 城、各三轮。压测器与被测服务共机，延迟包括本机调度，不能当作终端公网延迟或完整语音并发容量。

`build_locations.py --sample-size 1000` 生成另一个真实 GeoNames 样本，用于 1,000 不同坐标的全冷、热批次和 30 分钟探索负载。GeoNames 的样本包含城市及聚居点，并非去重的地级行政市；不同输入坐标也可能落在同一模型网格，验收报告须另外统计实际引擎返回网格。中文主名／别名覆盖、歧义及未命中均按真实库内容报告。

[run_coexistence.sh](../../scripts/weather43/run_coexistence.sh) 依赖已有的私有临时测试设备和合成 WAV，以当前产品许可签发令牌，经现有语音服务执行真实 ASR／模型／TTS，前、中、后分别测量。它只重启本次结果缓存容器以应用监听队列修复。服务器公网端口回环不通时使用 LAN 443，保持公开域名的 Host 与 TLS SNI、证书校验；这不是公网蜂窝或物理音箱测试。

[recovery_checks.py](../../scripts/weather43/recovery_checks.py) 只在持续负载与共存测试结束后执行。它使用专用 relay 的故障文件及新夹具目录，测试实际 TTL 到期、合并刷新、离线、慢源、持久重启、完整冷缓存及无请求预热；`finally` 清除故障并恢复本次引擎。40 秒延迟只作用于试验字节转发；不更改全机 DNS、防火墙或业务端点。模型与结果数据不删除，完整冷缓存使用新目录。出现低于 2GiB 可用内存时停止冷启动扩压并记录样本。

[observe_updates.py](../../scripts/weather43/observe_updates.py) 保留首次真实基线，连续记录源与本地结果；必须收齐基线后的两次 AQ 起报批次和至少一次天气更新，再由 [summarize_updates.py](../../scripts/weather43/summarize_updates.py) 核对持久快照、原始响应哈希、相同有效小时的值与过期状态。启动观察程序不能代替已完成观察。

结束时删除仅属于本次语音夹具的设备／令牌／会话及测试对话记录，保留真实授权和其他设备。是否保留隔离天气服务由验收结论决定；暂停用本项目 Compose `stop`，恢复用 `up -d` 并复用私有结果／模型目录。不要用 `down -v` 或全机清理命令回退。
