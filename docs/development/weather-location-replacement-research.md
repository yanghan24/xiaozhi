# 室外天气、空气质量与定位服务替换调研

查证日期：2026-11-10。本文记录官方资料和本轮项目上下文。用户已同意先验证自建 Open-Meteo，空气质量首版采用明确标注的 PM2.5、PM10 模型浓度；不代表已选定正式部署或通过容量验收。未实现、未部署、未调用带凭据的天气接口。

## 决策相关结论

用户已明确：显示室外环境；希望服务免费，且服务端同样不需要第三方密钥；定位范围为中国大陆城市，接受 IP 粗定位，语音指定的城市优先。项目面向外部客户，首批约 350 台设备，不能直接假定属于供应商定义的非商业用途。

1. **尚无经过实际部署验证、同时满足全部要求的现成托管服务。** Open-Meteo 公共 API 无需密钥，但免费托管服务仅允许非商业使用。和风、心知均需要凭据；心知免费天气实况还不含湿度。[OM1][OM2][Q3][S1][S2]
2. **Open-Meteo 自建是符合“无天气 API 购买费、无第三方 API key”方向的候选。** 官方明确允许商业与非商业自建；启动示例直接读取公开 S3 数据，未配置第三方 API key；公开数据分发同时包含全球天气与 CAMS 空气质量。但服务器、存储、带宽与运维有成本，不能称为零运行成本。[OM5][OM6]
3. **数据口径已确认采用模型浓度并明确标注。** Open-Meteo 当前天气来自数值模式，空气质量也来自模式；中国区域的 CAMS 全球空气质量约 45 km 分辨率，不能当作设备附近监测站实测。官方接口提供美国和欧洲 AQI，没有本轮查到的内置中国 AQI 字段。用户已同意首版显示带单位、模型估算标识和有效时间的 PM2.5、PM10 浓度。[OM3][OM4]
4. **定位能力单独选择。** 天气 API 需要城市或经纬度，不能让没有 GNSS 的 Wi-Fi 设备获得精准定位。IP 只能作为粗定位；服务器代请求时必须使用设备连接的可信公网 IP，不能让上游根据服务器出网 IP 自动判断位置。语音／人工指定城市应覆盖 IP 推断。[Q6][S3]
5. **当前实机是 Waveshare ESP32-P4 WiFi6 Touch LCD 4.3 + C6，并非最初描述的 ESP32-S3。** 本轮现场源码核查显示现有 `aggregate_url` 只解析空气质量，刷新流程仍先要求高德凭据并调用高德定位与天气，因此只改此 URL 不能完成替换。候选改法是在 `EnvironmentService::Refresh` 接入自有服务统一返回位置、天气与空气质量，尽量沿用 `EnvironmentSnapshot`、仪表盘和 NVS 缓存。此为实现建议，尚未实施。

当前决定是先验证自建，必须考虑 350 台及更大规模。正式部署的资源与运维安排、具体刷新和缓存期限需依据验证结果落实；不得把收费托管档标成符合“免费无第三方密钥”的选项。完整产品决定见[需求访谈](../planning/reminders-weather-discovery-2026-11-10.md)。

## 三类候选比较

