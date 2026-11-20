import json
from pathlib import Path
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from cache_service import Gateway
from device_environment import DeviceEnvironment
from location_candidates import CityCatalog


class DeviceEnvironmentTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name)
        cities = [
            {"id": "1", "name": "Changsha", "chinese_name": "长沙", "aliases": ["长沙", "Changsha"],
             "province_code": "11", "province": "Hunan Sheng", "province_chinese": "湖南省"},
            {"id": "2", "name": "Test A", "aliases": ["同名城"], "province_code": "11", "province": "湖南省"},
            {"id": "3", "name": "Test B", "aliases": ["同名城"], "province_code": "22", "province": "河北省"},
        ]
        catalog_path = path / "cities.json"
        catalog_path.write_text(json.dumps({"cities": cities}), encoding="utf-8")
        self.cache = Gateway(SimpleNamespace(cities=catalog_path, data=path / "data", weather_max_age=3600,
                                             aq_max_age=3600))
        self.addCleanup(self.cache.db.close)
        self.addCleanup(self.cache.executor.shutdown)
        self.addCleanup(self.cache.log_handler.close)
        self.addCleanup(self.cache.logger.removeHandler, self.cache.log_handler)
        self.ip = Mock()
        self.ip.lookup.return_value = {"status": "estimated", "city": "Changsha", "region": "Hunan"}
        adapter = DeviceEnvironment(self.cache, CityCatalog(catalog_path), self.ip)
        app = web.Application()
        app.router.add_post("/internal/device-environment", adapter.handle)
        self.client = TestClient(TestServer(app))
        await self.client.start_server()
        self.addAsyncCleanup(self.client.close)

    async def query(self, payload):
        response = await self.client.post("/internal/device-environment", json=payload)
        self.assertEqual(200, response.status)
        self.assertEqual("no-store", response.headers["Cache-Control"])
        return await response.json()

    async def test_shared_city_coalesces_pending_requests_and_missing_data_is_null(self):
        by_name = await self.query({"city": "长沙市"})
        by_id = await self.query({"city_id": "1"})
        self.assertEqual(by_name, by_id)
        self.assertEqual("长沙", by_name["city"]["name"])
        self.assertEqual("loading", by_name["weather"]["status"])
        self.assertIsNone(by_name["weather"]["temperature_c"])
        self.assertIsNone(by_name["air_quality"]["pm10"])
        self.assertEqual(2, self.cache.metrics["scheduled"])
        self.assertEqual(2, self.cache.metrics["coalesced"])
        self.ip.lookup.assert_not_called()

    async def test_ambiguity_is_resolved_only_by_explicit_city_or_province(self):
        # The pilot cache only serves one of the two places sharing this name.
        del self.cache.cities["2"]
        result = await self.query({"city": "同名城"})
        self.assertEqual(["2", "3"], [entry["id"] for entry in result["candidates"]])
        self.assertEqual("ambiguous", result["location_status"])
        self.assertIsNone(result["weather"])
        chosen = await self.query({"city": "同名城", "province": "河北"})
        self.assertEqual("3", chosen["city"]["id"])
        outside = await self.query({"city": "同名城", "province": "湖南"})
        self.assertEqual("unknown", outside["location_status"])
        self.assertEqual("unknown", (await self.query({"city_id": "8"}))["location_status"])
        self.ip.lookup.assert_not_called()

    async def test_auto_location_uses_only_public_ipv4_with_a_unique_catalog_match(self):
        for peer in ("::1", "2001:4860:4860::8888", "10.0.0.8", "bad"):
            self.assertEqual("unknown", (await self.query({"client_ip": peer}))["location_status"])
        self.ip.lookup.assert_not_called()
        result = await self.query({"client_ip": "::ffff:8.8.8.8"})
        self.ip.lookup.assert_called_once_with("8.8.8.8")
        self.assertEqual(("ip", "1"), (result["location_source"], result["city"]["id"]))
        self.ip.lookup.return_value = {"status": "estimated", "city": "同名城"}
        self.assertEqual("unknown", (await self.query({"client_ip": "8.8.8.8"}))["location_status"])

    async def test_components_keep_their_own_validity_model_and_stale_state(self):
        now = int(time.time())
        hour = now // 3600 * 3600
        for kind, model, values in (
                ("weather", "ncep_gfs013", {"temperature_2m": [-2.5], "relative_humidity_2m": [0],
                                            "weather_code": [71]}),
                ("air_quality", "cams_global", {"pm2_5": [0.0], "pm10": [15.2]})):
            self.cache.entries["1", kind] = {
                "model": model, "metadata": {"published_at": now, "run_at": hour, "checked_at": now},
                "hourly": {"time": [hour], **values}, "next_check_at": now + 300,
                "fetched_at": now, "payload_sha256": "fixture"}
        self.cache.meta_errors["air_quality"] = "unreachable"
        result = await self.query({"city_id": "1"})
        self.assertEqual(("fresh", "小雪", -2.5, 0), tuple(result["weather"][k] for k in
                         ("status", "description", "temperature_c", "humidity_pct")))
        self.assertEqual(("stale", "cams_global", True, 0, 15.2, "μg/m³"),
                         tuple(result["air_quality"][k] for k in ("status", "model", "model_estimate",
                                                                  "pm25", "pm10", "unit")))
        self.assertIsNotNone(result["air_quality"]["valid_at"])
        self.cache.entries["1", "air_quality"]["hourly"]["time"] = [hour - 3600]
        result = await self.query({"city_id": "1"})
        self.assertEqual("unavailable", result["air_quality"]["status"])
        self.assertIsNone(result["air_quality"]["pm25"])
        self.assertIsNone(result["air_quality"]["valid_at"])
        self.assertEqual("fresh", result["weather"]["status"])

    async def test_invalid_shapes_are_rejected(self):
        for body in (None, {}, {"city": ""}, {"city_id": "1&peek=1"}, {"client_ip": "8.8.8.8", "city": "长沙"}):
            response = await self.client.post("/internal/device-environment", json=body)
            self.assertEqual(400, response.status)
