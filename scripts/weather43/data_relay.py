#!/usr/bin/env python3
"""Independent raw S3 relay, connection pooling, bounded logs and scoped faults."""
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import ipaddress
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import time

import aiohttp
from aiohttp import web


async def main_async(args):
    logger = logging.getLogger("weather43.s3")
    logger.setLevel(logging.INFO)
    handler = RotatingFileHandler(args.log, maxBytes=10485760, backupCount=5, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(handler)
    network = ipaddress.ip_network(args.allowed_subnet)
    metrics = {"requests": 0, "source_requests": 0, "source_bytes": 0, "injected_failures": 0, "errors": 0}
    connector = aiohttp.TCPConnector(limit=32, limit_per_host=24, ttl_dns_cache=300, keepalive_timeout=90)
    session = aiohttp.ClientSession(connector=connector, auto_decompress=False, trust_env=False)

    async def forward(request):
        try:
            if ipaddress.ip_address(request.remote) not in network:
                return web.Response(status=403)
        except ValueError:
            return web.Response(status=403)
        if request.path == "/relay-health":
            return web.json_response({"scope": "raw_source_relay", "metrics": metrics})
        metrics["requests"] += 1
        start = time.monotonic()
        record = {"at": datetime.now(timezone.utc).isoformat(), "method": request.method,
                  "path": request.raw_path, "range": request.headers.get("Range"), "bytes": 0}
        try:
            allowed = (request.path.startswith("/data/") or
                       (request.path == "/" and (request.method == "HEAD" or
                        request.query.get("prefix", "").startswith("data/") or
                        (request.query.get("list-type") == "2" and request.query.get("delimiter") == "/" and
                         not request.query.get("prefix", "")))))
            if request.method not in ("GET", "HEAD") or not allowed or ".." in request.path:
                return web.Response(status=403)
            if request.method == "GET" and request.path.endswith(".om") and not request.headers.get("Range"):
                return web.Response(status=400)
            fault = json.loads(args.fault.read_text()) if args.fault.exists() else {}
            resource = request.path if request.path != "/" else "/" + request.query.get("prefix", "")
            domains = fault.get("domains", [])
            targeted = not domains or any("/data/" + d + "/" in resource for d in domains)
            if targeted and fault.get("offline"):
                metrics["injected_failures"] += 1
                record.update(status=503, fault="offline")
                return web.Response(status=503, text="isolated trial source outage")
            if targeted and fault.get("delay_seconds"):
                await asyncio.sleep(min(60, max(0, float(fault["delay_seconds"]))))
                record["fault"] = "slow_source"
            headers = {k: request.headers[k] for k in ("Range", "If-Match", "If-None-Match", "If-Modified-Since")
                       if k in request.headers}
            headers["Accept-Encoding"] = "identity"
            metrics["source_requests"] += 1
            async with session.request(request.method, "https://openmeteo.s3.amazonaws.com" + request.raw_path,
                                       headers=headers, proxy=args.proxy,
                                       timeout=aiohttp.ClientTimeout(total=90)) as upstream:
                length = int(upstream.headers.get("Content-Length", "0"))
                if request.method == "GET" and length > 67108864:
                    record.update(status=502, error="source_size_limit")
                    return web.Response(status=502)
                record.update(status=upstream.status, etag=upstream.headers.get("ETag"),
                              last_modified=upstream.headers.get("Last-Modified"),
                              content_range=upstream.headers.get("Content-Range"))
                response = web.StreamResponse(status=upstream.status, headers={
                    k: upstream.headers[k] for k in ("Content-Type", "Content-Length", "Content-Range",
                                                     "Accept-Ranges", "ETag", "Last-Modified", "Content-Encoding")
                    if k in upstream.headers})
                await response.prepare(request)
                digest = hashlib.sha256()
                if request.method != "HEAD":
                    async for block in upstream.content.iter_chunked(65536):
                        record["bytes"] += len(block)
                        metrics["source_bytes"] += len(block)
                        if record["bytes"] > 67108864:
                            raise ValueError("source_size_limit")
                        digest.update(block)
                        await response.write(block)
                record["sha256"] = digest.hexdigest()
                await response.write_eof()
                return response
        except Exception as exc:
            record["error"] = type(exc).__name__
            metrics["errors"] += 1
            return web.Response(status=502, text="source unavailable")
        finally:
            record["elapsed_s"] = time.monotonic() - start
            logger.info(json.dumps(record))

    app = web.Application(client_max_size=4096)
    app.router.add_route("*", "/{tail:.*}", forward)
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    await web.TCPSite(runner, args.bind, args.port).start()
    try:
        await asyncio.Event().wait()
    finally:
        await session.close()
        await runner.cleanup()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bind", required=True)
    p.add_argument("--allowed-subnet", required=True)
    p.add_argument("--port", type=int, default=18085)
    p.add_argument("--proxy", default="http://127.0.0.1:7890")
    p.add_argument("--fault", required=True, type=Path)
    p.add_argument("--log", required=True, type=Path)
    args = p.parse_args()
    address = ipaddress.ip_address(args.bind)
    if not address.is_private or address.is_unspecified:
        p.error("Only the dedicated trial bridge may be bound")
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
