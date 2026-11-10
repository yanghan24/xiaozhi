#pragma once
#include <esp_err.h>
#include "identity_config.h"

namespace device_control {
void ReportDeliveryHardware();
esp_err_t CheckDeviceDelivery(const IdentityConfig& identity);
}  // namespace device_control
