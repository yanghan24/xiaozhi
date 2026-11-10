"""Verify the P4 product image, preserved dashboard, control gate and flash layout."""

import argparse
import hashlib
import json
import shlex
import struct
import subprocess
from pathlib import Path


def verify(build):
    checks = {}
    config = (build / "config/sdkconfig.h").read_text()
    required_config = (
        "CONFIG_IDF_TARGET_ESP32P4",
        "CONFIG_ESP32P4_REV_MIN_301",
        "CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB",
        "CONFIG_XIAOZHI_DEVICE_CONTROL",
        "CONFIG_LV_USE_QRCODE",
        "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE",
    )
    checks["product_config"] = all(f"#define {key} 1\n" in config for key in required_config)
    checks["maintenance_disabled"] = "#define CONFIG_XIAOZHI_DEVICE_PROVISIONING " not in config

    commands = json.loads((build / "compile_commands.json").read_text())
    application = [
        entry for entry in commands
        if Path(entry["file"]).as_posix().endswith("/main/application.cc")
    ]
    checks["application_size_optimization"] = len(application) == 1 and [
        flag for flag in shlex.split(application[0]["command"])
        if flag.startswith("-O")
    ][-1:] == ["-Os"]
    sources = {Path(entry["file"]).as_posix() for entry in commands}
    required_sources = (
        "display/standby_dashboard.cc",
        "display/assets/hncig_logo_i2.c",
        "services/environment_service.cc",
        "services/iot_telemetry_service.cc",
        "device_control/device_identity.cc",
        "device_control/control_transport.cc",
    )
    checks["product_sources"] = all(
        any(source.endswith("/main/" + required) for source in sources)
        for required in required_sources
    )
    checks["no_maintenance_source"] = not any(
        source.endswith("/device_provisioning.cc") for source in sources
    )

    symbols = subprocess.run(
        ["nm", "-C", "--defined-only", str(build / "xiaozhi.elf")],
        capture_output=True, text=True, check=True,
    ).stdout
    required_symbols = (
        "LcdDisplay::CreateStandbyDashboard()",
        "StandbyDashboard::CreateClockPanel()",
        "hncig_logo_i2",
        "LcdDisplay::CreateDeviceBindingUi()",
        "DeviceControl::Enabled() const",
        "DeviceControl::AcknowledgeState(",
        "device_control::PendingAck::Rebase(",
        "device_control::LoadDeviceIdentity(",
        "AudioService::StopControlledAudio()",
    )
    checks["dashboard_and_control_linked"] = all(name in symbols for name in required_symbols)
    checks["no_maintenance_entrypoint"] = "RunDeviceProvisioning()" not in symbols

    partitions = {}
    table = (build / "partition_table/partition-table.bin").read_bytes()
    for offset in range(0, len(table), 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from(
            "<HBBII16sI", table, offset
        )
        if magic != 0x50AA:
            break
        name = label.rstrip(b"\0").decode("ascii")
        partitions[name] = (kind, subtype, address, size)
    expected = {
        "nvs": (1, 2, 0x9000, 0x4000),
        "otadata": (1, 0, 0xD000, 0x2000),
        "phy_init": (1, 1, 0xF000, 0x1000),
        "ota_0": (0, 0x10, 0x20000, 0x3F0000),
        "ota_1": (0, 0x11, 0x410000, 0x3F0000),
        "assets": (1, 0x82, 0x800000, 0x800000),
    }
    checks["preserved_16mb_layout"] = partitions == expected
    slots = [value[3] for value in partitions.values() if value[0] == 0]
    image = (build / "xiaozhi.bin").read_bytes()
    capacity = min(slots) if slots else 0
    checks["image_fits_both_slots"] = 0 < len(image) <= capacity
    return {
        "checks": checks,
        "image_bytes": len(image),
        "smallest_app_partition_bytes": capacity,
        "remaining_bytes": capacity - len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    result = verify(args.build_dir)
    print(json.dumps(result, indent=2))
    return 0 if all(result["checks"].values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
