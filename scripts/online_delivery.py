"""Authorized online P4 delivery with a private, resumable journal.

Use one session for a batch and one journal directory per physical device.
No credential or administrator token is accepted on the command line.
"""

import argparse
import contextlib
import fcntl
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import stat
import sys
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

from provision_device import ProvisioningError, unique_object, validate_manifest


class DeliveryError(ProvisioningError):
    pass


class BackendFailure(DeliveryError):
    def __init__(self, status, code):
        super().__init__("Delivery backend rejected the request (HTTP %d); resume the same journal" % status)
        self.status = status
        self.code = code


def origin(value):
    validate_manifest(dict(version=1, hardware_id="020000000001", device_id="validation",
                           credential="A" * 43, ota_url=value + "/xiaozhi/ota/", control_origin=value))
    host = urlsplit(value).hostname
    if host == "localhost" or host.endswith((".localhost", ".local")):
        raise DeliveryError("A trusted public HTTPS origin is required")
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        return value
    if not address.is_global:
        raise DeliveryError("A trusted public HTTPS origin is required")
    return value


def private_directory(path):
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    mode = path.lstat().st_mode
    if not stat.S_ISDIR(mode) or stat.S_IMODE(mode) & 0o077:
        raise DeliveryError("Delivery directories must be private real directories (mode 700)")


def read_private(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, "rb") as stream:
        meta = os.fstat(stream.fileno())
        if not stat.S_ISREG(meta.st_mode) or stat.S_IMODE(meta.st_mode) & 0o077 or meta.st_size > 65536:
            raise DeliveryError("Delivery state must be a private small regular file (mode 600)")
        return json.loads(stream.read(), object_pairs_hook=unique_object)


