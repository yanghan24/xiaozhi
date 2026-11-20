#pragma once

#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3

#include <array>
#include <cstddef>
#include <cstdint>

namespace SpeakerDisplayDiagnostics {

enum class Mode : uint32_t { Idle, Connecting, Listening, Waiting, Speaking, Count };
struct Sample {
    Mode mode;
    bool animation_active;
    uint32_t underruns;
};
using Samples = std::array<Sample, static_cast<size_t>(Mode::Count) * 2>;

void SetWindow(int64_t remaining_ms, bool reset);
void SetContext(Mode mode, bool animation_active);
size_t TakeSamples(Samples& samples, bool include_zero);
const char* ModeName(Mode mode);

}  // namespace SpeakerDisplayDiagnostics

#endif
