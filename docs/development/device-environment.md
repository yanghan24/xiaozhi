# P4 城市天气接口

设备向现有 TLS 控制入口发送 `GET /xiaozhi/device-control/v1/environment`，沿用逐台签发的 `Authorization: Bearer …` 和 `Device-Id`。管理 API 检查设备准入后，只向指定的私有 weather43 缓存发送位置查询。缓存按城市共享模型结果。

| 查询参数 | 行为 |
| --- | --- |
| 无参数 | 使用 TLS 入口观测的设备公网 IPv4 和离线 DB-IP City Lite 粗定位。IPv6、非公网地址、库未配置或无法唯一定位时返回 `unknown`。 |
| `city=长沙&province=湖南` | 以 GeoNames 城市别名匹配，省份可省略。名称有歧义时返回 `ambiguous` 及至多八个候选，音箱追问省份。 |
| `city_id=1815577` | 查询已选城市的 GeoNames ID。手选成功后在本机保存该 ID，后续刷新优先使用它。用户明确恢复自动定位时清除 ID。 |

查询值按 UTF-8 做 URL 编码。返回标准版本 1 的 `code=OK`、`data` 信封，`data.location_source` 为 `manual` 或 `ip`。未定位时 `city`、`weather` 和 `air_quality` 均为 `null`。唯一定位时 `city` 包含字符串 `id`、`name` 和 `province`。接口按所选城市即时读取缓存，不保存账号或设备设置。

| 字段 | 内容 |
| --- | --- |
| `weather` | `temperature_c`（°C）、`humidity_pct`（%）、`weather_code`（WMO）、中文 `description`、`model`、`valid_at`、`status` |
| `air_quality` | `pm25`、`pm10`、`unit`（μg/m³）、`model`、`model_estimate=true`、`valid_at`、`status` |

两个分项分别返回 `fresh`、`stale`、`loading` 或 `unavailable`。有效时间为 UTC ISO 8601 模型小时。缺值为 JSON null。客户端按分项保留旧值、城市和有效时间，并明确显示过期。未知 WMO 码没有中文描述。数据处理包含逐时取样、WMO 中文映射及 PM 浓度显示。天气代码对应 [Open-Meteo WMO 表](https://open-meteo.com/en/docs#weather_variable_documentation)。

## 接入私有试点缓存

1. 复用已准备的 [weather43 缓存及模型](../../deploy/weather43/README.md)和持久数据。将同版 GeoNames 完整目录放在 `private/location/china-city-catalog.json`，缓存可以继续只服务试点城市。重名判断查完整目录，超出缓存覆盖的城市返回 `unknown`。在现有 Ubuntu 私有试点目录使用 `compose.acceptance.yaml` 时，追加 `-f deploy/weather43/compose.device-environment.yaml` 并将新增的两个 Python 模块放在对应挂载路径。启用 IPv4 时，将已校验的 DB-IP City Lite MMDB 放在私有目录 `private/location/`，设置 `WEATHER_IP_DATABASE=/location/<文件名>.mmdb`，运行镜像需包含 `maxminddb`。仅测试语音选城时该变量可留空。
2. 建立内部 Docker 网络 `docker network create --internal xiaozhi-weather-device`，在管理 API 的私有 Compose 中将 `api` 同时接入其原网络及此 external 网络。覆盖文件会将缓存以 `weather-device-cache` 别名接入同一网络。设置管理 API 环境变量 `DEVICE_CONTROL_WEATHER_ORIGIN=http://weather-device-cache:8081`。保持缓存端口只绑定回环，公网 Nginx 只转发设备 GET 到管理 API。未配置缓存时，经认证的设备收到 503 `WEATHER_UNAVAILABLE`。
3. 沿用[控制入口的可信代理配置](device-control-v1.md)，核对 Tomcat 仅信任实际入口代理。Nginx 以连接对端覆盖转发地址；业务代码只读取 servlet 的有效远端地址。手选城市请求不会向缓存发送设备 IP。关闭试点可撤回准确的天气路由或清空管理 API 的天气 origin，保留已建缓存和模型数据。

在设备可访问的说明中保留 Open-Meteo、NOAA/NCEP、CAMS / Copernicus / ECMWF、GeoNames 以及已启用的 DB-IP City Lite 的来源和署名。数据许可、下载版本和处理说明见[运行版本与署名清单](../planning/issue-43-runtime-notices.md)。[任务 #44](https://github.com/yanghan24/xiaozhi/issues/44) 的 Ubuntu 联调、P4 首页实测和加载试验需针对实际部署分别记录。
