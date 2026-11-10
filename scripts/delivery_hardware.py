"""Physical adapter for the verified P4 4.3-inch board and preserved 16 MB layout.

Only the selected application partition is written. Bootloader, partition table,
OTA selection and assets are independently checked around every image write.
"""

import binascii
import contextlib
import hashlib
import io
import os
from pathlib import Path
import struct
import sys
import time
from types import SimpleNamespace

from online_delivery import DeliveryError, private_directory, save_private
from provision_device import provision, read_frame
from verify_product_build import verify


def digest(value):
    return hashlib.sha256(value).hexdigest()


def image_details(path):
    from esptool.bin_image import ESP32P4FirmwareImage
    raw = path.read_bytes()
    image = ESP32P4FirmwareImage(io.BytesIO(raw))
    image.verify()
    if (image.chip_id != 18 or not image.append_digest or image.stored_digest != image.calc_digest
            or not 0 < len(raw) <= 0x3f0000):
        raise DeliveryError("Invalid or oversized P4 application image")
    descriptors = [segment.data for segment in image.segments if segment.data[:4] == bytes.fromhex("3254cdab")]
    if len(descriptors) != 1 or len(descriptors[0]) < 256:
        raise DeliveryError("Application image lacks a unique ESP-IDF descriptor")
    return dict(sha256=digest(raw), elf_sha256=descriptors[0][144:176].hex(),
                min_revision=image.min_rev_full, max_revision=image.max_rev_full)


def build_artifacts(product, maintenance):
    checked = verify(product)
    if not all(checked["checks"].values()):
        raise DeliveryError("Product build failed the preserved UI/control/layout checks")
    config = (product / "config/sdkconfig.h").read_text()
    if "#define CONFIG_XIAOZHI_ONLINE_DELIVERY 1\n" not in config:
        raise DeliveryError("Product build does not enable online delivery")
    bench_config = (maintenance / "config/sdkconfig.h").read_text()
    for option in ("CONFIG_IDF_TARGET_ESP32P4", "CONFIG_XIAOZHI_DEVICE_PROVISIONING", "CONFIG_ESP_CONSOLE_UART_DEFAULT"):
        if "#define " + option + " 1\n" not in bench_config:
            raise DeliveryError("Maintenance image must target this P4 UART connection")
    first = image_details(product / "xiaozhi.bin")
    second = image_details(maintenance / "xiaozhi_provisioning.bin")
    if first["elf_sha256"] != digest((product / "xiaozhi.elf").read_bytes()):
        raise DeliveryError("Product binary and ELF do not belong to the same build")
    table = product / "partition_table/partition-table.bin"
    if table.read_bytes() != (maintenance / "partition_table/partition-table.bin").read_bytes():
        raise DeliveryError("Product and maintenance builds have different partition layouts")
    return dict(board_type="waveshare-esp32-p4-wifi6-touch-lcd-4.3", product=first, maintenance=second,
                product_elf_sha256=first["elf_sha256"], partition_table_sha256=digest(table.read_bytes()))


def partition_table(raw):
    idf = os.environ.get("IDF_PATH", "")
    if not idf:
        raise DeliveryError("Activate ESP-IDF before physical delivery")
    sys.path.insert(0, str(Path(idf) / "components/partition_table"))
    from gen_esp32part import PartitionTable
    table = PartitionTable.from_binary(raw)
    table.verify()
    actual = {part.name: (part.type, part.subtype, part.offset, part.size) for part in table}
    expected = dict(nvs=(1, 2, 0x9000, 0x4000), otadata=(1, 0, 0xd000, 0x2000),
                    phy_init=(1, 1, 0xf000, 0x1000), ota_0=(0, 0x10, 0x20000, 0x3f0000),
                    ota_1=(0, 0x11, 0x410000, 0x3f0000), assets=(1, 0x82, 0x800000, 0x800000))
    if actual != expected:
        raise DeliveryError("Physical partition layout is unsupported; no flash was written")
    return actual


def active_slot(otadata):
    candidates = []
    for sector in (0, 4096):
        sequence = struct.unpack_from("<I", otadata, sector)[0]
        state = struct.unpack_from("<I", otadata, sector + 24)[0]
        crc = struct.unpack_from("<I", otadata, sector + 28)[0]
        computed = binascii.crc32(otadata[sector:sector + 4], 0xffffffff) & 0xffffffff
        if sequence not in (0, 0xffffffff) and state not in (3, 4) and crc == computed:
            candidates.append((sequence, state))
    if not candidates:
        raise DeliveryError("No valid OTA selection; refusing to guess an application partition")
    sequence, state = max(candidates)
    if state not in (2, 0xffffffff):
        raise DeliveryError("Current application is still a trial boot; validate it before delivery")
    return "ota_" + str((sequence - 1) % 2)


def protected_prefix(prefix):
    # NVS changes through the image's atomic identity API; its original contents are backed up privately.
    return digest(prefix[:0x9000] + prefix[0xd000:0x20000])


