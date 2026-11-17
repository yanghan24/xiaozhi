import binascii
from contextlib import contextmanager
import copy
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

try:
    import esptool as installed_esptool
except ImportError:
    installed_esptool = None

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import delivery_hardware as physical


def selection(sequence, state=2):
    raw = bytearray(b"\xff" * 8192)
    struct.pack_into("<I", raw, 0, sequence)
    struct.pack_into("<I", raw, 24, state)
    struct.pack_into("<I", raw, 28, binascii.crc32(raw[:4], 0xffffffff) & 0xffffffff)
    return raw


class HardwareTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.image = b"application-test-image"
        (self.directory / "xiaozhi.bin").write_bytes(self.image)
        self.artifacts = {"product": {"sha256": physical.digest(self.image)}}
        self.hardware = physical.P4Hardware("fake", self.directory, self.directory, self.artifacts, self.directory)
        self.baseline = dict(hardware_id="020000000001", application_offset=0x410000,
                             application_bytes=0x3f0000, protected_prefix_sha256="unchanged")
        self.calls = []
        self.reads = []
        self.esp = SimpleNamespace(read_flash=self.read, flash_md5sum=lambda *args: __import__("hashlib").md5(self.image).hexdigest())

        @contextmanager
        def connection():
            yield self.esp, 32 * 1024 * 1024
        self.hardware.connection = connection
        self.hardware.inspect = lambda *args: (copy.deepcopy(self.baseline), b"")
        self.addCleanup(patch.stopall)
        patch.dict(sys.modules, esptool=SimpleNamespace(
            cmds=SimpleNamespace(write_flash=lambda esp, args: self.calls.append(args)))).start()

    def read(self, offset, size):
        self.reads.append((offset, size))
        return self.image

    def test_only_current_application_slot_is_written_then_independently_read(self):
        self.hardware.flash("product", self.baseline)
        self.assertEqual(len(self.calls), 1)
        arguments = self.calls[0]
        self.assertEqual(len(arguments.addr_filename), 1)
        offset, application = arguments.addr_filename[0]
        self.assertEqual(offset, 0x410000)
        self.assertEqual(Path(application.name).read_bytes(), self.image)
        self.assertTrue(application.closed)
        self.assertEqual((arguments.flash_mode, arguments.flash_freq, arguments.flash_size), ("keep", "keep", "keep"))
        self.assertEqual(self.reads, [(0x410000, len(self.image))])
        self.assertFalse(arguments.erase_all)
        self.assertFalse(arguments.force)

    @unittest.skipIf(installed_esptool is None, "Run with the ESP-IDF Python environment for real esptool compatibility")
    def test_installed_esptool_does_not_upload_another_stub_to_the_verified_connection(self):
        # Exercise the installed CLI entry point up to its physical write boundary.
        # A second run_stub on this already-running stub reproduces the real USB failure.
        self.esp.CHIP_NAME = "ESP32-P4"
        self.esp.IS_STUB = True
        self.esp.secure_download_mode = False
        self.esp.get_chip_description = lambda: "ESP32-P4"
        self.esp.get_chip_features = lambda: []
        self.esp.get_crystal_freq = lambda: 40
        self.esp.get_usb_mode = lambda: None
        self.esp.read_mac = lambda kind="BASE_MAC": None if kind == "EUI64" else bytes.fromhex("020000000001")
        self.esp.run_stub = Mock(side_effect=RuntimeError("A verified active stub cannot accept another ROM stub upload"))
        writes = []
        with patch.dict(sys.modules, esptool=installed_esptool), \
                patch.object(installed_esptool, "get_port_list", return_value=["fake"]), \
                patch.object(installed_esptool.cmds, "write_flash", side_effect=lambda esp, args: writes.append((esp, args))):
            self.hardware.flash("product", self.baseline)
        self.esp.run_stub.assert_not_called()
        self.assertEqual(len(writes), 1)
        self.assertIs(writes[0][0], self.esp)
        self.assertEqual(self.reads, [(0x410000, len(self.image))])

    def test_wrong_physical_identity_prevents_any_flash_write(self):
        other = dict(self.baseline, hardware_id="020000000002")
        self.hardware.inspect = lambda *args: (other, b"")
        with self.assertRaises(physical.DeliveryError):
            self.hardware.flash("product", self.baseline)
        self.assertEqual(self.calls, [])

    def test_changed_or_oversized_artifact_prevents_any_flash_write(self):
        (self.directory / "xiaozhi.bin").write_bytes(b"changed")
        with self.assertRaises(physical.DeliveryError):
            self.hardware.flash("product", self.baseline)
        (self.directory / "xiaozhi.bin").write_bytes(self.image)
        self.baseline["application_bytes"] = 2
        with self.assertRaises(physical.DeliveryError):
            self.hardware.flash("product", self.baseline)
        self.assertEqual(self.calls, [])

    def test_failed_readback_never_reports_success(self):
        self.hardware.read = lambda *args: b"corrupted"
        @contextmanager
        def connection():
            yield SimpleNamespace(read_flash=self.hardware.read), 32 * 1024 * 1024
        self.hardware.connection = connection
        with self.assertRaises(physical.DeliveryError):
            self.hardware.flash("product", self.baseline)

    def test_protected_region_changes_are_detected_after_write(self):
        snapshots = iter([(copy.deepcopy(self.baseline), b""),
                          (dict(self.baseline, protected_prefix_sha256="changed"), b"")])
        self.hardware.inspect = lambda *args: next(snapshots)
        with self.assertRaises(physical.DeliveryError):
            self.hardware.flash("product", self.baseline)

    def test_ota_selection_requires_valid_crc_and_a_confirmed_boot(self):
        self.assertEqual(physical.active_slot(selection(1)), "ota_0")
        self.assertEqual(physical.active_slot(selection(2)), "ota_1")
        invalid = selection(1)
        invalid[28] ^= 1
        for raw in (invalid, b"\xff" * 8192, selection(1, 1), selection(1, 3), selection(1, 4)):
            with self.assertRaises(physical.DeliveryError):
                physical.active_slot(raw)

    def test_identity_nvs_is_the_only_mutable_prefix_region(self):
        original = bytearray(0x20000)
        before = physical.protected_prefix(original)
        original[0x9000] = 1
        self.assertEqual(physical.protected_prefix(original), before)
        for offset in (0x2000, 0x8000, 0xd000, 0xf000):
            changed = original.copy()
            changed[offset] = 1
            self.assertNotEqual(physical.protected_prefix(changed), before)


if __name__ == "__main__":
    unittest.main()
