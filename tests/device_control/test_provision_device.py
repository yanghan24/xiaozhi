import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).resolve().parents[2] / "scripts/provision_device.py"
spec = importlib.util.spec_from_file_location("provision_device", MODULE)
provisioning = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provisioning)


def manifest():
    return dict(version=1, hardware_id="020000000001", device_id="speaker-a",
                credential="A" * 43, ota_url="https://speaker.example:18443/xiaozhi/ota/",
                control_origin="")


class Serial:
    def __init__(self, hardware="020000000001", result="stored"):
        self.frames = [json.dumps(dict(version=1, mode="device_provisioning", hardware_id=hardware)).encode() + b"\n"]
        self.result = result
        self.written = []

    def read_until(self, *args):
        return self.frames.pop(0)

    def write(self, raw):
        self.written.append(raw)
        self.frames.append(json.dumps(dict(version=1, operation_id=json.loads(raw)["operation_id"],
                                           result=self.result)).encode() + b"\n")

    def flush(self):
        pass


class ProvisioningTests(unittest.TestCase):
    def test_manifest_rejects_unsafe_identity_and_origins(self):
        for field, value in [("credential", "short"), ("hardware_id", "bad"),
                             ("device_id", "speaker\na"), ("ota_url", "http://speaker.example/xiaozhi/ota/"),
                             ("ota_url", "https://user:pass@speaker.example/xiaozhi/ota/"),
                             ("ota_url", "https://speaker.example/xiaozhi/ota/?"),
                             ("control_origin", "https://other.example/path")]:
            with self.subTest(field=field):
                value_to_test = manifest()
                value_to_test[field] = value
                with self.assertRaises(provisioning.ProvisioningError):
                    provisioning.validate_manifest(value_to_test)

    def test_file_permissions_and_duplicate_fields(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "private.json"
            path.write_text(json.dumps(manifest()))
            path.chmod(0o644)
            with self.assertRaises(provisioning.ProvisioningError):
                provisioning.load_manifest(path)
            path.chmod(0o600)
            self.assertEqual(provisioning.load_manifest(path), manifest())
            path.write_text(json.dumps(manifest()).replace('"version": 1', '"version": 1, "version": 1'))
            with self.assertRaises(provisioning.ProvisioningError):
                provisioning.load_manifest(path)

    def test_wrong_physical_device_never_receives_secret(self):
        port = Serial(hardware="020000000002")
        with self.assertRaises(provisioning.ProvisioningError):
            provisioning.provision(port, manifest())
        self.assertEqual(port.written, [])

    def test_writes_one_record_and_requires_commit_confirmation(self):
        port = Serial()
        provisioning.provision(port, manifest())
        self.assertEqual(len(port.written), 1)
        self.assertEqual(json.loads(port.written[0])["identity"], manifest())
        self.assertEqual(json.loads(port.written[0])["action"], "write")
        for result in ["identity_conflict", "storage_unavailable", "unknown"]:
            with self.subTest(result=result):
                with self.assertRaises(provisioning.ProvisioningError):
                    provisioning.provision(Serial(result=result), manifest())

    def test_stale_confirmation_is_ignored_and_repair_is_explicit(self):
        port = Serial(result="storage_unavailable")
        port.frames.append(json.dumps(dict(version=1, operation_id="0" * 32,
                                           result="stored")).encode() + b"\n")
        with self.assertRaises(provisioning.ProvisioningError):
            provisioning.provision(port, manifest())
        port = Serial()
        provisioning.provision(port, manifest(), repair=True)
        self.assertEqual(json.loads(port.written[0])["action"], "repair")

    def test_durable_write_id_is_reused_and_receipt_is_returned(self):
        operation = "c" * 32
        for attempt in range(2):
            port = Serial()
            receipt = provisioning.provision(port, manifest(), operation_id=operation)
            self.assertEqual(receipt, dict(version=1, operation_id=operation, result="stored"))
            self.assertEqual(json.loads(port.written[0])["operation_id"], operation)
        port = Serial()
        with self.assertRaises(provisioning.ProvisioningError):
            provisioning.provision(port, manifest(), operation_id="unsafe")
        self.assertEqual(port.written, [])


if __name__ == "__main__":
    unittest.main()