| 方案 | 天气／温度／湿度 | 空气质量 | 凭据、费用与许可 | 对当前约束的结论 |
| --- | --- | --- | --- | --- |
| Open-Meteo 公共托管 API | `temperature_2m`、`relative_humidity_2m`、`weather_code`；模式估计 | `pm2_5`、`pm10`、气体浓度、`us_aqi`、`european_aqi`；模式预报 | 无 key；免费托管限非商业，另有调用限制与署名要求 | 可用于符合许可的验证；不能直接作为面向外部客户的永久免费方案 |
| Open-Meteo 自建 | 同一开源 API，可读取公开天气数据 | 公开分发列有 `cams_global` 等空气质量数据 | 官方允许商业自建；示例无第三方 key；软件 AGPL-3.0、数据 CC BY 4.0；自行承担基础设施 | 候选，需部署与数据完整性验证；没有内置中国 AQI 的已核实支持 |
| 和风天气 QWeather | 城市 v7 与当前经纬度接口均有天气、温度、湿度，结构不同 | 本地中国 AQI、PM2.5 等；中国 AQI 含模式与观测站数据 | 独立 API Host + JWT／API key；按量计费有共享免费额度；需注明来源 | 能力接近需求，但需要凭据；不满足当前严格约束 |
| 心知天气 Seniverse | 付费实况包含湿度；免费仅天气文字、代码、气温 | 城市／监测站 AQI、PM2.5 等接口 | API key 或签名；免费数据要注明来源；空气质量套餐权限及现价未完整核实 | 免费天气档不能满足全部字段，且需要凭据 |

来源分别为 [OM1]–[OM6]、[Q1]–[Q9]、[S1]–[S5]。以上“覆盖”均为供应商官方描述，不是中国部署网络或实机测量结果。

## Open-Meteo：公开托管与自建应分别评估

### 公共托管的边界

官网明确：公共免费 API 用于非商业用途；无需 API key。当前公布的限制为每分钟 600 次、每小时 5,000 次、每天 10,000 次、每月 300,000 次。商业托管需订阅，并使用 `customer-` 域名及 `apikey`；它不满足本轮“免费且服务端也无第三方 key”的要求。数据 CC BY 4.0 允许商业利用，并不意味着其公共托管 API 允许免费商业调用，这两层许可不能混为一谈。[OM1][OM2]

按 350 台、每 30 分钟各拉一次天气和空气质量、30 天计算，若没有跨设备共享缓存，则约为 `350 × 48 × 2 = 33,600` 次／日、`1,008,000` 次／月，尚未计定位、重试及语音查询。该数量已经超过公共免费限额；共享城市缓存可减少请求，但不能改变非商业使用条件。此为容量估算，不是实际调用量。

天气请求示意：`/v1/forecast?latitude=...&longitude=...&current=temperature_2m,relative_humidity_2m,weather_code`。温度默认摄氏度，相对湿度为百分比；`weather_code` 需要转换为本地文案与图标。官方称 current 基于 15 分钟模式数据，不能解释为每 15 分钟获得一次当地仪器实测；部分区域原始数据经时间插值。[OM3]

空气质量请求使用 `/v1/air-quality`，可选 `current=pm2_5,pm10,us_aqi` 等。PM2.5、PM10 默认单位为 μg/m³；API 接受 WGS84 经纬度。中国区域采用全球 CAMS，官方 API 页面列约 45 km、原始 3 小时步长、每 12 小时更新；返回的逐小时／current 序列不意味着原始模式每小时／每 15 分钟重新运行。[OM4]

公开数据仓库的 CAMS 表列处理后逐小时序列，全球分辨率写为约 44 km；这与 API 文档列的原始 3 小时步长／约 45 km 不应合并描述成监测站分辨率。实际部署需固定模型、版本及更新元数据进行核对。[OM4][OM6]

`us_aqi` 与 `european_aqi` 按各自标准计算，不能换成“中国 AQI”标签。仅用一个小时的 PM2.5 浓度也不能据此宣称算出了符合中国规定的综合 AQI；需要另行核对标准版本、污染物、平均时段、缺值规则与插值规则。[OM4]

### 自建部署事实及仍需验证的部分

官方自建文档提供 Docker 与 Ubuntu 22.04 预编译包。系统由 HTTP API 服务、文件数据库、下载／更新程序组成。Docker 示例设置 `REMOTE_DATA_DIRECTORY=https://openmeteo.s3.amazonaws.com/data/`，按请求读取公开数据并缓存；无需一开始下载全部全球数据。该示例未要求 Open-Meteo 订阅 key 或 AWS 密钥；公开分发仓库明确包含天气和 CAMS 空气质量。[OM5][OM6]

