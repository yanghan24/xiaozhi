# #43 运行版本、数据许可与署名

核对日期：2026-11-10。范围为本次隔离可行性环境；程序、模型数据、地理数据分别列明。没有购买天气 API，也没有将公共非商业托管预报接口作为回退。

| 实际使用项 | 固定版本／来源 | 许可与交付要求 |
| --- | --- | --- |
| Open-Meteo 官方容器 | 1.8.0，源码 `b66bc9d58a5427562625383cfbd70ff6856ad4a5`，镜像 ID `7b1114b8a78e27fabac5c08a322ceb8d540b0a3c75419941d9e26d54dfc379df` | [该提交 LICENSE](https://github.com/open-meteo/open-meteo/blob/b66bc9d58a5427562625383cfbd70ff6856ad4a5/LICENSE)：AGPL-3.0。原镜像未改程序，通过独立 HTTP 服务使用；保留许可及上游源码入口。若以后修改程序或形成衍生作品，按 AGPL 处理对应源码及网络使用义务，不能把仓库 MIT 标记套到它上面。 |
| 天气与 AQ 分发数据 | 匿名读取 `openmeteo.s3.amazonaws.com/data/` 的 GFS013、CAMS Global | [Open-Meteo Open Data](https://github.com/open-meteo/open-data#license) 和[数据许可](https://open-meteo.com/en/licence)：CC-BY-4.0；署名、许可链接和处理说明。自建路径的可商用说明见[官方自建指南](https://github.com/open-meteo/open-meteo/blob/main/docs/getting-started.md#license)，不等同于[公共免费 API 的非商业条款](https://open-meteo.com/en/terms)。 |
| 原始气象模型 | NOAA/NCEP GFS013 | 在 Open-Meteo 之外同时保留 NOAA/NCEP 来源。[Open-Meteo 来源清单](https://open-meteo.com/en/licence)链接原始提供方条款；本次没有绕过数据授权下载受限模型。 |
| 原始空气质量模型 | ECMWF／Copernicus CAMS 全球大气成分预报 | [当前数据集](https://ads.atmosphere.copernicus.eu/datasets/cams-global-atmospheric-composition-forecasts?tab=overview)的官方 catalogue 元数据标为 CC-BY-4.0。本次使用 Global，不能将欧洲 ENSEMBLE 的作者名单当作本次模型来源。提供 Copernicus、CAMS、ECMWF 与 Open-Meteo 署名及处理说明；见[官方署名指南](https://confluence.ecmwf.int/spaces/CKB/pages/58131166/How+to+reference+or+acknowledge+data+from+CAMS+Copernicus+Atmosphere+Monitoring+Service+in+a+publication)。 |
| Ubuntu 出站程序 Mihomo | v1.19.33，官方 linux-amd64-v1，二进制 SHA-256 `19cbe7ba1523a92ed2d0a30716ea093de57db05b7844db23de2b9c21fd7a1afa` | [固定标签 LICENSE](https://github.com/MetaCubeX/mihomo/blob/v1.19.33/LICENSE)：GPL-3.0；保留许可证及对应源码入口。只复用现有获准出站配置，私有节点配置不入 Git。 |
| 本次缓存、转发及探针 | 本 PR 的 `scripts/weather43` | 仓库 [MIT LICENSE](../../LICENSE)；独立进程未复制 Open-Meteo 源码。 |
| Python / SQLite | 复用既有语音镜像，Python 3.10.22 | Python 许可及 libopus notice 从实际镜像读取留存。镜像 ID `e519f14843fefbeab0e3c966da0733b81be7e1512b1c9774579f5d252a0a67a8`；没有在主机全局安装依赖。 |
| aiohttp 及直接依赖 | aiohttp 3.14.5；aiosignal 1.4.0、attrs 26.1.0、frozenlist 1.8.0、multidict 7.1.0、propcache 0.5.4、yarl 1.25.1、idna 3.20 | 实际安装的 aiohttp SPDX 为 **Apache-2.0 AND MIT**（含 llhttp）；其余为 Apache-2.0、MIT 或 BSD-3-Clause。按安装包内 LICENSE／NOTICE 留存，不能凭旧版印象只写 Apache。 |
| 语音负载工具 | websockets 14.2、opuslib_next 1.3.1；系统 libopus | 前两者 BSD-3-Clause；libopus 保留实际系统包 copyright。只用既有合成测试录音，不采集客户音频；报告保存帧数与解码摘要。 |
| MMDB 读取器 | maxminddb 2.8.2 官方 sdist，SHA-256 `26a8e536228d8cc28c5b8f574a571a2704befce3b368ceca593a76d56b6590f9` | sdist 内 LICENSE 为 Apache-2.0；只使用其纯 Python 读取器。它是读取程序，不代表 DB-IP 数据许可。 |
| GeoNames 城市及别名 | 当日 `cities500.zip`、`CN.zip`、`alternatenames/CN.zip` | [官方说明](https://download.geonames.org/export/dump/readme.txt)：CC-BY-4.0。保留 GeoNames 链接、下载哈希及筛选／编译说明。当前导出及修改／删除增量可用于更新，不能只新增而遗漏删除。 |
| DB-IP City Lite | 2026-11 MMDB；126,873,350 字节，公布 MD5 校验通过 | [下载及许可](https://db-ip.com/db/download/ip-to-city-lite)：CC-BY-4.0，月更，Lite 的覆盖和精度较低。使用结果的网页需链接 DB-IP；读取器支持 IPv6 不代表城市准确率。 |
| RIPE Atlas 参考元数据 | 当日公开 anchors/probes REST 查询 | 只作为公开操作方参考位置的一致性比较，保留 RIPE 来源及查询条件；遵守[服务条款](https://www.ripe.net/about-us/legal/ripe-atlas-service-terms-and-conditions/)。没有对探针发起网络测量，也没有将参考点当作现场测绘真值。 |

运行证据须包括镜像 ID、二进制与数据哈希、安装包许可文件及 notice 摘要。复用镜像的全部 84 项 Python 包版本／许可元数据也留档；该清单不把未知元数据自动判断为开源许可。本 PR 不分发基础镜像、数据库或代理订阅。

显示模型结果时使用如下署名，并链接各来源与 [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/)：天气与空气质量由 [Open-Meteo](https://open-meteo.com/) 提供模型数据处理，天气源为 NOAA/NCEP；空气质量使用 2026 年 [Copernicus CAMS](https://atmosphere.copernicus.eu/)／ECMWF 资料。本项目将资料按城市网格和小时提取并缓存；结果为模型估计。欧盟委员会与 ECMWF 不对使用结果承担责任。

城市资料署名 [GeoNames](https://www.geonames.org/)，注明筛选大陆候选、合并别名和生成测试样本；IP 结果署名 [IP Geolocation by DB-IP](https://db-ip.com/)。这些链接应随结果展示进入后续产品页面，不能仅藏在开发报告中。

地理库更新采用“下载到新版本目录 → 校验公开摘要及本地 SHA-256 → 离线解析、边界和中文歧义回归 → 原子切换版本 → 保留前版回退”的流程。GeoNames 还须处理删除记录；DB-IP 每月取得新 MMDB，离线校验后重开 reader。当前验证只使用 2026-11 实际版本，不宣称已验证尚未发布的下月数据库。

已在 Ubuntu 以同一真实版本的两个目录执行离线切换演练：错误摘要与损坏格式均被拒绝，活动版本保持可读；正常版本通过中文解析检查后原子切换，已有 reader 继续读取，新 reader 重开后一致，随后可回退。工具为 [location_update_checks.py](../../scripts/weather43/location_update_checks.py)，不把这次机制验证计为跨月数据更新。
