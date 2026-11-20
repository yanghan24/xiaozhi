"""Device view for the private weather cache; the manager API authenticates callers."""
import ipaddress
import re

from aiohttp import web

from location_candidates import CityCatalog, IpDatabase, public_ip


# Open-Meteo's WMO weather interpretation codes (WW).
WEATHER = {
    0: "晴", 1: "大部晴朗", 2: "多云", 3: "阴", 45: "雾", 48: "雾凇",
    51: "小毛毛雨", 53: "中毛毛雨", 55: "浓毛毛雨", 56: "轻微冻毛毛雨", 57: "强冻毛毛雨",
    61: "小雨", 63: "中雨", 65: "大雨", 66: "小冻雨", 67: "强冻雨",
    71: "小雪", 73: "中雪", 75: "大雪", 77: "雪粒", 80: "小阵雨", 81: "中阵雨",
    82: "强阵雨", 85: "小阵雪", 86: "强阵雪", 95: "雷暴", 96: "雷暴伴小冰雹",
    97: "强雷暴", 99: "雷暴伴大冰雹",
}


def display_city(city):
    return {"id": str(city["id"]), "name": city.get("chinese_name") or city["name"],
            "province": city.get("province_chinese") or city.get("province")}


class DeviceEnvironment:
    def __init__(self, cache, catalog, ip_database):
        self.cache = cache
        self.catalog = catalog
        self.ip_database = ip_database

    def resolve_ip(self, address):
        address = public_ip(address)
        if address and ipaddress.ip_address(address).version == 4 and self.ip_database:
            guess = self.ip_database.lookup(address)
            if guess["status"] == "estimated" and guess.get("city"):
                result = self.catalog.resolve(guess["city"], guess.get("region"))
                if result["status"] == "found":
                    return result["candidates"]
        return []

    def snapshot(self, query):
        if not isinstance(query, dict) or not all(
                isinstance(v, str) and 0 < len(v.strip()) <= 64 for v in query.values()):
            raise web.HTTPBadRequest()
        keys = set(query)
        mode = "manual"
        if keys == {"client_ip"}:
            mode = "ip"
            matches = self.resolve_ip(query["client_ip"])
        elif keys == {"city_id"} and re.fullmatch(r"[1-9][0-9]{0,9}", query["city_id"]):
            match = self.cache.cities.get(query["city_id"])
            matches = [match] if match else []
        elif keys in ({"city"}, {"city", "province"}):
            matches = self.catalog.resolve(query["city"], query.get("province"))["candidates"]
        else:
            raise web.HTTPBadRequest()

        data = {"location_status": "unknown", "location_source": mode, "candidates": [],
                "city": None, "weather": None, "air_quality": None}
        if not matches:
            return data
        if len(matches) > 1:
            data.update(location_status="ambiguous", candidates=[display_city(c) for c in matches[:8]])
            return data
        city = display_city(matches[0])
        if city["id"] not in self.cache.cities:
            return data
        components = self.cache.city_components(city["id"])
        weather, air = components["weather"], components["air_quality"]
        conditions = weather["values"] or {}
        particles = air["values"] or {}
        code = conditions.get("weather_code")
        data.update(location_status="found", city=city,
                    weather={"status": weather["status"], "valid_at": weather.get("valid_at"),
                             "model": weather.get("model"), "weather_code": code,
                             "description": WEATHER.get(code), "temperature_c": conditions.get("temperature_2m"),
                             "humidity_pct": conditions.get("relative_humidity_2m")},
                    air_quality={"status": air["status"], "valid_at": air.get("valid_at"),
                                 "model": air.get("model"), "model_estimate": True, "pm25": particles.get("pm2_5"),
                                 "pm10": particles.get("pm10"), "unit": "μg/m³"})
        return data

    async def handle(self, request):
        self.cache.metrics["requests"] += 1
        try:
            query = await request.json()
        except (ValueError, UnicodeError):
            raise web.HTTPBadRequest()
        return web.json_response(self.snapshot(query), headers={"Cache-Control": "no-store"})


def register_device_environment(app, cache, catalog_path, database_path):
    catalog = CityCatalog(catalog_path)
    database = IpDatabase(database_path) if database_path else None
    adapter = DeviceEnvironment(cache, catalog, database)
    app.router.add_post("/internal/device-environment", adapter.handle)

    if database:
        async def close_database(_app):
            database.close()
        app.on_cleanup.append(close_database)
