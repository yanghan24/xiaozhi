#include <nvs_flash.h>
#include "device_identity.h"

extern "C" void app_main() {
    // Maintenance never auto-erases NVS, including on corruption or version mismatch.
    ESP_ERROR_CHECK(nvs_flash_init());
    device_control::RunDeviceProvisioning();
}
