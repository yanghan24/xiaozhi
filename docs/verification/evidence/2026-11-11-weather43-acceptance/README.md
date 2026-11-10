# #43 最终证据

真实服务测试均运行于指定 Ubuntu。用户最后确认的环境请求并发为 50 路；350／1,000 台文件保留历史范围。研究结论与限制见[验收报告](../../2026-11-10-weather43-acceptance.md)。

- `stage50-summary.json`：50 路容量、冷填充、资源与版本一致性。
- `voice-comparison.json`：40 个同账号串行真实问答、原始分位数重算及实际冷期重叠；`fixture-cleanup.json`：两个唯一测试设备清理。
- `model-updates.json`、`model-grid-coverage.json`：两次 AQ／天气起报更新及主缓存 700 项的最新 run、源 SHA 和网格核验。
- `network-soak.json`：5 小时 35 分钟、1,344 次无重试源读取；成功 1,340、失败 4。
- `metrics-final.json`：76 个完整负载档案的原始计数／延迟及共享宿主资源重算；记录一致不表示所有历史测试成功。
- `runtime-inventory.json`：最终镜像、实际源码 SHA、资源和 relay 计数。
- `raw-results.tar.gz`、`checksums.json`、`archive-receipt.json`：394 份证据／工具和内置校验清单；22,152,891 字节，SHA-256 `da4647480ff0034b4e78c2c79b8a3aed834951eeb803032f4faac6fa9fe34b1a`。
- `local-verification.json`：下载后逐成员 SHA／大小／路径核验及独立网络重算，属于本地证据分析，不是 Windows 后端测试。

归档保留失败、修复前结果、API 换版后失去可比性的批次、原始请求、源更新持久快照、固定城市样本和工具。完整大型城市目录未打包，其摘要／输入版本收据和编译工具保留；服务器原件保留。历史冷夹具已停止，不能把其旧模型批次当作当前结果。

实际代码节点为 `5a7219c7536ec3fd0c05bfdea58fbbaf2c34a711`；源码运行时字节 SHA 另存，避免换行规范化造成混淆。服务器检查两个测试身份、现有代理节点及新模型 Key；Windows 检查 Git／SSH／代理／新模型秘密，并扫描解压后的归档成员。私有配置、Key、设备接入秘密、OAuth 入口、SQLite 数据库和录音均不在允许列表中。

数据署名：Open-Meteo、NOAA/NCEP、Copernicus CAMS/ECMWF、GeoNames、DB-IP。模型数值为加工后的估计值，数据按 CC-BY-4.0 保留来源与修改说明；不提供监测站观测或中国 AQI。程序、Python 包及实际 notices 的分别要求见[运行版本与许可](../../../planning/issue-43-runtime-notices.md)。
