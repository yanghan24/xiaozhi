import copy
import io
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import online_delivery as delivery

PUBLIC = "https://speaker.example:18443"
ARTIFACTS = dict(board_type="test-p4", product_elf_sha256="e" * 64)


class Backend:
    origin = PUBLIC

    def __init__(self):
        self.operations = {}
        self.create_calls = []
        self.complete_calls = []
        self.fail = None
        self.corrupt = None

    def fault(self, action):
        if self.fail == action:
            self.fail = None
            raise delivery.DeliveryError("injected lost response")

    def create(self, state):
        key = state["operation_id"]
        self.create_calls.append(key)
        if key not in self.operations:
            hardware = state["hardware"]
            device = "device-" + str(len(self.operations) + 1)
            secret = chr(65 + len(self.operations)) * 43
            self.operations[key] = dict(operation_id=key, device_id=device,
                hardware_id=hardware["hardware_id"], network_hardware_id=hardware["network_hardware_id"],
                board_type=state["artifacts"]["board_type"], status="pending", device_verified_at_ms=None,
                completed_at_ms=None, identity=dict(version=1, hardware_id=hardware["hardware_id"],
                device_id=device, credential=secret, ota_url=PUBLIC + "/xiaozhi/ota/", control_origin=""))
        self.fault("create")
        result = copy.deepcopy(self.operations[key])
        if self.corrupt:
            result.update(self.corrupt)
        return result

    def get(self, state):
        value = copy.deepcopy(self.operations[state["operation_id"]])
        value.pop("identity")
        return value

    def lookup(self, state):
        self.fault("authorization")
        return self.get(state) if state["operation_id"] in self.operations else None

    def complete(self, state):
        self.complete_calls.append(state["write_operation_id"])
        value = self.operations[state["operation_id"]]
        if value["device_verified_at_ms"] is None:
            raise delivery.DeliveryError("no real device check")
        value.update(status="delivered", completed_at_ms=200)
        self.fault("complete")
        return self.get(state)


