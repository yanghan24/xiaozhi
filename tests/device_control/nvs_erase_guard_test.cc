#include <stdexcept>
#include "nvs_flash.h"

int main() {
    if (nvs_flash_erase() != ESP_ERR_INVALID_STATE)
        throw std::runtime_error("product dependencies must not erase the identity NVS partition");
    if (nvs_flash_erase_partition("nvs") != ESP_ERR_INVALID_STATE)
        throw std::runtime_error("a named partition erase must not bypass identity preservation");
}
