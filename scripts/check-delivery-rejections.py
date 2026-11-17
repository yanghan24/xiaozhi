"""Check public delivery rejection envelopes without credentials or device registration."""

import argparse
import json
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class WithoutContentType(Request):
    def has_header(self, name):
        # urllib otherwise injects form Content-Type for a POST with data.
        return name.lower() == "content-type" or super().has_header(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin", default="https://bridge.hncig.cn:18443")
    args = parser.parse_args()
    target = urlsplit(args.origin)
    if (target.scheme != "https" or not target.hostname or target.username is not None
            or target.path or target.query or target.fragment):
        parser.error("Use the trusted public HTTPS origin without a path or credentials")
    cases = {
        "authorize": ({"version": 1}, "OPERATOR_AUTH_REQUIRED"),
        "operations": ({"version": 1, "hardware_id": "020000000001", "network_hardware_id": "020000000101",
                        "board_type": "rejection-check"}, "TOOL_AUTH_REQUIRED"),
        "device-check": ({"version": 1, "hardware_id": "020000000001", "network_hardware_id": "020000000101"},
                         "INVALID_DEVICE_CREDENTIAL"),
        "operations/" + "0" * 32 + "/complete": ({"version": 1, "write_operation_id": "0" * 32}, "TOOL_AUTH_REQUIRED"),
    }
    client = build_opener(NoRedirect())
    failures = 0
    count = 0
    for path, (body, unauthenticated) in cases.items():
        for media in ("application/json", "application/json; charset=UTF-8", None, "text/plain",
                      "application/json; charset=unsupported-charset"):
            headers = {"Accept": "application/json"}
            if media is not None:
                headers["Content-Type"] = media
            request = (WithoutContentType if media is None else Request)(
                args.origin + "/xiaozhi/device-delivery/v1/" + path, method="POST",
                headers=headers, data=json.dumps(body, separators=(",", ":")).encode())
            expected = 401 if media in ("application/json", "application/json; charset=UTF-8") else 400
            try:
                try:
                    response = client.open(request, timeout=20)
                except HTTPError as error:
                    response = error
                with response:
                    raw = response.read(8193)
                    value = json.loads(raw) if len(raw) <= 8192 else {}
                    passed = (response.code == expected and isinstance(value, dict)
                              and type(value.get("version")) is int and value["version"] == 1
                              and value.get("code") == (unauthenticated if expected == 401 else "INVALID_REQUEST")
                              and response.headers.get("Cache-Control") == "no-store")
                    status = response.code
            except Exception:
                # Do not print response bodies or raw transport exceptions.
                passed, status = False, None
            count += 1
            failures += not passed
            print(json.dumps(dict(path=path, content_type=media, http=status, expected_http=expected, passed=passed)))
    print(json.dumps(dict(checks=count, failures=failures, credentials_used=False, devices_registered=False)))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