官方建议的资源为最低 8 GB 内存、推荐 16 GB、至少 100 GB 存储，偏好高 IOPS 的 NVMe SSD。Docker 示例使用 8 GB 本地缓存。这是官方部署指导，不能当成项目的容量验收：现有 Ubuntu 服务器曾记录的历史资源快照，不代表当前资源，更不能证明同机叠加语音服务后仍有足够余量。[OM5]

远程数据模式的冷缓存首请求可能需要数秒；官方说明会约每两分钟检查 S3 预报更新并预加载变化。模型自身仍按各自周期发布，检查频率不会使预报变成分钟级实测。也可同步所需模型与字段到本地，但需要持续更新、清理旧数据、监测时效与控制磁盘增长。[OM5][OM6][OM7]

官方自建许可明确允许商业使用。软件／Docker 镜像为 AGPL-3.0，数据为 CC BY 4.0，需保留版权与来源；修改 AGPL 程序并通过网络提供服务时，许可第 13 条要求向远程交互用户提供对应源代码。独立业务适配服务与开源程序的组合方式需要在落地设计时核对，不能把“可商用”解释成没有许可义务。[OM5][OM8]

本轮没有部署容器、读取完整实际 AQ 数据或开展性能测试。天气与 AQ 可共用开源服务和公开分发数据，是官方材料支持的技术候选；仍须验证实际端点可用、模型字段齐全、匿名数据读取、首次加载与缓存命中延迟、国内 S3 链路、更新失败行为，以及与语音服务同机的资源竞争。不要将改为自建等同于已经解决国内可达性。

直接向原始供应商下载是另一条路径，不能笼统声称所有原始数据源都不需要注册：例如官方下载文档的 ERA5 路径明确需要 Copernicus 凭据。当前室外实时天气／空气质量候选应先验证公开 S3 分发路径；本轮不扩展到历史再分析数据。[OM7]

## 和风：已有复用点，但需要凭据

当前所查官方“实时天气”页面描述 `GET /weather/v1/current/{latitude}/{longitude}`，提供约 1 km 网格、全球位置、分钟级更新；字段为 `condition.text/code`、`temperature.value/unit`、`humidity`。这里 `humidity` 是 `[0,1]` 的比例值，展示百分比需转换。[Q1]

**旧城市 v7 接口仍有独立官方页面**：`GET /v7/weather/now?location=...`，返回 `now.temp`、`now.text`、`now.humidity`（百分比字符串）、`now.obsTime` 和 `updateTime` 等。本轮没有依据认定该接口已经失效或强制弃用；旧文档路径跳转到新页面也不能证明旧 API 停服。现有对话天气插件使用和风天气，可作为后续复用候选，但插件本身不等于已实现设备屏幕与 AQI。[Q2]

空气质量页面描述 `/airquality/v1/current/{latitude}/{longitude}`，通过 `indexes[]` 返回 AQI 标准、值、类别等，通过 `pollutants[]` 返回污染物浓度；PM2.5 的 code 为 `pm2p5`，不能假定与旧结构同名。需要按 `indexes[].code` 选定标准，不能取数组首项就当中国 AQI。[Q7][Q8]

当前 AQI 列表包含 `cn-mee`（AQI CN）及 `cn-mee-1h`（AQI-1H CN）。中国说明页当前写明遵循 HJ 633—2026，数据由数值模型和观测站数据计算而来，未经完整审核修订确认，仅供参考；中国暂不支持通用 QAQI，低污染时首要污染物可以为空。由此不能将所有格点 AQI 宣称为已审核的官方监测站实测。[Q8][Q9]

鉴权应使用开发者独立 API Host。官方提示旧公共域名 `api.qweather.com`、`devapi.qweather.com`、`geoapi.qweather.com` 从 2026 年起逐步停止服务；这和 v7 路径是否有效是两件事。[Q4]

