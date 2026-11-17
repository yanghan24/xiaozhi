#!/usr/bin/env python3
"""Probe anonymous model metadata and bounded OM-file ranges, never hosted APIs.

Runs on any OS for network preparation. Only an actual Ubuntu run is Ubuntu
evidence. A successful probe is not a self-hosted forecast or capacity result.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import socket
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET


ROOT = "https://openmeteo.s3.amazonaws.com/"
NS = {"s": "http://s3.amazonaws.com/doc/2006-03-01/"}
SOURCES = (("ncep_gfs013", "temperature_2m"), ("cams_global", "pm2_5"))


def now():
    return datetime.now(timezone.utc).isoformat()


def read(client, url, *, byte_range=False):
    """Keep all attempts; reject ignored Range headers before reading a big file."""
    attempts = []
    for attempt in range(1, 3):
        started = time.monotonic()
        record = {"attempt": attempt, "at": now(), "url": url}
        try:
            headers = {"User-Agent": "xiaozhi-issue43-source-probe/1"}
            if byte_range:
                headers["Range"] = "bytes=0-4095"
            request = urllib.request.Request(url, headers=headers)
            with client.open(request, timeout=15) as response:
                expected = 206 if byte_range else 200
                record.update(http=response.status, etag=response.headers.get("ETag"),
                              last_modified=response.headers.get("Last-Modified"),
                              content_range=response.headers.get("Content-Range"))
                if response.status != expected:
                    raise ValueError("Expected HTTP %s" % expected)
                if byte_range and not (record["content_range"] or "").startswith("bytes 0-"):
                    raise ValueError("Missing or invalid Content-Range")
                limit = 4096 if byte_range else 262144
                body = response.read(limit + 1)
                if len(body) > limit:
                    raise ValueError("Response exceeded the bounded read limit")
                record.update(bytes=len(body), sha256=hashlib.sha256(body).hexdigest(),
                              elapsed_s=round(time.monotonic() - started, 4), ok=True)
                attempts.append(record)
                return body, attempts
        except (OSError, ValueError, urllib.error.URLError) as exc:
            record.update(ok=False, error=type(exc).__name__, detail=str(exc)[:240],
                          elapsed_s=round(time.monotonic() - started, 4))
            attempts.append(record)
    return None, attempts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proxy", help="Explicit unauthenticated HTTP proxy; omitted means direct")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new output path; evidence is not overwritten")
    if args.proxy:
        parsed = urllib.parse.urlsplit(args.proxy)
        if parsed.scheme != "http" or not parsed.hostname or parsed.username or parsed.password:
            parser.error("Proxy must be an HTTP URL without credentials")
    client = urllib.request.build_opener(urllib.request.ProxyHandler(
        {"http": args.proxy, "https": args.proxy} if args.proxy else {}))
    report = {"started_at": now(), "system": platform.platform(),
              "route": "explicit_proxy" if args.proxy else "direct",
              "scope": "anonymous_source_read_only", "tls_verification": True,
              "authorization_sent": False, "sources": [],
              "limitations": ["Not a local forecast response or capacity result",
                              "Range object is the first lexicographic object, possibly historical",
                              "Last-Modified is object publication time, not observation time"]}
    try:
        report["local_dns_addresses"] = sorted({x[4][0] for x in socket.getaddrinfo(
            "openmeteo.s3.amazonaws.com", 443, type=socket.SOCK_STREAM)})
    except OSError as exc:
        report["local_dns_error"] = type(exc).__name__
    for model, variable in SOURCES:
        entry = {"model": model, "variable": variable}
        metadata, entry["metadata_attempts"] = read(client, ROOT + "data/" + model + "/static/meta.json")
        if metadata is not None:
            try:
                entry["metadata"] = json.loads(metadata)
            except ValueError:
                entry["metadata_error"] = "Invalid JSON"
        query = urllib.parse.urlencode({"list-type": "2", "prefix": "data/" + model + "/" + variable + "/", "max-keys": 1})
        listing, entry["listing_attempts"] = read(client, ROOT + "?" + query)
        if listing is not None:
            try:
                root = ET.fromstring(listing)
                key = root.findtext("s:Contents/s:Key", namespaces=NS)
                if key and key.endswith(".om"):
                    _, entry["range_attempts"] = read(client, ROOT + urllib.parse.quote(key, safe="/"), byte_range=True)
                else:
                    entry["listing_error"] = "No OM object in the bounded listing"
            except ET.ParseError:
                entry["listing_error"] = "Invalid S3 XML"
        entry["ok"] = ("metadata" in entry and
                       any(x["ok"] for x in entry.get("range_attempts", [])))
        report["sources"].append(entry)
    report.update(finished_at=now(), ok=all(x["ok"] for x in report["sources"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    print(json.dumps({"ok": report["ok"], "scope": report["scope"], "output": str(args.output)}))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
