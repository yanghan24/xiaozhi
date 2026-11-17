#pragma once
#include <cstdint>
using TickType_t = uint32_t;
using TaskHandle_t = void*;
constexpr int pdTRUE = 1;
constexpr int pdPASS = 1;
#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)