JWT 使用 Ed25519／EdDSA，Header 有 `kid`，Payload 包括 `iss`、`sub`、`iat`、`exp`，当前最大有效期 24 小时。API 也支持 API key，但官方提示自 2027-01-01 起限制 key 认证的每日请求数量；本轮没有查到可据以承诺的具体限值。应以自己的账号、API Host、接口与控制台权限重新验证，不能沿用旧配置就假定可用。[Q3]

当前官方按量计费把天气预报、空气质量、GeoAPI 等归入“天气和基础服务”，该组请求量合并累进，前 50,000 次／月免费，之后的 950,000 次为每次 CNY 0.0007；不是每个接口各有 50,000 次。免费额度不代表无需账号或凭据，也不保证实际设备规模没有费用。本轮不替用户开通、承诺商业授权或报价；后续若放宽无 key 条件，再核对账号条款与预估用量。[Q5]

需要注明来源；GeoAPI 数据有缓存、批量提取与索引限制，不能把供应商城市查询结果批量保存成自己的离线城市数据库。普通天气缓存和 GeoAPI 内容存储要分别核对。[Q10][Q11]

## 心知：免费档字段不足，IP 定位语义清楚

天气实况 `/v3/weather/now.json` 的付费结构有 `now.text`、`now.temperature`、`now.humidity`（0–100 百分比）及 `last_update`。官方明确免费用户仅返回天气现象文字、代码和气温三项；因此免费档缺湿度，不满足当前要求。官方同时说明中国城市暂不支持该接口中的云量与露点温度。[S1]

空气质量 `/v3/air/now.json` 提供城市与监测站数据，含 `aqi`、`pm25`、`pm10`、气体浓度、`quality`、`last_update` 等；`pm25` 注释为一小时平均、μg/m³。`scope=city` 返回城市值，`scope=all` 还含站点。空气质量接口的当前免费资格、产品套餐、调用限制与正式 AQI 标准版本，本轮未完整查证，不能以“文档有字段”推导“免费账号可使用”。[S2]

`location` 支持城市 ID、名称、`纬度:经度`、具体 IP，或字符串 `ip`；`ip` 表示自动识别**请求 IP**，官方注明某些 IP 无法定位到城市。自有服务端代请求若传 `location=ip`，得到的是服务器出网 IP 对应城市，而非设备所在地；即使显式传设备公网 IP，也只有 IP 推断精度。[S3]

心知支持直接 key 参数或公钥加私钥签名，均有凭据；免费展示需注明来源。官方开始页声称覆盖全国 3,156 个、全球 24,962 个城市／地点，本轮未逐城验证，也不能由总城市数推断空气质量对每个城市都完整覆盖。[S4][S5]

## 免费、无第三方 key 的定位候选

定位服务与城市解析分开处理：先得到设备应使用的城市，再把该城市解析到稳定的 WGS84 经纬度；城市中心点天气不等于客户所在街道天气。高德 adcode、和风 Location ID、心知城市 ID、GeoNames ID 也不能直接互换。

已确认的优先级为：客户通过语音指定的城市 → 设备可信公网 IP 推断的大陆城市。客户指定城市持续生效，IP 变动不能自动覆盖，并支持客户要求恢复自动定位。定位失败时必须明确展示未取得位置，不能静默把任意默认城市当作设备所在地。

无 GNSS 的 Wi-Fi 连接本身不能给出精确坐标。若改用浏览器／手机定位或 Wi-Fi 定位数据库，就新增了独立定位能力与权限、覆盖及服务成本问题，不属于天气 API 自带能力。[Q6][S3]

服务器读取设备 IP 时应使用连接来源，并仅接受可信反向代理写入的转发头；不能相信客户端随意提交的 `X-Forwarded-For`。CGNAT、移动网络、VPN、代理和跨城出口均可能影响城市推断，定位结果需允许客户纠正。此为系统设计建议，并非本轮已完成的定位准确率测试。

