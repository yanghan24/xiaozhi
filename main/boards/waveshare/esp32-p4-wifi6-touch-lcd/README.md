# Waveshare ESP32-P4-WIFI6-Touch-LCD


**ESP32-P4-WIFI6-Touch-LCD** series is the ESP32-P4 +ESP32-C6 screen development board launched by waveshare

## Configuration

Configuration in `menuconfig`.

Selection Board Type `Xiaozhi Assistant --> Board Type`
- Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
- Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3
- Waveshare ESP32-P4-WIFI6-Touch-LCD-7B
- Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
- Waveshare ESP32-P4-WIFI6-Touch-LCD-4C
- Waveshare ESP32-P4-WIFI6-Touch-LCD-7
- Waveshare ESP32-P4-WIFI6-Touch-LCD-8
- Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1

## 4.3 英寸产品版的待机天气

本实现面向公开的 `firmware/p4`，通过 `device-control/v1/environment` 认证接口获取本地天气。固件提供 `self.environment.set_city` 和 `self.environment.get_status`，支持按城市与省份消歧并设置本机覆盖，传入 `city=auto` 可恢复 IP 定位。

目前公开的产品语音服务仅向模型开放 `ask_knowledge`。要让用户通过语音选城，还需在授权产品轮次中接入上述工具，并确认实际设备完成消歧和保存。采用 `speaker/v1` 的现场版本需要在其自身协议和固件分支上移植、联调。

天气和空气质量分别显示模型有效时间（UTC）。两个 PM 指标的单位均为微克每立方米，屏上记为 ug/m3。过期标记表示最近缓存数据，缺失指标显示为双横线。面板展示按城市网格和小时提取的模型估算值。

模型数据由 Open-Meteo 处理，天气来自 NOAA/NCEP，空气质量来自 Copernicus CAMS／ECMWF。欧盟委员会与 ECMWF 对数据使用结果不承担责任。城市别名来自 GeoNames，IP 粗定位来自 DB-IP City Lite。[Open-Meteo 数据许可](https://open-meteo.com/en/licence) · [Copernicus CAMS](https://atmosphere.copernicus.eu/) · [GeoNames](https://www.geonames.org/) · [IP Geolocation by DB-IP](https://db-ip.com/) · [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