class P4Hardware:
    def __init__(self, port, product, maintenance, artifacts, journal):
        self.port = port
        self.product = product
        self.maintenance = maintenance
        self.artifacts = artifacts
        self.journal = journal

    @contextlib.contextmanager
    def connection(self):
        import esptool
        private_directory(self.journal)
        descriptor = os.open(self.journal / "transport.log", os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        esp = None
        with os.fdopen(descriptor, "a") as log, contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
            try:
                esp = esptool.detect_chip(port=self.port, baud=115200)
                if esp.CHIP_NAME != "ESP32-P4" or esp.secure_download_mode:
                    raise DeliveryError("Connect the supported P4 in ordinary maintenance mode")
                if esp.get_secure_boot_enabled() or esp.get_flash_encryption_enabled():
                    raise DeliveryError("This delivery adapter cannot replace signed or encrypted firmware")
                esp = esp.run_stub()
                esp.change_baud(921600)
                esp.flash_spi_attach(0)
                capacity = 1 << ((esp.flash_id() >> 16) & 0xff)
                if capacity < 0x1000000:
                    raise DeliveryError("Physical flash is too small for the preserved layout")
                esp.flash_set_parameters(capacity)
                yield esp, capacity
                esp.hard_reset()
            finally:
                if esp is not None:
                    esp._port.close()

    def inspect(self, esp, capacity):
        prefix = esp.read_flash(0, 0x20000)
        parts = partition_table(prefix[0x8000:0x9000])
        expected = (self.product / "partition_table/partition-table.bin").read_bytes()
        if digest(expected) != self.artifacts["partition_table_sha256"] or prefix[0x8000:0x8000 + len(expected)] != expected:
            raise DeliveryError("Physical partition table differs from the verified build")
        revision = esp.get_chip_revision()
        for kind in ("product", "maintenance"):
            image = self.artifacts[kind]
            if revision < image["min_revision"] or (image["max_revision"] not in (0, 65535) and revision > image["max_revision"]):
                raise DeliveryError("Firmware does not support this P4 silicon revision")
        slot = active_slot(prefix[0xd000:0xf000])
        baseline = dict(hardware_id=bytes(esp.read_mac()).hex(), revision=revision, flash_bytes=capacity,
                        active_slot=slot, application_offset=parts[slot][2], application_bytes=parts[slot][3],
                        protected_prefix_sha256=protected_prefix(prefix), assets_md5=esp.flash_md5sum(0x800000, 0x800000))
        return baseline, prefix

    def backup(self, directory):
        private_directory(directory)
        with self.connection() as (esp, capacity):
            baseline, prefix = self.inspect(esp, capacity)
            save_private(directory / "boot-prefix.bin", prefix)
            application = esp.read_flash(baseline["application_offset"], baseline["application_bytes"])
            if hashlib.md5(application).hexdigest() != esp.flash_md5sum(baseline["application_offset"], len(application)):
                raise DeliveryError("Application backup differs from the device checksum")
            save_private(directory / "application.bin", application)
            baseline["backup_application_sha256"] = digest(application)
            baseline["backup_prefix_sha256"] = digest(prefix)
            save_private(directory / "baseline.json", baseline)
        return baseline

    @staticmethod
    def compare(current, baseline):
        if any(current.get(key) != baseline.get(key) for key in current):
            raise DeliveryError("Physical device, OTA selection, assets or protected flash changed; stopped before writing")

    def verify_baseline(self, baseline):
        if (digest((self.journal / "backup/application.bin").read_bytes()) != baseline["backup_application_sha256"]
                or digest((self.journal / "backup/boot-prefix.bin").read_bytes()) != baseline["backup_prefix_sha256"]):
            raise DeliveryError("Private recovery backup is missing or damaged")
        with self.connection() as (esp, capacity):
            current, _ = self.inspect(esp, capacity)
            self.compare(current, baseline)

    def flash(self, kind, baseline):
        import esptool
        if kind not in ("product", "maintenance"):
            raise DeliveryError("Unsupported application image")
        path = self.product / "xiaozhi.bin" if kind == "product" else self.maintenance / "xiaozhi_provisioning.bin"
        image = path.read_bytes()
        if digest(image) != self.artifacts[kind]["sha256"]:
            raise DeliveryError("Application artifact changed since preflight")
        with self.connection() as (esp, capacity):
            before, _ = self.inspect(esp, capacity)
            self.compare(before, baseline)
            offset = baseline["application_offset"]
            if len(image) > baseline["application_bytes"] or offset % 4096:
                raise DeliveryError("Application cannot be safely written to the current slot")
            snapshot = self.journal / (kind + "-verified.bin")
            save_private(snapshot, image)
            # The CLI uploads another stub even when given an active stub. Use the
            # write operation directly so this verified connection stays alive for read-back.
            with snapshot.open("rb") as application:
                esptool.cmds.write_flash(esp, SimpleNamespace(
                    addr_filename=[(offset, application)], flash_mode="keep", flash_freq="keep",
                    flash_size="keep", compress=True, no_compress=False, no_stub=False,
                    encrypt=False, encrypt_files=None, ignore_flash_encryption_efuse_setting=False,
                    force=False, erase_all=False,
                ))
            actual = esp.read_flash(offset, len(image))
            if actual != image or hashlib.md5(image).hexdigest() != esp.flash_md5sum(offset, len(image)):
                raise DeliveryError("Application write did not pass independent flash read-back")
            after, _ = self.inspect(esp, capacity)
            self.compare(after, baseline)

    @contextlib.contextmanager
    def serial(self):
        import serial
        with serial.Serial(port=None, baudrate=115200, timeout=0.5, write_timeout=5, exclusive=True) as port:
            port.dtr = False
            port.rts = False
            port.port = self.port
            port.open()
            yield port

    def report(self):
        deadline = time.monotonic() + 180
        with self.serial() as port:
            while True:
                frame = read_frame(port, deadline)
                if frame.get("mode") == "device_hardware":
                    return frame

    def write(self, manifest, operation):
        with self.serial() as port:
            return provision(port, manifest, operation_id=operation)

    def reset(self):
        with self.connection():
            pass
