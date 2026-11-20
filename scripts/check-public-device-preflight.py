"""Read-only public ingress checks using only synthetic, unissued identities."""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from functools import partial
import hashlib
import http.client
import json
import os
from pathlib import Path
import ssl
import struct
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener, getproxies

DELIVERY = "/xiaozhi/device-delivery/v1/"
CONTROL = "/xiaozhi/device-control/v1/"
FAKE = {"Device-Id": "preflight-unissued-" + os.urandom(8).hex(), "Client-Id": "preflight", "Authorization": "Bearer " + "A" * 43}


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def http_check(case, public_origin):
    name, method, path, body, headers, expected, protocol = case
    outgoing = {"Accept": "application/json", "User-Agent": "xiaozhi-public-preflight/1", **headers}
    if isinstance(body, dict):
        body = json.dumps(body, separators=(",", ":")).encode()
        outgoing["Content-Type"] = "application/json"
    request = Request(public_origin + path, method=method, data=body, headers=outgoing)
    try:
        try:
            response = build_opener(NoRedirect()).open(request, timeout=15)
        except HTTPError as error:
            response = error
        with response:
            raw = response.read(8193)
            result = dict(name=name, http=response.code, expected_http=expected)
            passed = response.code == expected
            if protocol == "discovery":
                passed = passed and json.loads(raw) == {"device_control": {"version": 1, "base_url": public_origin + CONTROL}}
                passed = passed and response.headers.get("Cache-Control") == "no-store"
            elif protocol:
                value = json.loads(raw)
                passed = passed and isinstance(value, dict) and type(value.get("version")) is int and value["version"] == 1
                passed = passed and value == {"version": 1, "code": protocol, "message": protocol}
                passed = passed and response.headers.get("Cache-Control") == "no-store"
                passed = passed and not any(field in value for field in ("credential", "tool_token", "identity"))
                result["protocol_code"] = value.get("code")
            result["passed"] = passed
            return result
    except Exception as error:
        return dict(name=name, passed=False, error_type=type(error).__name__)