离线 `ip2region` 可作为免费无 key 的 IP→城市候选；本轮尚未完成其官方仓库许可、数据库更新、IPv4／IPv6覆盖和中国城市精度核验，因此不写成已满足要求的方案，不给出准确率承诺。

城市→经纬度可考虑本地城市表或 GeoNames 数据。Open-Meteo 的 geocoding 官方文档说明其地点数据基于 GeoNames，但其**公共托管 geocoding API 同样不能被默认当作可商业免费调用**。本轮未完成 GeoNames 原始数据的具体许可、版本、中文别名、行政区消歧、城市粒度与更新流程核验；仅记录为后续候选，不建立数据库。[OM9]

没有经纬度时，天气 API 不会自动补出设备精确位置。若要求完全无第三方定位 key，首个可验收版本应明确测试“语音指定大陆城市→本地坐标→天气／空气质量”的闭环，再独立验证离线 IP 粗定位能力。

## 候选接入方式与时效处理

候选链路是：小智设备 → 自有环境数据接口 → 城市与坐标解析 → 自建 Open-Meteo（或将来获授权的供应商适配层）。设备使用项目自身鉴权，不内置第三方密钥；自有接口也不应开放成任意匿名天气代理。用户已批准按此方向先验证，正式部署仍以验证结果为前置，当前未实施。

服务端统一返回天气、摄氏温度、百分比湿度、空气质量指标、城市及定位来源。每项数据保留 `provider`、`metric/standard`、单位、数据有效时间／观测时间、获取时间与 `stale` 状态；“获取时间”不能替代真实观测时间，模式 valid time 也不能写成观测时间。

缓存按规范化城市／坐标、供应商、模型、AQI 标准和单位区分；相同城市的设备可共享环境数据缓存，位置归属与客户设置仍属于各自设备。天气与空气质量更新频率不同，应分别设置时效，避免一项失败清掉另一项可用数据。[Q11][OM3][OM4][OM5]

候选刷新节奏可先用天气 15–30 分钟、空气质量 30–60 分钟开展验证，并根据模式更新与用户期望调整。和风最佳实践给出的建议缓存为实时天气 10–30 分钟、实时空气质量 30–60 分钟；其最新天气接口页面又描述分钟级更新，因此需要按最终使用的具体接口与数据更新时间设定，不能照搬成所有供应商的保证。[Q1][Q11]

缺少、为空、超时或格式不符的温度／湿度／浓度应视为不可用，不能补零；AQI 标准缺失时不默认为中国标准。允许保留上次有效缓存时，必须显示更新时间与过期状态，并规定最长可显示时长。过期、断网、无定位、首次加载和时钟未同步均需实机验收。

中国境内可达性均未测试：本轮读取官网成功只代表调研环境能读取文档，不代表 Ubuntu 服务器或设备能稳定调用 API／AWS 数据。需在实际部署环境测 DNS、TLS、首次取数、热缓存延迟、失败重试与数据更新；约 350 台设备及共享缓存能否支撑，应另做负载与资源验证。

## 后续验证的入口

