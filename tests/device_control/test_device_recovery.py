import copy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import online_delivery as delivery
import device_recovery as recovery

PUBLIC = "https://speaker.example:18443"
ARTIFACTS = dict(board_type="test-p4", product_elf_sha256="e" * 64)


class Backend:
    origin = PUBLIC

    def __init__(self):
        self.operations = {}
        self.fail = None
        self.superseded = False

    def fault(self, action):
        if self.fail == action:
            self.fail = None
            raise delivery.DeliveryError("lost boundary response")

    def lookup(self, state):
        self.fault("authorization")
        return self.get(state) if state["operation_id"] in self.operations else None

    def create(self, state):
        key = state["operation_id"]
        if key not in self.operations:
            self.operations[key] = dict(operation_id=key, device_id=state["device_id"],
                hardware_id=state["hardware"]["hardware_id"],
                network_hardware_id=state["hardware"]["network_hardware_id"],
                board_type="test-p4", status="pending", device_verified_at_ms=None,
                completed_at_ms=None, identity=dict(version=1, device_id=state["device_id"],
                    hardware_id=state["hardware"]["hardware_id"], credential="B" * 43,
                    ota_url=PUBLIC + "/xiaozhi/ota/", control_origin=""))
        self.fault("create")
        return copy.deepcopy(self.operations[key])

    def get(self, state):
        if self.superseded:
            raise delivery.BackendFailure(409, "STATE_CHANGED")
        result = copy.deepcopy(self.operations[state["operation_id"]])
        result.pop("identity")
        return result

    def complete(self, state):
        value = self.operations[state["operation_id"]]
        if value["device_verified_at_ms"] is None:
            raise delivery.DeliveryError("device has not checked its new credential")
        value.update(status="delivered", completed_at_ms=200)
        self.fault("complete")
        return self.get(state)


class Hardware:
    def __init__(self, backend):
        self.backend = backend
        self.hardware = "020000000001"
        self.identity = None
        self.flashes = []
        self.fail = None
        self.backups = 0

    def backup(self, directory):
        self.backups += 1
        return dict(hardware_id=self.hardware)

    def verify_baseline(self, baseline):
        if baseline["hardware_id"] != self.hardware:
            raise delivery.DeliveryError("different physical chip")

    def flash(self, kind, baseline):
        self.verify_baseline(baseline)
        self.flashes.append(kind)

    def recover(self, identity, operation):
        self.identity = copy.deepcopy(identity)
        if self.fail == "write":
            self.fail = None
            raise delivery.DeliveryError("lost USB receipt")
        return dict(version=1, operation_id=operation, result="stored")

    def report(self):
        for value in self.backend.operations.values():
            if value["identity"] == self.identity:
                value["device_verified_at_ms"] = 100
        return dict(version=1, mode="device_hardware", hardware_id=self.hardware,
            network_hardware_id="020000000101", network_connected=True,
            device_id=self.identity["device_id"], firmware_elf_sha256="e" * 64)

    def reset(self):
        pass


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "recovery"
        self.backend = Backend()
        self.hardware = Hardware(self.backend)
        self.previous = dict(version=1, origin=PUBLIC, phase="delivered", operation_id="a" * 32,
            artifacts=ARTIFACTS, hardware=dict(hardware_id="020000000001", network_hardware_id="020000000101"),
            identity=dict(version=1, device_id="device-original", hardware_id="020000000001",
                credential="A" * 43, ota_url=PUBLIC + "/xiaozhi/ota/", control_origin=""))
        self.progress = []

    def run_recovery(self):
        return recovery.recover(self.directory, self.backend, self.hardware, ARTIFACTS,
                                self.previous, self.progress.append)

    def test_recovery_keeps_device_and_uses_a_new_credential_then_records_real_verification(self):
        state = self.run_recovery()
        self.assertEqual(state["phase"], "delivered")
        self.assertEqual(state["identity"]["device_id"], "device-original")
        self.assertEqual(state["previous_operation_id"], "a" * 32)
        self.assertEqual(self.hardware.identity["credential"], "B" * 43)
        self.assertEqual(self.hardware.flashes, ["maintenance", "product"])
        self.assertNotIn("B" * 43, "\n".join(self.progress))

    def test_unauthorized_administrator_stops_before_any_device_io(self):
        self.backend.fail = "authorization"
        with self.assertRaises(delivery.DeliveryError):
            self.run_recovery()
        self.assertEqual(self.hardware.backups, 0)
        self.assertEqual(self.hardware.flashes, [])
        self.assertEqual(self.backend.operations, {})

    def test_wrong_physical_chip_does_not_rotate_or_write_any_credential(self):
        self.hardware.hardware = "020000000002"
        with self.assertRaises(delivery.DeliveryError):
            self.run_recovery()
        self.assertEqual(self.hardware.flashes, [])
        self.assertEqual(self.backend.operations, {})

    def test_lost_creation_response_reuses_the_durable_recovery_operation(self):
        self.backend.fail = "create"
        with self.assertRaises(delivery.DeliveryError):
            self.run_recovery()
        first = delivery.read_private(self.directory / "journal.json")
        self.assertEqual(first["phase"], "rotating")
        self.assertEqual(self.hardware.flashes, [])
        final = self.run_recovery()
        self.assertEqual(final["operation_id"], first["operation_id"])
        self.assertEqual(len(self.backend.operations), 1)
        self.assertEqual(final["phase"], "delivered")

    def test_lost_usb_receipt_repeats_the_same_credential_and_write_operation(self):
        self.hardware.fail = "write"
        with self.assertRaises(delivery.DeliveryError):
            self.run_recovery()
        first = delivery.read_private(self.directory / "journal.json")
        self.assertEqual(first["phase"], "writing")
        written = copy.deepcopy(self.hardware.identity)
        final = self.run_recovery()
        self.assertEqual(self.hardware.identity, written)
        self.assertEqual(final["write_operation_id"], first["write_operation_id"])
        self.assertEqual(final["phase"], "delivered")

    def test_lost_completion_response_resumes_without_creating_another_credential(self):
        self.backend.fail = "complete"
        with self.assertRaises(delivery.DeliveryError):
            self.run_recovery()
        first = delivery.read_private(self.directory / "journal.json")
        self.assertEqual(first["phase"], "verifying")
        final = self.run_recovery()
        self.assertEqual(final["identity"], first["identity"])
        self.assertEqual(len(self.backend.operations), 1)
        self.assertEqual(final["phase"], "delivered")

    def test_completed_recovery_never_rewrites_identity_and_newer_generation_stops_before_flash(self):
        self.run_recovery()
        writes = list(self.hardware.flashes)
        self.run_recovery()
        self.assertEqual(self.hardware.flashes, writes)
        self.backend.superseded = True
        with self.assertRaises(delivery.BackendFailure):
            self.run_recovery()
        self.assertEqual(self.hardware.flashes, writes)

    def test_changed_candidate_or_predecessor_does_not_continue_existing_recovery(self):
        self.run_recovery()
        writes = list(self.hardware.flashes)
        with self.assertRaises(delivery.DeliveryError):
            recovery.recover(self.directory, self.backend, self.hardware,
                dict(ARTIFACTS, product_elf_sha256="f" * 64), self.previous)
        self.previous["operation_id"] = "f" * 32
        with self.assertRaises(delivery.DeliveryError):
            self.run_recovery()
        self.assertEqual(self.hardware.flashes, writes)


if __name__ == "__main__":
    unittest.main()
