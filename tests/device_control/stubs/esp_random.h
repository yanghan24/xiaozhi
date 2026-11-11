#pragma once
#include <cstddef>
#include <cstdint>
void esp_fill_random(void* buffer, size_t size);
uint32_t esp_random();
