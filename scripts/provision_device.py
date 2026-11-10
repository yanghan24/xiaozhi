"""Validate a private per-device manifest, or deliver it to the offline USB bench image."""
import argparse
import json
import re
import secrets
import stat
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit


class ProvisioningError(Exception):
    pass


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ProvisioningError("Duplicate manifest field")
        result[key] = value
    return result


def validate_manifest(value):
    fields = {"version", "hardware_id", "device_id", "credential", "ota_url", "control_origin"}
    if not isinstance(value, dict) or set(value) != fields or type(value["version"]) is not int or value["version"] != 1:
        raise ProvisioningError("Invalid manifest schema")
    if any(not isinstance(value[key], str) for key in fields - {"version"}):
        raise ProvisioningError("Invalid manifest field type")
    for key, pattern in [("hardware_id", r"[0-9a-f]{12}"),
                         ("device_id", r"[A-Za-z0-9_.:-]{1,128}"),
                         ("credential", r"[A-Za-z0-9_-]{43,128}")]:
        if not re.fullmatch(pattern, value[key]):
            raise ProvisioningError("Invalid device identity format")
    for key in ("ota_url", "control_origin"):
        text = value[key]
        if key == "control_origin" and not text:
            continue
        try:
            parsed = urlsplit(text)
            host = parsed.hostname or ""
            authority = parsed.netloc
            port = parsed.port
            if (len(text) > 2048 or not re.fullmatch(r"[\x21-\x7e]+", text) or "\\" in text
                    or parsed.scheme.lower() != "https" or not host or host.endswith(".")
                    or len(host) > 253 or host.startswith(".") or ".." in host
                    or not re.fullmatch(r"[A-Za-z0-9.-]+", host)
                    or re.search(r"[@?#%\[\]]", authority) or parsed.username is not None
                    or parsed.query or parsed.fragment or "?" in text or "#" in text
                    or (":" in authority and not re.fullmatch(r"[^:]+:[0-9]{1,5}", authority))
                    or (port is not None and not 1 <= port <= 65535)
                    or parsed.path != ("/xiaozhi/ota/" if key == "ota_url" else "")):
                raise ProvisioningError("Invalid trusted HTTPS location")
        except ValueError:
            raise ProvisioningError("Invalid trusted HTTPS location") from None
    encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":")).encode()
    if len(encoded) > 2048:
        raise ProvisioningError("Manifest is too large")
    return value


def load_manifest(path):
    try:
        metadata = path.stat()
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_size > 8192:
            raise ProvisioningError("Manifest must be a small regular file")
        if sys.platform != "win32" and stat.S_IMODE(metadata.st_mode) & 0o077:
            raise ProvisioningError("Private manifest requires mode 600")
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (OSError, UnicodeError, ValueError):
        raise ProvisioningError("Cannot read a valid private manifest") from None
    return validate_manifest(value)


def read_frame(port, deadline):
    while time.monotonic() < deadline:
        raw = port.read_until(b"\n", 4096)
        if not raw.endswith(b"\n"):
            continue
        try:
            frame = json.loads(raw, object_pairs_hook=unique_object)
        except (ValueError, UnicodeError, ProvisioningError):
            continue  # Ignore boot logs, but never echo serial input.
        if isinstance(frame, dict) and type(frame.get("version")) is int and frame["version"] == 1:
            return frame
    raise ProvisioningError("Timed out waiting for the maintenance image")


def provision(port, manifest, repair=False):
    deadline = time.monotonic() + 15
    while True:
        ready = read_frame(port, deadline)
        if ready.get("mode") == "device_provisioning":
            break
    if ready.get("hardware_id") != manifest["hardware_id"]:
        raise ProvisioningError("Connected hardware does not match the private manifest")
    operation_id = secrets.token_hex(16)
    request = {"version": 1, "operation_id": operation_id,
               "action": "repair" if repair else "write", "identity": manifest}
    port.write(json.dumps(request, ensure_ascii=True, separators=(",", ":")).encode() + b"\n")
    port.flush()
    deadline = time.monotonic() + 15
    while True:
        result = read_frame(port, deadline)
        if result.get("operation_id") == operation_id and "result" in result:
            break
    if result.get("result") != "stored":
        raise ProvisioningError("Device rejected the manifest or could not commit it")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True, help="Private per-device JSON, mode 600")
    parser.add_argument("--port", help="USB serial device running the bench image")
    parser.add_argument("--write", action="store_true", help="Write after checking the physical hardware ID")
    parser.add_argument("--repair", action="store_true",
                        help="Explicitly restore a corrupt credential record; requires its intact device ID anchor")
    args = parser.parse_args()
    try:
        manifest = load_manifest(args.config)
        if args.repair and not args.write:
            raise ProvisioningError("--repair requires --write and a controlled maintenance connection")
        if not args.write:
            print("PASS: private manifest validated; no device opened")
            return 0
        if not args.port:
            raise ProvisioningError("--write requires --port")
        try:
            import serial
        except ImportError:
            raise ProvisioningError("Use the ESP-IDF Python environment with pyserial") from None
        # Avoid reset/bootloader transitions while opening the maintenance firmware.
        with serial.Serial(port=None, baudrate=115200, timeout=0.5, write_timeout=5) as port:
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            provision(port, manifest, repair=args.repair)
        print("PASS: device identity committed and read back; install the product image next")
        return 0
    except ProvisioningError as error:
        print(str(error), file=sys.stderr)
    except Exception:
        print("Provisioning transport failed; verify the stored identity before retrying", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
