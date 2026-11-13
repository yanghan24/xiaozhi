# Firmware request fixture

`firmware-request.json` is the output of the production C++ `device_control::Maintenance::Request` function, exercised by `tests/device_control/maintenance_test.cc --fixture` at firmware commit `51f4c09`. It contains a synthetic boot ID and the P4 4.3-inch system-info projection; it contains no device credential.

The HTTP regression submits this generated document directly, checks compatibility and writes the actual Java command envelope to `target/maintenance-response.json` for C++ parser replay. The fixture is a protocol check, not evidence that a physical device installed a firmware image.

For a live round trip through the compiled C++ request and response parser, run `DeviceMaintenanceFirmwareAcceptance` explicitly on Ubuntu with `-Dmaintenance.firmware.binary=/interop/host-product/maintenance_tests`. The executable must come from the firmware's host test build. The test uses the isolated test database and synthetic credentials; the binary path is a test dependency, never a device-provided command.
