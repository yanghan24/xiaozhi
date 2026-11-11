#pragma once

#include <esp_err.h>
#include "identity_config.h"

namespace device_control {
std::string DeviceHardwareId();
bool LoadDeviceIdentity(IdentityConfig& config);
enum class IdentityWrite { Provision, Repair, Recovery };
// Provision permits initial issuance or an identical retry, never credential rotation.
// Repair requires a damaged blob and matching ID anchor. Recovery writes a new
// administrator-issued credential for the same ID; authorization is checked by
// the connected recovery tool, not by this offline maintenance image.
esp_err_t StoreDeviceIdentity(const std::string& json,
                              IdentityWrite mode = IdentityWrite::Provision);
// Erase user namespaces without ever erasing device_ctrl, including during an interrupted reset.
esp_err_t ResetUserSettings();
void RunDeviceProvisioning();
}  // namespace device_control