def websocket_check(public_origin, headers):
    target = urlsplit(public_origin)
    proxy = getproxies().get("https")
    context = ssl.create_default_context()
    if proxy:
        endpoint = urlsplit(proxy)
        connection = http.client.HTTPSConnection(endpoint.hostname, endpoint.port or 80, context=context, timeout=15)
        connection.set_tunnel(target.hostname, target.port)
    else:
        connection = http.client.HTTPSConnection(target.hostname, target.port, context=context, timeout=15)
    result = dict(name="WSS rejects " + ("invalid credential" if headers else "anonymous connection"))
    try:
        key = base64.b64encode(os.urandom(16)).decode()
        connection.request("GET", "/xiaozhi/v1/", headers={"Host": target.netloc, "Upgrade": "websocket", "Connection": "Upgrade", "Sec-WebSocket-Version": "13", "Sec-WebSocket-Key": key, **headers})
        response = connection.getresponse()
        result["upgrade_http"] = response.status
        expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        if response.status != 101 or response.getheader("Sec-WebSocket-Accept") != expected:
            result["passed"] = False
            return result
        first = response.fp.read(2)
        if len(first) != 2 or first[0] != 0x88 or first[1] & 0x80 or first[1] > 125:
            result["passed"] = False
            return result
        payload = response.fp.read(first[1])
        result["close_code"] = struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else None
        result["passed"] = result["close_code"] == 1008
        return result
    except Exception as error:
        return {**result, "passed": False, "error_type": type(error).__name__}
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin", default="https://bridge.hncig.cn:18443")
    parser.add_argument("--output", type=Path, help="Save the report without credentials or response bodies")
    args = parser.parse_args()
    try:
        target = urlsplit(args.origin)
        valid = (target.scheme == "https" and target.hostname and target.username is None
                 and not (target.path or target.query or target.fragment) and target.port != 0)
    except ValueError:
        valid = False
    if not valid:
        parser.error("Use the trusted public HTTPS origin without a path or credentials")
    cases = [
        ("OTA health", "GET", "/xiaozhi/ota/", None, {}, 200, False),
        ("voice HTTP health", "GET", "/xiaozhi/v1/", None, {}, 200, False),
        ("OTA public discovery", "POST", "/xiaozhi/ota/", {}, FAKE, 200, "discovery"),
        ("legacy OTA activation blocked", "POST", "/xiaozhi/ota/activate", {}, FAKE, 403, False),
        ("control state anonymous", "GET", CONTROL + "state", None, {}, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("control state unissued credential", "GET", CONTROL + "state", None, FAKE, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("maintenance anonymous", "POST", CONTROL + "maintenance", {}, {}, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("maintenance unissued credential", "POST", CONTROL + "maintenance", {}, FAKE, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("maintenance wrong method blocked", "GET", CONTROL + "maintenance", None, {}, 403, False),
        ("maintenance body over 16KB blocked", "POST", CONTROL + "maintenance", b" " * 16385, {"Content-Type": "application/json"}, 413, False),
        ("control request anonymous", "POST", CONTROL + "requests", {"version": 1}, {}, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("control ACK anonymous", "POST", CONTROL + "state-acks", {"version": 1}, {}, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("control unbind anonymous", "POST", CONTROL + "unbind", {"expected_permission_version": 1}, {}, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("control unbind unissued credential", "POST", CONTROL + "unbind", {"expected_permission_version": 1}, FAKE, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("control unbind wrong method blocked", "GET", CONTROL + "unbind", None, {}, 403, False),
        ("delivery query anonymous", "GET", DELIVERY + "operations/" + "0" * 32, None, {}, 401, "TOOL_AUTH_REQUIRED"),
        ("delivery query unissued tool", "GET", DELIVERY + "operations/" + "0" * 32, None, FAKE, 401, "TOOL_AUTH_REQUIRED"),
        ("tool authorize invalid login", "POST", DELIVERY + "authorize", {"version": 1}, FAKE, 401, "OPERATOR_AUTH_REQUIRED"),
        ("device-check unissued credential", "POST", DELIVERY + "device-check", {"version": 1, "hardware_id": "020000000001", "network_hardware_id": "020000000101"}, FAKE, 401, "INVALID_DEVICE_CREDENTIAL"),
        ("ready anonymous", "GET", "/xiaozhi/v1/ready/preflight", None, {}, 401, False),
        ("unissued audio URL", "GET", "/xiaozhi/asr-audio/" + "A" * 43 + ".wav", None, {}, 404, False),
        ("internal voice API blocked", "POST", "/xiaozhi/internal/device-voice/v1/open", {}, {}, 404, False),
        ("management config blocked", "GET", "/xiaozhi/config/server-base", None, {}, 404, False),
        ("management login blocked", "POST", "/xiaozhi/user/login", {}, {}, 404, False),
        ("unknown delivery route blocked", "GET", DELIVERY + "unknown", None, {}, 404, False),
        ("malformed operation ID blocked", "GET", DELIVERY + "operations/invalid", None, {}, 404, False),
        ("authorize wrong method blocked", "GET", DELIVERY + "authorize", None, {}, 403, False),
        ("operation query wrong method blocked", "POST", DELIVERY + "operations/" + "0" * 32, {}, {}, 403, False),
        ("delivery body over 4KB blocked", "POST", DELIVERY + "operations", b" " * 4097, {"Content-Type": "application/json"}, 413, False),
    ]
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(partial(http_check, public_origin=args.origin), cases))
    results.extend(websocket_check(args.origin, headers) for headers in ({}, FAKE))
    report = dict(observed_at=datetime.now(timezone.utc).isoformat(), origin=args.origin, tls_verification=True,
                  real_credentials_used=False, devices_registered=False, flash_accessed=False,
                  checks=len(results), failures=sum(not result["passed"] for result in results), results=results)
    encoded = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    return bool(report["failures"])


if __name__ == "__main__":
    raise SystemExit(main())
