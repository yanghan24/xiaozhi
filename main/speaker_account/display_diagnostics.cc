#include "display_diagnostics.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3

#include <atomic>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace SpeakerDisplayDiagnostics;

namespace {
static_assert(std::atomic<uint32_t>::is_always_lock_free);
constexpr size_t kContexts = std::tuple_size_v<Samples>;
DRAM_ATTR std::atomic<uint32_t> deadline_tick{0};
DRAM_ATTR std::atomic<uint32_t> context{0};
DRAM_ATTR std::atomic<uint32_t> underruns[kContexts]{};

Sample Decode(uint32_t index, uint32_t count) {
    return {static_cast<Mode>(index / 2), (index % 2) != 0, count};
}
}  // namespace

extern "C" void IRAM_ATTR xiaozhi_display_record_underrun(void) {
    const auto deadline = deadline_tick.load(std::memory_order_relaxed);
    if (deadline && static_cast<int32_t>(deadline - xTaskGetTickCountFromISR()) > 0) {
        underruns[context.load(std::memory_order_relaxed)].fetch_add(1, std::memory_order_relaxed);
    }
}

void SpeakerDisplayDiagnostics::SetWindow(int64_t remaining_ms, bool reset) {
    if (reset) {
        deadline_tick.store(0, std::memory_order_relaxed);
        for (auto& counter : underruns) {
            counter.store(0, std::memory_order_relaxed);
        }
    }
    if (remaining_ms > 0) {
        const uint32_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(remaining_ms);
        deadline_tick.store(deadline ? deadline : 1, std::memory_order_relaxed);
    }
}

void SpeakerDisplayDiagnostics::SetContext(Mode mode, bool animation_active) {
    context.store(static_cast<uint32_t>(mode) * 2 + animation_active, std::memory_order_relaxed);
}

size_t SpeakerDisplayDiagnostics::TakeSamples(Samples& samples, bool include_zero) {
    size_t size = 0;
    for (size_t i = 0; i < kContexts; ++i) {
        const auto count = underruns[i].exchange(0, std::memory_order_relaxed);
        if (count) {
            samples[size++] = Decode(i, count);
        }
    }
    if (!size && include_zero) {
        samples[size++] = Decode(context.load(std::memory_order_relaxed), 0);
    }
    return size;
}

const char* SpeakerDisplayDiagnostics::ModeName(Mode mode) {
    constexpr const char* names[] = {"idle", "connecting", "listening", "waiting", "speaking"};
    static_assert(std::size(names) == static_cast<size_t>(Mode::Count));
    return names[static_cast<size_t>(mode)];
}

#endif