def save_private(path, value):
    """Durability precedes every externally visible mutation, including USB writes."""
    private_directory(path.parent)
    raw = value if isinstance(value, bytes) else json.dumps(value, separators=(",", ":")).encode()
    descriptor, temporary = tempfile.mkstemp(prefix=".delivery-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


@contextlib.contextmanager
def journal_lock(directory):
    private_directory(directory)
    descriptor = os.open(directory / ".lock", os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise DeliveryError("Another process is using this delivery journal") from None
        yield
    finally:
        os.close(descriptor)


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class Backend:
    def __init__(self, public_origin, token):
        self.origin = origin(public_origin)
        self.token = token
        self.http = build_opener(NoRedirects())  # Normal certificate/hostname verification stays enabled.

    def request(self, method, path, body=None, operation=None):
        headers = {"Authorization": "Bearer " + self.token, "Accept": "application/json"}
        if operation is not None:
            headers["Idempotency-Key"] = operation
        encoded = None
        if body is not None:
            encoded = json.dumps(body, separators=(",", ":")).encode()
            headers["Content-Type"] = "application/json"
        request = Request(self.origin + "/xiaozhi/device-delivery/v1/" + path,
                          data=encoded, headers=headers, method=method)
        try:
            with self.http.open(request, timeout=30) as response:
                raw = response.read(16385)
                if response.status != 200 or len(raw) > 16384:
                    raise DeliveryError("Invalid delivery response")
                result = json.loads(raw, object_pairs_hook=unique_object)
        except HTTPError as failure:
            # Do not echo server bodies, request URLs, headers or raw transport exceptions.
            code = None
            try:
                with failure:
                    raw = failure.read(16385)
                value = json.loads(raw, object_pairs_hook=unique_object) if len(raw) <= 16384 else None
                if (isinstance(value, dict) and type(value.get("version")) is int and value["version"] == 1
                        and isinstance(value.get("code"), str) and re.fullmatch(r"[A-Z_]{1,64}", value["code"])):
                    code = value["code"]
            except (OSError, ValueError, UnicodeError, ProvisioningError):
                pass
            raise BackendFailure(failure.code, code) from None
        except (URLError, TimeoutError, OSError):
            raise DeliveryError("Delivery connection failed; resume the same journal") from None
        if (not isinstance(result, dict) or set(result) != {"version", "code", "data"}
                or type(result["version"]) is not int or result["version"] != 1
                or result["code"] != "OK" or not isinstance(result["data"], dict)):
            raise DeliveryError("Invalid delivery response")
        return result["data"]

    def create(self, state):
        return self.request("POST", "operations", dict(version=1, hardware_id=state["hardware"]["hardware_id"],
                            network_hardware_id=state["hardware"]["network_hardware_id"],
                            board_type=state["artifacts"]["board_type"]), state["operation_id"])

    def get(self, state):
        return self.request("GET", "operations/" + state["operation_id"])

    def lookup(self, state):
        # This protected read authenticates the live tool grant without registering a device.
        # An ingress HTML 404 is not proof that the backend authorized the request.
        try:
            return self.get(state)
        except BackendFailure as failure:
            if failure.status == 404 and failure.code == "DELIVERY_NOT_FOUND":
                return None
            raise

    def complete(self, state):
        return self.request("POST", "operations/" + state["operation_id"] + "/complete",
                            dict(version=1, write_operation_id=state["write_operation_id"]))


def validate_operation(result, state, identity=False):
    expected = {"operation_id": state["operation_id"],
                "hardware_id": state["hardware"]["hardware_id"],
                "network_hardware_id": state["hardware"]["network_hardware_id"],
                "board_type": state["artifacts"]["board_type"]}
    if any(result.get(key) != value for key, value in expected.items()):
        raise DeliveryError("Backend operation does not match this physical device")
    if result.get("status") not in ("pending", "delivered"):
        raise DeliveryError("Invalid delivery status")
    device = result.get("device_id")
    if not isinstance(device, str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", device):
        raise DeliveryError("Invalid stable device ID")
    for field in ("device_verified_at_ms", "completed_at_ms"):
        timestamp = result.get(field)
        if timestamp is not None and (type(timestamp) is not int or not 0 <= timestamp <= 9007199254740991):
            raise DeliveryError("Invalid delivery timestamp")
    if "identity" in state and device != state["identity"]["device_id"]:
        raise DeliveryError("Backend changed the stable device ID")
    if result["status"] == "delivered" and (result.get("device_verified_at_ms") is None
                                            or result.get("completed_at_ms") is None):
        raise DeliveryError("Delivered status lacks verification evidence")
    if identity:
        manifest = validate_manifest(result.get("identity"))
        if manifest["hardware_id"] != expected["hardware_id"] or manifest["device_id"] != device:
            raise DeliveryError("Issued identity does not match this physical device")
        if manifest["ota_url"] != state["origin"] + "/xiaozhi/ota/" or manifest["control_origin"]:
            raise DeliveryError("Issued identity changed the trusted backend")
    return result


def validate_hardware(report, state, expected_device):
    fields = {"version", "mode", "hardware_id", "network_hardware_id", "network_connected",
              "device_id", "firmware_elf_sha256"}
    if (not isinstance(report, dict) or set(report) != fields or type(report["version"]) is not int
            or report["version"] != 1 or report["mode"] != "device_hardware"
            or report["network_connected"] is not True
            or report["device_id"] != expected_device
            or report["hardware_id"] != state["baseline"]["hardware_id"]
            or report["firmware_elf_sha256"] != state["artifacts"]["product_elf_sha256"]
            or not isinstance(report["network_hardware_id"], str)
            or not re.fullmatch(r"[0-9a-f]{12}", report["network_hardware_id"])
            or report["network_hardware_id"] == report["hardware_id"]):
        raise DeliveryError("Running product image or physical identities do not match the delivery journal")
    if "hardware" in state and any(report[key] != state["hardware"][key]
                                  for key in ("hardware_id", "network_hardware_id")):
        raise DeliveryError("Connected hardware changed; refusing to issue or write credentials")


def deliver(directory, backend, hardware, artifacts, progress=print):
    """Hardware and HTTP adapters allow faults at the real I/O boundaries to be tested."""
    with journal_lock(directory):
        path = directory / "journal.json"
        if path.exists():
            state = read_private(path)
            if (state.get("version") != 1 or state.get("origin") != backend.origin
                    or state.get("artifacts") != artifacts
                    or state.get("phase") not in {"prepared", "identifying", "registering", "writing", "written", "verifying", "delivered"}
                    or any(not isinstance(state.get(key), str) or not re.fullmatch(r"[0-9a-f]{32}", state[key])
                           for key in ("operation_id", "write_operation_id"))):
                raise DeliveryError("Journal, backend or artifacts changed; keep the existing delivery state")
        else:
            state = dict(version=1, origin=backend.origin, artifacts=artifacts, phase="prepared",
                         operation_id=secrets.token_hex(16), write_operation_id=secrets.token_hex(16))
            save_private(path, state)

        def checkpoint(phase, **values):
            state.update(phase=phase, **values)
            save_private(path, state)
            progress("Delivery: " + phase)

        # Reject expired/revoked grants and unavailable APIs before opening the physical chip.
        operation = backend.lookup(state)
        if operation is not None:
            if "hardware" not in state:
                raise DeliveryError("Backend operation already exists; recover its original delivery journal")
            validate_operation(operation, state)
        elif state["phase"] not in ("prepared", "identifying", "registering"):
            raise DeliveryError("Existing backend operation is missing; retain this journal for recovery")

        # Every invocation reconnects to the physical chip; never trust a saved serial port name.
        if "baseline" not in state:
            baseline = hardware.backup(directory / "backup")
            checkpoint("identifying", baseline=baseline)
        else:
            hardware.verify_baseline(state["baseline"])

        if state["phase"] == "identifying":
            # Intent is already durable. An interrupted application write is safely repeated.
            hardware.flash("product", state["baseline"])
            report = hardware.report()
            validate_hardware(report, state, None)
            checkpoint("registering", hardware=report)

        if state["phase"] == "registering":
            result = validate_operation(backend.create(state), state, identity=True)
            checkpoint("writing", identity=result["identity"])

        if state["phase"] in ("writing", "written", "verifying", "delivered"):
            validate_manifest(state.get("identity"))
            validate_hardware(state.get("hardware"), state, None)
        if state["phase"] in ("written", "verifying", "delivered") and state.get("write_receipt") != dict(
                version=1, operation_id=state["write_operation_id"], result="stored"):
            raise DeliveryError("Journal lacks a committed USB write receipt")

        if state["phase"] == "writing":
            # A lost USB reply repeats the same identity and operation; it never rotates credentials.
            hardware.flash("maintenance", state["baseline"])
            receipt = hardware.write(state["identity"], state["write_operation_id"])
            if receipt != dict(version=1, operation_id=state["write_operation_id"], result="stored"):
                raise DeliveryError("USB credential write lacks a matching committed/read-back receipt")
            checkpoint("written", write_receipt=receipt)

        if state["phase"] in ("written", "verifying"):
            checkpoint("verifying")
            hardware.flash("product", state["baseline"])
            report = hardware.report()
            validate_hardware(report, state, state["identity"]["device_id"])
            deadline = time.monotonic() + 120
            while True:
                result = validate_operation(backend.get(state), state)
                if result.get("device_verified_at_ms") is not None:
                    break
                if time.monotonic() >= deadline:
                    raise DeliveryError("Device verification is pending; resume the same journal")
                time.sleep(2)
            result = validate_operation(backend.complete(state), state)
            if result["status"] != "delivered":
                raise DeliveryError("Backend has not enabled admission")
            checkpoint("delivered", completion=result)
        elif state["phase"] == "delivered":
            # A completed journal never flashes or enables admission again on its own.
            hardware.reset()
            validate_hardware(hardware.report(), state, state["identity"]["device_id"])
            result = validate_operation(backend.get(state), state)
            if result["status"] != "delivered":
                raise DeliveryError("Backend delivery status changed")
        if state["phase"] != "delivered":
            raise DeliveryError("Delivery is incomplete; retain the same journal")
        return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    auth = commands.add_parser("authorize", help="Exchange an existing operator login for a one-hour tool session")
    auth.add_argument("--origin", required=True)
    auth.add_argument("--admin-token-file", type=Path, required=True, help="Existing login JSON containing token, mode 600")
    auth.add_argument("--session", type=Path, required=True)
    run = commands.add_parser("deliver", help="Start or resume one physical device, retaining the same journal")
    run.add_argument("--session", type=Path, required=True)
    run.add_argument("--journal", type=Path, required=True)
    run.add_argument("--port", required=True)
    run.add_argument("--product-build", type=Path, required=True)
    run.add_argument("--maintenance-build", type=Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    try:
        if args.command == "authorize":
            public = origin(args.origin)
            token = read_private(args.admin_token_file).get("token")
            if not isinstance(token, str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,4096}", token):
                raise DeliveryError("Invalid private operator login file")
            result = Backend(public, token).request("POST", "authorize", {"version": 1})
            if (not isinstance(result.get("tool_token"), str)
                    or not re.fullmatch(r"[A-Za-z0-9_-]{43}", result["tool_token"])
                    or type(result.get("expires_at_ms")) is not int):
                raise DeliveryError("Invalid tool authorization response")
            save_private(args.session, dict(version=1, origin=public, **result))
            print("Authorized: private tool session saved; reuse it for the batch")
        else:
            from delivery_hardware import P4Hardware, build_artifacts
            session = read_private(args.session)
            if session.get("version") != 1 or not isinstance(session.get("tool_token"), str):
                raise DeliveryError("Invalid private tool session")
            artifacts = build_artifacts(args.product_build, args.maintenance_build)
            device = P4Hardware(args.port, args.product_build, args.maintenance_build,
                                artifacts, args.journal)
            deliver(args.journal, Backend(session["origin"], session["tool_token"]), device, artifacts)
            print("Delivered: independent credential written and verified; customer binding remains available on the device")
        return 0
    except ProvisioningError as failure:
        print(str(failure), file=sys.stderr)
    except KeyboardInterrupt:
        print("Interrupted: retain and resume this same journal", file=sys.stderr)
    except Exception:
        print("Delivery stopped: retain the private journal and resume it; no new operation is needed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
