"""Resume an administrator-authorized credential recovery for a known physical P4.

The previous delivery/recovery journal supplies the existing hardware mapping.
Keep both journals private; administrator credentials are read from a file only.
"""

import argparse
import os
from pathlib import Path
import re
import secrets
import sys
import time

from online_delivery import (Backend, DeliveryError, journal_lock, read_private,
                             save_private, validate_hardware, validate_operation)
from provision_device import ProvisioningError, validate_manifest


class RecoveryBackend(Backend):
    def create(self, state):
        return self.request("POST", "recoveries", dict(version=1, device_id=state["device_id"],
            previous_operation_id=state["previous_operation_id"]), state["operation_id"])

    def get(self, state):
        return self.request("GET", "recoveries/" + state["operation_id"])

    def complete(self, state):
        return self.request("POST", "recoveries/" + state["operation_id"] + "/complete",
                            dict(version=1, write_operation_id=state["write_operation_id"]))


def predecessor(previous, backend, artifacts):
    if (not isinstance(previous, dict) or type(previous.get("version")) is not int
            or previous["version"] != 1 or previous.get("phase") != "delivered"
            or previous.get("origin") != backend.origin
            or not isinstance(previous.get("operation_id"), str)
            or not re.fullmatch(r"[0-9a-f]{32}", previous["operation_id"])
            or not isinstance(previous.get("artifacts"), dict)
            or previous["artifacts"].get("board_type") != artifacts["board_type"]):
        raise DeliveryError("Use the completed private journal for this device and trusted backend")
    identity = validate_manifest(previous.get("identity"))
    hardware = previous.get("hardware")
    if (not isinstance(hardware, dict) or hardware.get("hardware_id") != identity["hardware_id"]
            or not isinstance(hardware.get("network_hardware_id"), str)
            or not re.fullmatch(r"[0-9a-f]{12}", hardware["network_hardware_id"])
            or hardware["hardware_id"] == hardware["network_hardware_id"]
            or identity["ota_url"] != backend.origin + "/xiaozhi/ota/" or identity["control_origin"]):
        raise DeliveryError("Previous journal does not identify this physical device and backend")
    return dict(previous_operation_id=previous["operation_id"], device_id=identity["device_id"],
                hardware={key: hardware[key] for key in ("hardware_id", "network_hardware_id")})


def checked(result, state, include_identity=False):
    result = validate_operation(result, state, identity=include_identity)
    if result["device_id"] != state["device_id"]:
        raise DeliveryError("Recovery changed the existing device record")
    if include_identity and "identity" in state and result["identity"] != state["identity"]:
        raise DeliveryError("Recovery replay changed the pinned credential")
    return result


