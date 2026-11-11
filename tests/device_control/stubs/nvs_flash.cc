#include "nvs_flash.h"

extern "C" esp_err_t nvs_flash_erase() { return ESP_OK; }
extern "C" esp_err_t nvs_flash_erase_partition(const char*) { return ESP_OK; }
