#!/usr/bin/env python3
"""Isolated feasibility gateway with persistent per-city weather/AQ results.

This loopback-only test service does not implement the product device identity
or firmware API. All positive values come from the real local model engine.
"""
import argparse
import asyncio
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import logging
from logging.handlers import RotatingFileHandler
import math
from pathlib import Path
import sqlite3
import time

import aiohttp
from aiohttp import web


KINDS = {
    "weather": {"domain": "ncep_gfs013", "path": "/v1/forecast", "selector": {"models": "gfs013"},
                "units": {"temperature_2m": "°C", "relative_humidity_2m": "%", "weather_code": "wmo code"}},
    "air_quality": {"domain": "cams_global", "path": "/v1/air-quality", "selector": {"domains": "cams_global"},
                    "units": {"pm2_5": "μg/m³", "pm10": "μg/m³"}},
}


class SourceUnavailable(Exception):
    """A shared metadata outage; retry at the model level, not per city."""


def iso(stamp):
    return datetime.fromtimestamp(stamp, timezone.utc).isoformat()


class Gateway:
    def __init__(self, args):
        self.args = args
        cities = json.loads(args.cities.read_text())
        self.cities = {str(c["id"]): c for c in cities["cities"]}
        if "changsha" not in self.cities:
            # Observer compatibility: canonical GeoNames ID remains available too.
            match = [c for c in self.cities.values() if c.get("geoname_id") == 1815577]
            if match:
                self.cities["changsha"] = match[0]
        self.entries, self.errors, self.pending, self.accessed = {}, {}, set(), set()
        self.metadata, self.meta_errors, self.meta_locks = {}, {}, {k: asyncio.Lock() for k in KINDS}
        self.queue = asyncio.Queue(maxsize=2 * len(self.cities))
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="weather43-sqlite")
        args.data.mkdir(parents=True, exist_ok=True)
        self.database = args.data / "results.sqlite3"
        self.db = sqlite3.connect(self.database, check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("CREATE TABLE IF NOT EXISTS results(city TEXT,kind TEXT,payload TEXT,PRIMARY KEY(city,kind))")
        for city, kind, payload in self.db.execute("SELECT city,kind,payload FROM results"):
            if city in self.cities and kind in KINDS:
                entry = json.loads(payload)
                entry["next_check_at"] = 0
                entry["restored_unchecked"] = True
                self.entries[(city, kind)] = entry
                self.accessed.add(city)
        self.metrics = {"requests": 0, "scheduled": 0, "coalesced": 0, "queue_full": 0,
                        "engine_calls": 0, "engine_failures": 0, "metadata_calls": 0,
                        "metadata_revalidations": 0, "persisted_updates": 0, "refresh_failures": 0,
                        "restored_entries": len(self.entries), "source_changes": 0}
        self.started, self.workers, self.scheduler = time.time(), [], None
        self.events_path = args.data / "events.jsonl"
        self.logger = logging.getLogger("weather43.cache.events")
        self.logger.setLevel(logging.INFO)
        self.logger.propagate = False
        self.log_handler = RotatingFileHandler(self.events_path, maxBytes=10485760,
                                               backupCount=5, encoding="utf-8")
        self.log_handler.setFormatter(logging.Formatter("%(message)s"))
        self.logger.addHandler(self.log_handler)
        for key, entry in self.entries.items():
            self.snapshot_entry(key, entry, "restored_at_startup")

    def snapshot_entry(self, key, entry, origin):
        if key[0] != "1815577":
            return
        folder = self.args.data / "publication-snapshots"
        folder.mkdir(exist_ok=True)
        path = folder / (key[0] + "-" + key[1] + "-" + str(entry["metadata"]["run_at"]) + ".json")
        if not path.exists():
            with path.open("x", encoding="utf-8") as f:
                json.dump({"captured_at": iso(time.time()), "origin": origin, "city_id": key[0],
                           "kind": key[1], "entry": entry}, f, ensure_ascii=False, indent=2)

    def event(self, event, **fields):
        self.logger.info(json.dumps({"at": iso(time.time()), "event": event, **fields}, ensure_ascii=False))

    def schedule(self, city_id, kind, background=False):
        key = (city_id, kind)
        if key in self.pending:
            if not background:
                self.metrics["coalesced"] += 1
            return
        if self.queue.full():
            self.metrics["queue_full"] += 1
            return
        self.pending.add(key)
        self.queue.put_nowait(key)
        self.metrics["scheduled"] += 1

    async def get_metadata(self, kind, force=False):
        async with self.meta_locks[kind]:
            failure = self.meta_errors.get(kind)
            if failure and time.time() - failure["at"] < self.args.source_backoff:
                raise SourceUnavailable("metadata_backoff")
            cached = self.metadata.get(kind)
            if cached and not force and time.time() - cached["checked_at"] < self.args.metadata_ttl:
                return cached
            self.metrics["metadata_calls"] += 1
            try:
                return await self.read_metadata(kind, cached)
            except asyncio.CancelledError:
                raise
            except Exception as error:
                self.meta_errors[kind] = {"at": time.time(), "code": type(error).__name__}
                raise SourceUnavailable("metadata_unavailable") from None

    async def read_metadata(self, kind, cached):
        async with self.client.get(self.args.source + "/data/" + KINDS[kind]["domain"] + "/static/meta.json",
                                   timeout=aiohttp.ClientTimeout(total=self.args.source_timeout)) as response:
            response.raise_for_status()
            body = await response.read()
            if len(body) > 262144:
                raise ValueError("metadata_size")
            value = json.loads(body)
        for field in ("last_run_initialisation_time", "last_run_availability_time", "data_end_time"):
            if not isinstance(value.get(field), (int, float)):
                raise ValueError("invalid_metadata")
        new = {"checked_at": time.time(), "run_at": value["last_run_initialisation_time"],
               "published_at": value["last_run_availability_time"], "raw": value,
               "sha256": hashlib.sha256(body).hexdigest()}
        if cached and new["run_at"] != cached["run_at"]:
            self.metrics["source_changes"] += 1
            self.event("source_changed", kind=kind, old_run=cached["run_at"], new_run=new["run_at"])
        self.metadata[kind] = new
        self.meta_errors.pop(kind, None)
        return new

    def persist(self, key, entry):
        with self.db:
            self.db.execute("INSERT INTO results(city,kind,payload) VALUES(?,?,?) "
                            "ON CONFLICT(city,kind) DO UPDATE SET payload=excluded.payload",
                            (key[0], key[1], json.dumps(entry, ensure_ascii=False)))

    async def save(self, key, entry):
        await asyncio.get_running_loop().run_in_executor(self.executor, self.persist, key, entry)
        self.entries[key] = entry
        self.errors.pop(key, None)
        self.metrics["persisted_updates"] += 1

    async def refresh(self, key):
        city_id, kind = key
        old = self.entries.get(key)
        metadata = await self.get_metadata(kind)
        now = time.time()
        # Metadata validation can renew a check deadline, never model or validity time.
        if old and old["metadata"]["run_at"] == metadata["run_at"] and old["hourly"]["time"][-1] > now + 7200:
            renewed = dict(old, metadata=metadata, next_check_at=now + self.args.ttl, restored_unchecked=False)
            await self.save(key, renewed)
            self.metrics["metadata_revalidations"] += 1
            return
        # Allow the model engine's remote catalog and block cache to catch up.
        if old is None or metadata["run_at"] != old["metadata"]["run_at"]:
            not_before = metadata["published_at"] + self.args.update_grace
            if now < not_before:
                raise ValueError("source_update_settling")
        city = self.cities[city_id]
        params = {"latitude": city["latitude"], "longitude": city["longitude"],
                  "hourly": ",".join(KINDS[kind]["units"]), "forecast_hours": 48,
                  "timeformat": "unixtime", "timezone": "GMT", "elevation": "nan", "cell_selection": "nearest",
                  **KINDS[kind]["selector"]}
        self.metrics["engine_calls"] += 1
        try:
            async with self.client.get(self.args.engine + KINDS[kind]["path"], params=params,
                                       timeout=aiohttp.ClientTimeout(total=self.args.engine_timeout)) as response:
                response.raise_for_status()
                body = await response.read()
                if len(body) > 1048576:
                    raise ValueError("engine_response_size")
                result = json.loads(body)
            hourly, units = result["hourly"], result["hourly_units"]
            times = hourly["time"]
            if not times or not any(abs(t - time.time()) < 3600 for t in times):
                raise ValueError("invalid_validity")
            for field, unit in KINDS[kind]["units"].items():
                values = hourly[field]
                if (units.get(field) != unit or len(values) != len(times) or
                        any(isinstance(v, bool) or not isinstance(v, (float, int)) or not math.isfinite(v) for v in values)):
                    raise ValueError("missing_field_or_unit")
                if field == "relative_humidity_2m" and any(not 0 <= v <= 100 for v in values):
                    raise ValueError("invalid_humidity")
                if field.startswith("pm") and any(v < 0 for v in values):
                    raise ValueError("invalid_pm")
            after = await self.get_metadata(kind, force=True)
            if after["run_at"] != metadata["run_at"]:
                raise ValueError("source_changed_during_read")
            entry = {"hourly": hourly, "units": units, "metadata": metadata,
                     "model_grid": {"latitude": result.get("latitude"), "longitude": result.get("longitude")},
                     "fetched_at": time.time(), "next_check_at": time.time() + self.args.ttl,
                     "payload_sha256": hashlib.sha256(body).hexdigest(),
                     "model": KINDS[kind]["domain"], "query": params}
            if city_id == "1815577":
                entry["raw_engine_body"] = body.decode("utf-8")
            await self.save(key, entry)
            self.snapshot_entry(key, entry, "fresh_engine_read")
            self.event("result_promoted", city=city_id, kind=kind, run_at=metadata["run_at"],
                       published_at=metadata["published_at"], valid_start=times[0], payload_sha256=entry["payload_sha256"])
        except Exception:
            self.metrics["engine_failures"] += 1
            raise

    async def worker(self):
        while True:
            key = await self.queue.get()
            try:
                await self.refresh(key)
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                previous = self.errors.get(key, {})
                failures = previous.get("failures", 0) + 1
                self.errors[key] = {"at": time.time(), "code": type(exc).__name__, "detail": str(exc)[:180],
                                    "failures": failures, "retry_at": time.time() + min(60, 2 ** min(failures, 5))}
                self.metrics["refresh_failures"] += 1
                self.event("refresh_failed", city=key[0], kind=key[1], error=type(exc).__name__)
            finally:
                self.pending.discard(key)
                self.queue.task_done()

    async def background(self):
        while True:
            for city_id in list(self.accessed):
                for kind in KINDS:
                    key = (city_id, kind)
                    entry = self.entries.get(key)
                    due = entry is None or time.time() >= entry["next_check_at"]
                    retry = self.errors.get(key, {}).get("retry_at", 0)
                    if due and time.time() >= retry:
                        self.schedule(city_id, kind, background=True)
            await asyncio.sleep(1)

    def component(self, key):
        entry = self.entries.get(key)
        error = self.errors.get(key)
        if not entry:
            return {"status": "unavailable" if error else "loading", "values": None,
                    "valid_at": None, "source_error": error["code"] if error else None}
        current = int(time.time()) // 3600 * 3600
        times = entry["hourly"]["time"]
        if current not in times:
            return {"status": "unavailable", "values": None, "valid_at": None, "source_error": "validity_exhausted"}
        index = times.index(current)
        values = {field: entry["hourly"][field][index] for field in KINDS[key[1]]["units"]}
        too_old = time.time() - entry["metadata"]["published_at"] > (
            self.args.weather_max_age if key[1] == "weather" else self.args.aq_max_age)
        latest = self.metadata.get(key[1])
        superseded = bool(latest and latest["run_at"] > entry["metadata"]["run_at"])
        stale = bool(error) or key[1] in self.meta_errors or too_old or superseded or entry.get("restored_unchecked", False)
        return {"status": "stale" if stale else "fresh", "values": values,
                "units": KINDS[key[1]]["units"], "valid_at": iso(current),
                "model": entry["model"], "model_run_at": iso(entry["metadata"]["run_at"]),
                "source_published_at": iso(entry["metadata"]["published_at"]),
                "source_checked_at": iso(entry["metadata"]["checked_at"]),
                "fetched_at": iso(entry["fetched_at"]), "payload_sha256": entry["payload_sha256"],
                "source_error": error["code"] if error else None, "model_estimate": True,
                "refresh_due": time.time() >= entry["next_check_at"],
                "refreshing": key in self.pending, "source_has_newer_run": superseded}

    async def environment(self, request):
        self.metrics["requests"] += 1
        city_id = request.query.get("city_id", "")
        if city_id not in self.cities:
            return web.json_response({"error": "unknown_city", "values": None}, status=404)
        canonical = str(self.cities[city_id]["id"])
        if request.query.get("peek") != "1":
            self.accessed.add(canonical)
            for kind in KINDS:
                key = (canonical, kind)
                if key not in self.entries or time.time() >= self.entries[key]["next_check_at"]:
                    if time.time() >= self.errors.get(key, {}).get("retry_at", 0):
                        self.schedule(canonical, kind)
        data = {kind: self.component((canonical, kind)) for kind in KINDS}
        complete = all(c["values"] is not None for c in data.values())
        city = self.cities[canonical]
        public_city = {"id": canonical, "name": city.get("chinese_name") or city["name"],
                       "province": city.get("province_chinese") or city.get("province"),
                       "latitude": city["latitude"], "longitude": city["longitude"]}
        return web.json_response({"city": public_city, "components": data,
                                  "complete": complete, "fresh": all(c["status"] == "fresh" for c in data.values()),
                                  "served_at": iso(time.time())}, status=200 if complete else 202,
                                 headers={"Cache-Control": "no-store"})

    async def health(self, request):
        return web.json_response({"status": "ok", "started_at": iso(self.started),
                                  "cache_entries": len(self.entries), "pending": len(self.pending),
                                  "metrics": self.metrics, "scope": "isolated_feasibility_service",
                                  "model_source_errors": self.meta_errors,
                                  "recent_errors": [{"city": k[0], "kind": k[1], **v}
                                                    for k, v in list(self.errors.items())[-10:]]})

    async def start(self, app):
        self.client = aiohttp.ClientSession(connector=aiohttp.TCPConnector(limit=32))
        self.workers = [asyncio.create_task(self.worker()) for _ in range(self.args.workers)]
        self.scheduler = asyncio.create_task(self.background())
        if self.args.prewarm:
            self.accessed.update(str(c["id"]) for c in list(self.cities.values())[:self.args.prewarm])
        self.event("started", restored_entries=len(self.entries), prewarm=self.args.prewarm)

    async def stop(self, app):
        for task in [self.scheduler, *self.workers]:
            task.cancel()
        await asyncio.gather(self.scheduler, *self.workers, return_exceptions=True)
        await self.client.close()
        self.executor.shutdown(wait=True)
        self.db.close()
        self.logger.removeHandler(self.log_handler)
        self.log_handler.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cities", required=True, type=Path)
    p.add_argument("--data", required=True, type=Path)
    p.add_argument("--engine", default="http://openmeteo:8080")
    p.add_argument("--source", required=True, help="Trial bridge S3 relay origin")
    p.add_argument("--port", type=int, default=8081)
    p.add_argument("--listen-backlog", type=int, default=4096)
    p.add_argument("--workers", type=int, default=8)
    p.add_argument("--ttl", type=int, default=300)
    p.add_argument("--metadata-ttl", type=int, default=60)
    p.add_argument("--source-timeout", type=float, default=30)
    p.add_argument("--source-backoff", type=float, default=5)
    p.add_argument("--engine-timeout", type=float, default=120)
    p.add_argument("--update-grace", type=int, default=600)
    p.add_argument("--weather-max-age", type=int, default=32400)
    p.add_argument("--aq-max-age", type=int, default=64800)
    p.add_argument("--prewarm", type=int, default=1)
    args = p.parse_args()
    service = Gateway(args)
    app = web.Application(client_max_size=4096)
    app.router.add_get("/v1/environment", service.environment)
    app.router.add_get("/health", service.health)
    app.on_startup.append(service.start)
    app.on_cleanup.append(service.stop)
    web.run_app(app, host="0.0.0.0", port=args.port, backlog=args.listen_backlog, access_log=None, print=None)


if __name__ == "__main__":
    main()
