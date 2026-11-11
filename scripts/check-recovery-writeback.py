#!/usr/bin/env python3
"""Check old/new device credentials on Ubuntu after physical recovery completes.

Private input and raw responses stay on disk with owner-only permissions.
This does not create a recovery, write a device, or change device ownership.
"""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import platform
import re
import stat
import sys
import tempfile
from urllib.error import HTTPError
from urllib.request import HTTPRedirectHandler, Request, build_opener


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate private input field")
        result[key] = value
    return result


def load_material(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, "rb") as stream:
        metadata = os.fstat(stream.fileno())
        if (not stat.S_ISREG(metadata.st_mode) or stat.S_IMODE(metadata.st_mode) & 0o077
                or metadata.st_size > 16384):
            raise ValueError("Private input must be a small regular file with mode 600")
        value = json.loads(stream.read(), object_pairs_hook=unique_object)
    if (not isinstance(value, dict) or type(value.get("version")) is not int or value["version"] != 1
            or value.get("origin") != "https://bridge.hncig.cn:18443"
            or value.get("physical_recovery_completed") is not True
            or not isinstance(value.get("device_alias"), str)
            or not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", value["device_alias"])):
        raise ValueError("Use material exported after the physical recovery completed")
    for key, pattern in (("device_id", r"[A-Za-z0-9_.:-]{1,128}"),
                         ("hardware_id", r"[0-9a-f]{12}"),
                         ("network_hardware_id", r"[0-9a-f]{12}"),
                         ("old_credential", r"[A-Za-z0-9_-]{43,128}"),
                         ("new_credential", r"[A-Za-z0-9_-]{43,128}")):
        if not isinstance(value.get(key), str) or not re.fullmatch(pattern, value[key]):
            raise ValueError("Invalid private verification material")
    verified, completed = value.get("device_verified_at_ms"), value.get("completed_at_ms")
    if (type(verified) is not int or type(completed) is not int or not 0 < verified <= completed
            or value["hardware_id"] == value["network_hardware_id"]
            or value["old_credential"] == value["new_credential"]):
        raise ValueError("Physical recovery verification or credential rotation is missing")
    return value


def save_private(path, value):
    descriptor, temporary = tempfile.mkstemp(prefix=".recovery-check-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def device_check(http, material, credential):
    body = json.dumps({"version": 1, "hardware_id": material["hardware_id"],
                       "network_hardware_id": material["network_hardware_id"]}).encode()
    request = Request(material["origin"] + "/xiaozhi/device-delivery/v1/device-check",
                      data=body, method="POST", headers={
                          "Authorization": "Bearer " + credential,
                          "Device-Id": material["device_id"],
                          "Content-Type": "application/json", "Accept": "application/json"})
    try:
        response = http.open(request, timeout=30)
    except HTTPError as failure:
        response = failure
    with response:
        raw = response.read(16385)
        if len(raw) > 16384:
            raise ValueError("Oversized verification response")
        parsed = json.loads(raw, object_pairs_hook=unique_object)
        if not isinstance(parsed, dict):
            raise ValueError("Invalid verification response")
        headers_ok = ("no-store" in response.headers.get("Cache-Control", "").lower()
                      and response.headers.get("Content-Type", "").split(";", 1)[0].lower()
                      == "application/json")
        return response.code, parsed, headers_ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--material", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if platform.freedesktop_os_release().get("ID") != "ubuntu":
        raise ValueError("Run this protocol check on Ubuntu; no requests were sent")
    os.umask(0o077)
    material = load_material(args.material)
    args.output.mkdir(mode=0o700, parents=True, exist_ok=True)
    metadata = args.output.lstat()
    if not stat.S_ISDIR(metadata.st_mode) or stat.S_IMODE(metadata.st_mode) & 0o077:
        raise ValueError("Output must be a private real directory with mode 700")
    http = build_opener(NoRedirects())
    # Device-side verification and completion must already have happened. These
    # probes must never supply the first verification used by physical recovery.
    old_status, old, old_headers = device_check(http, material, material["old_credential"])
    save_private(args.output / "old-response.json", old)
    new_status, new, new_headers = device_check(http, material, material["new_credential"])
    save_private(args.output / "new-response.json", new)
    data = new.get("data") if isinstance(new.get("data"), dict) else {}
    checks = {
        "old_credential_rejected": old_status == 401 and type(old.get("version")) is int
            and old["version"] == 1 and old.get("code") == "INVALID_DEVICE_CREDENTIAL",
        "new_credential_verified": new_status == 200 and type(new.get("version")) is int
            and new["version"] == 1 and new.get("code") == "OK"
            and data.get("verified") is True and data.get("status") == "delivered"
            and data.get("device_id") == material["device_id"],
        "response_headers_valid": old_headers and new_headers,
    }
    summary = {"observed_at_utc": datetime.now(timezone.utc).isoformat(),
               "device_alias": material["device_alias"], "client_os": "ubuntu",
               "physical_recovery_completed_before_probes": True,
               "old_http_status": old_status, "new_http_status": new_status, "checks": checks}
    save_private(args.output / "summary.json", summary)
    print(json.dumps(summary))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as failure:
        print(str(failure), file=sys.stderr)
    except Exception:
        print("Verification stopped; inspect private prerequisites and connectivity", file=sys.stderr)
    raise SystemExit(1)
