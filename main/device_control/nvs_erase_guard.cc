#include <nvs_flash.h>

// The deployed layout shares identity and settings in one NVS partition. Apply
// this guard to the final product and maintenance links, including dependencies.
// Ordinary resets use ResetUserSettings; damaged NVS requires explicit recovery.
extern "C" esp_err_t __wrap_nvs_flash_erase() { return ESP_ERR_INVALID_STATE; }

extern "C" esp_err_t __wrap_nvs_flash_erase_partition(const char*) { return ESP_ERR_INVALID_STATE; }
