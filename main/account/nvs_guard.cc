#include <nvs_flash.h>

// The hardware credential lives in the existing NVS partition. Preferences
// can be cleared by DeviceAccount::ResetUserSettings; dependencies must not
// erase that entire partition when initialization or recovery fails.
extern "C" esp_err_t __wrap_nvs_flash_erase() { return ESP_ERR_INVALID_STATE; }
extern "C" esp_err_t __wrap_nvs_flash_erase_partition(const char*) { return ESP_ERR_INVALID_STATE; }
