#pragma once

#include <esp_err.h>
#include "identity_config.h"

namespace device_control {
std::string DeviceHardwareId();
bool LoadDeviceIdentity(IdentityConfig& config);
// Maintenance only: credentials are replaced as one atomic blob. A separate immutable
// device_id anchor permits explicit repair of a damaged blob without changing identity.
// Repair requires a damaged blob and an intact matching anchor; it never erases NVS.
esp_err_t StoreDeviceIdentity(const std::string& json, bool repair_corrupt = false);
// Erase user namespaces without ever erasing device_ctrl, including during an interrupted reset.
esp_err_t ResetUserSettings();
void RunDeviceProvisioning();
}  // namespace device_control