用户已选择先验证自建 Open-Meteo、离线城市解析与 IP 粗定位，并接受空气质量先显示模型浓度。应验证 350 台设备对应的请求量与城市分布、同时启动／缓存失效的突发请求，以及更大规模下的缓存共享、资源瓶颈和扩容方式。相同城市缓存命中良好不能替代多城市冷缓存的测试。用户已批准以 1,000 台作为扩容探索，相关规格与任务为 [#40](https://github.com/yanghan24/xiaozhi/issues/40)、[#43](https://github.com/yanghan24/xiaozhi/issues/43)、[#44](https://github.com/yanghan24/xiaozhi/issues/44)，先纳入 2026-12-07 交付目标。

验证报告应包含无第三方 key 读取、字段完整性、源数据更新、冷／热缓存耗时、错误与超时、CPU／内存／磁盘／带宽，以及上游不可达的过期处理。在实际 Ubuntu 环境执行，并区分后台负载模拟与真实 P4 首页显示；本轮没有开展这些测试。

如果自建验证不满足需要，应带着测量结果重新选择方案；不能把公共非商业接口或收费密钥接口自动切入客户服务。现有公共资料不构成设备实测验收、特定账号权限或服务持续可用的保证。

## 官方资料

以下页面均于 2026-11-10 查阅；GitHub `main` 与官网会变动，实施时应固定版本并再次核对。

- [OM1] [Open-Meteo 官方 README：免费托管、模式、数据与代码许可](https://github.com/open-meteo/open-meteo/blob/main/README.md)
- [OM2] [Open-Meteo Pricing：非商业限制、调用量、商业端点与署名](https://open-meteo.com/en/pricing)
- [OM3] [Open-Meteo Weather Forecast API：current、温度、湿度与天气代码](https://open-meteo.com/en/docs)
- [OM4] [Open-Meteo Air Quality API：CAMS、指标、分辨率与更新频率](https://open-meteo.com/en/docs/air-quality-api)
- [OM5] [Open-Meteo 官方 Getting Started：自建、资源、缓存、商业许可](https://github.com/open-meteo/open-meteo/blob/main/docs/getting-started.md)
- [OM6] [Open-Meteo on AWS Open Data：天气与空气质量数据分发](https://github.com/open-meteo/open-data/blob/main/README.md)
- [OM7] [Open-Meteo 官方 Downloading Weather Models：同步与原始数据下载](https://github.com/open-meteo/open-meteo/blob/main/docs/downloading-datasets.md)
- [OM8] [Open-Meteo AGPL-3.0 许可，特别是第 13 条](https://github.com/open-meteo/open-meteo/blob/main/LICENSE)
- [OM9] [Open-Meteo Geocoding API：城市搜索、GeoNames 来源与商业 apikey](https://open-meteo.com/en/docs/geocoding-api)
- [Q1] [和风：当前经纬度实时天气接口](https://dev.qweather.com/docs/api/weather/weather-current/)
- [Q2] [和风：城市实时天气 v7](https://dev.qweather.com/docs/api/weather/weather-now-webapi-v7/)
- [Q3] [和风：身份认证](https://dev.qweather.com/docs/configuration/authentication/)
- [Q4] [和风：API Host](https://dev.qweather.com/docs/configuration/api-host/)
- [Q5] [和风：按量计费定价](https://dev.qweather.com/docs/finance/pricing/)
- [Q6] [和风：城市搜索与经纬度参数](https://dev.qweather.com/docs/api/geoapi/city-lookup/)
- [Q7] [和风：实时空气质量](https://dev.qweather.com/docs/api/air-quality/air-current/)
- [Q8] [和风：支持的空气质量指数](https://dev.qweather.com/docs/api/air-quality/aqi-list/)
- [Q9] [和风：中国空气质量说明](https://dev.qweather.com/docs/api/air-quality/china-aqi/)
- [Q10] [和风：使用限制、注明来源与地理数据存储](https://dev.qweather.com/docs/terms/restriction/)
- [Q11] [和风：缓存你的数据](https://dev.qweather.com/docs/best-practices/cache/)
- [S1] [心知：天气实况与免费字段限制](https://docs.seniverse.com/api/weather/now.html)
- [S2] [心知：空气质量实况](https://docs.seniverse.com/api/air/now.html)
- [S3] [心知：location 等通用参数与 IP 定位语义](https://docs.seniverse.com/api/start/common.html)
- [S4] [心知：密钥与签名认证](https://docs.seniverse.com/api/start/key.html)
- [S5] [心知：来源标注、城市覆盖与使用说明](https://docs.seniverse.com/api/start/start.html)
