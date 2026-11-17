#!/usr/bin/env python3
"""Temporary trial-only S3 byte relay through an existing HTTP proxy.

No forecast API, cache, credentials, generated data or default values. The
Open-Meteo engine still computes the response from the real model files.
Bind only to the dedicated trial Docker bridge gateway, never a public NIC.
"""
import argparse
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
import threading
import time
import urllib.error
import urllib.parse
import urllib.request


class Relay(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 128
    slots = threading.BoundedSemaphore(32)

    def process_request(self, request, client_address):
        self.slots.acquire()
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_HEAD(self):
        self.forward()

    def do_GET(self):
        self.forward()

    def forward(self):
        started = time.monotonic()
        record = {"at": datetime.now(timezone.utc).isoformat(), "method": self.command,
                  "path": self.path, "range": self.headers.get("Range"), "bytes": 0}
        try:
            parts = urllib.parse.urlsplit(self.path)
            query = urllib.parse.parse_qs(parts.query)
            allowed = (parts.path.startswith("/data/") or
                       (parts.path == "/" and (self.command == "HEAD" or
                        query.get("prefix", [""])[0].startswith("data/") or
                        (query.get("list-type") == ["2"] and query.get("delimiter") == ["/"] and
                         not query.get("prefix", [""])[0]))))
            if parts.scheme or parts.netloc or not allowed or ".." in urllib.parse.unquote(parts.path):
                self.send_error(403)
                record["status"] = 403
                return
            if parts.path.endswith(".om") and self.command == "GET" and not self.headers.get("Range"):
                self.send_error(400, "Bounded Range required for model files")
                record["status"] = 400
                return
            headers = {name: self.headers[name] for name in ("Range", "If-Match", "If-None-Match", "If-Modified-Since")
                       if self.headers.get(name)}
            request = urllib.request.Request("https://openmeteo.s3.amazonaws.com" + self.path,
                                             headers=headers, method=self.command)
            try:
                response = self.server.client.open(request, timeout=25)
            except urllib.error.HTTPError as exc:
                response = exc
            with response:
                length = int(response.headers.get("Content-Length", "0"))
                if self.command == "GET" and length > 67108864:
                    self.send_error(502, "Trial response size limit")
                    record["status"] = 502
                    return
                self.send_response(response.status)
                for name in ("Content-Type", "Content-Length", "Content-Range", "Accept-Ranges", "ETag", "Last-Modified"):
                    if response.headers.get(name):
                        self.send_header(name, response.headers[name])
                self.send_header("Connection", "close")
                self.end_headers()
                record["status"] = response.status
                if self.command == "GET":
                    while True:
                        data = response.read(65536)
                        if not data:
                            break
                        record["bytes"] += len(data)
                        if record["bytes"] > 67108864:
                            raise ValueError("Trial response size limit")
                        self.wfile.write(data)
            self.close_connection = True
        except (OSError, ValueError) as exc:
            record.update(error=type(exc).__name__)
            self.close_connection = True
        finally:
            record["elapsed_s"] = time.monotonic() - started
            print(json.dumps(record), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", required=True)
    parser.add_argument("--port", type=int, default=18085)
    parser.add_argument("--proxy", default="http://127.0.0.1:17890")
    args = parser.parse_args()
    address = ipaddress.ip_address(args.bind)
    if not address.is_private or address.is_unspecified or address.is_loopback:
        parser.error("Bind must be the trial-only Docker bridge gateway address")
    proxy = urllib.parse.urlsplit(args.proxy)
    if proxy.scheme != "http" or not proxy.hostname or proxy.username or proxy.password:
        parser.error("An existing HTTP proxy without credentials is required")
    server = Relay((args.bind, args.port), Handler)
    server.client = urllib.request.build_opener(urllib.request.ProxyHandler({"https": args.proxy}))
    print(json.dumps({"event": "relay_ready", "bind": args.bind, "port": args.port,
                      "source": "https://openmeteo.s3.amazonaws.com", "scope": "temporary_trial"}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