class Hardware:
    def __init__(self, backend, number=1):
        self.backend = backend
        self.hardware = "02000000000" + str(number)
        self.network = "02000000010" + str(number)
        self.identity = None
        self.image = "original"
        self.flashes = []
        self.writes = []
        self.fail = None
        self.corrupt_network_after_write = False
        self.verify_device = True
        self.backups = 0

    def fault(self, action):
        if self.fail == action:
            self.fail = None
            raise delivery.DeliveryError("injected hardware interruption")

    def backup(self, directory):
        self.backups += 1
        return {"hardware_id": self.hardware}

    def verify_baseline(self, baseline):
        if baseline["hardware_id"] != self.hardware:
            raise delivery.DeliveryError("different physical device")

    def flash(self, kind, baseline):
        self.verify_baseline(baseline)
        self.flashes.append(kind)
        self.image = kind
        self.fault(kind + ("-after-write" if self.identity else ""))

    def report(self):
        assert self.image == "product"
        for value in self.backend.operations.values():
            if self.verify_device and self.identity == value["identity"]:
                value["device_verified_at_ms"] = 100
        return dict(version=1, mode="device_hardware", hardware_id=self.hardware,
                    network_hardware_id="020000000199" if self.identity and self.corrupt_network_after_write else self.network,
                    network_connected=True, device_id=self.identity["device_id"] if self.identity else None,
                    firmware_elf_sha256=ARTIFACTS["product_elf_sha256"])

    def write(self, manifest, operation):
        assert self.image == "maintenance"
        self.writes.append((copy.deepcopy(manifest), operation))
        if self.identity is not None and self.identity != manifest:
            raise delivery.DeliveryError("identity conflict")
        self.identity = copy.deepcopy(manifest)
        self.fault("write")
        return dict(version=1, operation_id=operation, result="stored")

    def reset(self):
        self.fault("reset")


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "device-a"
        self.backend = Backend()
        self.hardware = Hardware(self.backend)
        self.progress = []

    def run_delivery(self, directory=None, hardware=None):
        return delivery.deliver(directory or self.directory, self.backend, hardware or self.hardware,
                                ARTIFACTS, self.progress.append)

    def test_two_devices_receive_separate_identities_and_verified_delivery(self):
        a = self.run_delivery()
        other = Hardware(self.backend, 2)
        b = self.run_delivery(Path(self.temporary.name) / "device-b", other)
        self.assertEqual(a["phase"], "delivered")
        self.assertEqual(b["phase"], "delivered")
        self.assertNotEqual(a["identity"]["credential"], b["identity"]["credential"])
        self.assertNotEqual(a["identity"]["device_id"], b["identity"]["device_id"])
        self.assertEqual(self.hardware.identity, a["identity"])
        self.assertEqual(other.identity, b["identity"])
        self.assertEqual(stat.S_IMODE((self.directory / "journal.json").stat().st_mode), 0o600)
        self.assertNotIn(a["identity"]["credential"], "\n".join(self.progress))
        self.assertNotIn(a["identity"]["device_id"], "\n".join(self.progress))

    def test_authorization_failure_stops_before_opening_or_flashing_device(self):
        self.backend.fail = "authorization"
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()
        self.assertEqual(self.hardware.backups, 0)
        self.assertEqual(self.hardware.flashes, [])
        self.assertEqual(self.hardware.writes, [])
        self.assertEqual(self.backend.operations, {})
        before = delivery.read_private(self.directory / "journal.json")
        result = self.run_delivery()
        self.assertEqual(before["operation_id"], result["operation_id"])
        self.assertEqual(before["write_operation_id"], result["write_operation_id"])

    def test_revoked_authorization_stops_resume_before_another_usb_write(self):
        self.hardware.fail = "write"
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()
        flashes, writes = len(self.hardware.flashes), len(self.hardware.writes)
        self.backend.fail = "authorization"
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()
        self.assertEqual(len(self.hardware.flashes), flashes)
        self.assertEqual(len(self.hardware.writes), writes)

    def test_only_authenticated_protocol_not_found_can_pass_lookup(self):
        backend = delivery.Backend(PUBLIC, "A" * 43)
        state = {"operation_id": "0" * 32}
        for status, body, allowed in [
                (404, b'{"version":1,"code":"DELIVERY_NOT_FOUND","data":{}}', True),
                (404, b'<html>not found</html>', False),
                (401, b'{"version":1,"code":"TOOL_AUTH_REQUIRED","data":{}}', False),
                (403, b'{"version":1,"code":"OPERATOR_REQUIRED","data":{}}', False),
                (503, b'{"version":1,"code":"DELIVERY_NOT_CONFIGURED","data":{}}', False)]:
            def rejected(*args, **kwargs):
                raise HTTPError(PUBLIC, status, "test", {}, io.BytesIO(body))
            backend.http = SimpleNamespace(open=rejected)
            with self.subTest(status=status, allowed=allowed):
                if allowed:
                    self.assertIsNone(backend.lookup(state))
                else:
                    with self.assertRaises(delivery.DeliveryError):
                        backend.lookup(state)

    def test_interrupted_mutations_resume_same_operation_identity_and_usb_receipt(self):
        for boundary in ("create", "maintenance", "write", "product-after-write", "complete"):
            with self.subTest(boundary=boundary):
                self.backend = Backend()
                self.hardware = Hardware(self.backend)
                directory = Path(self.temporary.name) / boundary
                target = self.backend if boundary in ("create", "complete") else self.hardware
                target.fail = boundary
                with self.assertRaises(delivery.DeliveryError):
                    self.run_delivery(directory)
                saved = delivery.read_private(directory / "journal.json")
                self.assertNotEqual(saved["phase"], "delivered")
                result = self.run_delivery(directory)
                self.assertEqual(saved["operation_id"], result["operation_id"])
                self.assertEqual(saved["write_operation_id"], result["write_operation_id"])
                self.assertEqual(len(self.backend.operations), 1)
                self.assertEqual(len(set(self.backend.create_calls)), 1)
                for identity, operation in self.hardware.writes:
                    self.assertEqual(identity, result["identity"])
                    self.assertEqual(operation, result["write_operation_id"])

    def test_switching_physical_devices_during_resume_never_writes_a_secret(self):
        self.backend.fail = "create"
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()
        other = Hardware(self.backend, 2)
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery(hardware=other)
        self.assertEqual(other.writes, [])
        self.assertEqual(other.flashes, [])
        self.assertEqual(len(self.backend.operations), 1)

    def test_changed_c6_identity_blocks_completion(self):
        self.hardware.corrupt_network_after_write = True
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()
        self.assertEqual(self.backend.complete_calls, [])
        self.assertEqual(delivery.read_private(self.directory / "journal.json")["phase"], "verifying")

    def test_real_device_verification_is_required(self):
        self.hardware.verify_device = False
        with patch.object(delivery.time, "monotonic", side_effect=[0, 121]):
            with self.assertRaises(delivery.DeliveryError):
                self.run_delivery()
        self.assertEqual(self.backend.complete_calls, [])
        self.assertEqual(next(iter(self.backend.operations.values()))["status"], "pending")

    def test_already_completed_resume_checks_device_and_backend_without_flashing(self):
        first = self.run_delivery()
        writes, flashes = len(self.hardware.writes), len(self.hardware.flashes)
        result = self.run_delivery()
        self.assertEqual(first, result)
        self.assertEqual(writes, len(self.hardware.writes))
        self.assertEqual(flashes, len(self.hardware.flashes))
        self.assertEqual(len(self.backend.complete_calls), 1)

    def test_backend_cross_device_result_is_rejected_before_usb_write(self):
        self.backend.corrupt = {"network_hardware_id": "020000000199"}
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()
        self.assertEqual(self.hardware.writes, [])

    def test_changed_artifacts_or_lost_receipt_never_claim_delivered(self):
        self.run_delivery()
        state = delivery.read_private(self.directory / "journal.json")
        del state["write_receipt"]
        delivery.save_private(self.directory / "journal.json", state)
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()
        state["artifacts"]["product_elf_sha256"] = "f" * 64
        delivery.save_private(self.directory / "journal.json", state)
        with self.assertRaises(delivery.DeliveryError):
            self.run_delivery()

    def test_private_files_and_exclusive_journal_are_required(self):
        delivery.private_directory(self.directory)
        path = self.directory / "private.json"
        path.write_text('{"version":1}')
        path.chmod(0o644)
        with self.assertRaises(delivery.DeliveryError):
            delivery.read_private(path)
        path.chmod(0o600)
        link = self.directory / "link"
        link.symlink_to(path)
        with self.assertRaises(OSError):
            delivery.read_private(link)
        with delivery.journal_lock(self.directory):
            with self.assertRaises(delivery.DeliveryError):
                with delivery.journal_lock(self.directory):
                    pass

    def test_public_https_origin_and_strict_json_are_required(self):
        self.assertEqual(delivery.origin(PUBLIC), PUBLIC)
        for value in ("http://speaker.example", "https://127.0.0.1", "https://192.168.1.1",
                      "https://localhost", "https://speaker.example/path", "https://speaker.example?x=1"):
            with self.subTest(origin=value), self.assertRaises(delivery.ProvisioningError):
                delivery.origin(value)
        with self.assertRaises(delivery.ProvisioningError):
            json.loads('{"version":1,"version":1}', object_pairs_hook=delivery.unique_object)


if __name__ == "__main__":
    unittest.main()