def recover(directory, backend, hardware, artifacts, previous, progress=print):
    expected = predecessor(previous, backend, artifacts)
    with journal_lock(directory):
        path = directory / "journal.json"
        if path.exists():
            state = read_private(path)
            if (type(state.get("version")) is not int or state["version"] != 1
                    or state.get("workflow") != "credential_recovery"
                    or state.get("origin") != backend.origin or state.get("artifacts") != artifacts
                    or any(state.get(key) != value for key, value in expected.items())
                    or state.get("phase") not in {"prepared", "rotating", "writing", "written", "verifying", "delivered"}
                    or any(not isinstance(state.get(key), str) or not re.fullmatch(r"[0-9a-f]{32}", state[key])
                           for key in ("operation_id", "write_operation_id"))
                    or state["operation_id"] == state["previous_operation_id"]):
                raise DeliveryError("Recovery journal, predecessor or pinned artifacts changed")
        else:
            state = dict(version=1, workflow="credential_recovery", origin=backend.origin,
                artifacts=artifacts, phase="prepared", operation_id=secrets.token_hex(16),
                write_operation_id=secrets.token_hex(16), **expected)
            save_private(path, state)

        def checkpoint(phase, **values):
            state.update(phase=phase, **values)
            save_private(path, state)
            progress("Recovery: " + phase)

        # Only an authenticated administrator can receive this protected 404.
        # A newer credential generation returns 409 before any physical I/O.
        current = backend.lookup(state)
        if current is not None:
            checked(current, state)
        elif state["phase"] not in ("prepared", "rotating"):
            raise DeliveryError("Current recovery record is missing; do not reissue automatically")

        if "baseline" not in state:
            baseline = hardware.backup(directory / "backup")
            if baseline.get("hardware_id") != state["hardware"]["hardware_id"]:
                raise DeliveryError("Connected chip differs from the original device; no credential was rotated")
            checkpoint("rotating", baseline=baseline)
        else:
            if state["baseline"].get("hardware_id") != state["hardware"]["hardware_id"]:
                raise DeliveryError("Recovery backup identifies a different physical chip")
            hardware.verify_baseline(state["baseline"])

        if state["phase"] == "rotating":
            result = checked(backend.create(state), state, include_identity=True)
            checkpoint("writing", identity=result["identity"])

        if state["phase"] in ("writing", "written", "verifying", "delivered"):
            manifest = validate_manifest(state.get("identity"))
            if (manifest["device_id"] != state["device_id"]
                    or manifest["hardware_id"] != state["hardware"]["hardware_id"]
                    or manifest["ota_url"] != backend.origin + "/xiaozhi/ota/" or manifest["control_origin"]):
                raise DeliveryError("Recovery identity differs from the original hardware or trusted backend")
        if state["phase"] in ("written", "verifying", "delivered"):
            if state.get("write_receipt") != dict(version=1, operation_id=state["write_operation_id"], result="stored"):
                raise DeliveryError("Recovery lacks a matching committed/read-back write receipt")

        if state["phase"] == "writing":
            checked(backend.get(state), state)
            hardware.flash("maintenance", state["baseline"])
            # Recheck after the potentially long application write, before sending a secret.
            checked(backend.get(state), state)
            receipt = hardware.recover(state["identity"], state["write_operation_id"])
            if receipt != dict(version=1, operation_id=state["write_operation_id"], result="stored"):
                raise DeliveryError("Recovery lacks a matching committed/read-back write receipt")
            checkpoint("written", write_receipt=receipt)

        if state["phase"] in ("written", "verifying"):
            checked(backend.get(state), state)
            checkpoint("verifying")
            hardware.flash("product", state["baseline"])
            report = hardware.report()
            validate_hardware(report, state, state["device_id"])
            deadline = time.monotonic() + 120
            while checked(backend.get(state), state).get("device_verified_at_ms") is None:
                if time.monotonic() >= deadline:
                    raise DeliveryError("New credential device verification is pending; resume this journal")
                time.sleep(2)
            result = checked(backend.complete(state), state)
            if result["status"] != "delivered":
                raise DeliveryError("Recovery writeback has not completed")
            checkpoint("delivered", completion=result)
        elif state["phase"] == "delivered":
            # A completed journal never rewrites an identity or changes admission.
            hardware.reset()
            validate_hardware(hardware.report(), state, state["device_id"])
            if checked(backend.get(state), state)["status"] != "delivered":
                raise DeliveryError("Recovery status changed")
        if state["phase"] != "delivered":
            raise DeliveryError("Recovery is incomplete; retain this journal")
        return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin", required=True)
    parser.add_argument("--admin-token-file", type=Path, required=True)
    parser.add_argument("--previous-journal", type=Path, required=True)
    parser.add_argument("--journal", type=Path, required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--product-build", type=Path, required=True)
    parser.add_argument("--maintenance-build", type=Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    try:
        from delivery_hardware import P4Hardware, build_artifacts
        token = read_private(args.admin_token_file).get("token")
        if not isinstance(token, str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,4096}", token):
            raise DeliveryError("Invalid private administrator login file")
        backend = RecoveryBackend(args.origin, token)
        previous = read_private(args.previous_journal / "journal.json")
        artifacts = build_artifacts(args.product_build, args.maintenance_build)
        device = P4Hardware(args.port, args.product_build, args.maintenance_build, artifacts, args.journal)
        recover(args.journal, backend, device, artifacts, previous)
        print("Recovered: same device and hardware verified; ownership and local use state were not changed")
        return 0
    except ProvisioningError as failure:
        print(str(failure), file=sys.stderr)
    except KeyboardInterrupt:
        print("Interrupted: retain and resume the same recovery journal", file=sys.stderr)
    except Exception:
        print("Recovery stopped: retain the private journal and pinned builds; do not start another operation", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
