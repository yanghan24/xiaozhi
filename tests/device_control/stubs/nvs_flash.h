#pragma once

#include "esp_err.h"

extern "C" esp_err_t nvs_flash_erase();
extern "C" esp_err_t nvs_flash_erase_partition(const char*);
