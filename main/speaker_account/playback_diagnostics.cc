#include "playback_diagnostics.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3

#include <atomic>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

namespace SpeakerPlaybackDiagnostics {
namespace {
constexpr int64_t kPendingWriteMs = 1000;
std::atomic<int64_t> deadline_ms{0};
std::atomic<int64_t> writing_since_ms{0};
portMUX_TYPE sample_lock = portMUX_INITIALIZER_UNLOCKED;
Samples pending{};
bool voice_output = false;

int64_t NowMs() { return esp_timer_get_time() / 1000; }
}  // namespace

void SetWindow(int64_t remaining_ms, bool reset) {
    if (reset) {
        deadline_ms.store(0);
        writing_since_ms.store(0);
        portENTER_CRITICAL(&sample_lock);
        pending.fill({});
        portEXIT_CRITICAL(&sample_lock);
    }
    if (remaining_ms > 0) deadline_ms.store(NowMs() + remaining_ms);
}

void Record(Phase phase, int code) {
    const auto deadline = deadline_ms.load(std::memory_order_relaxed);
    if (deadline == 0) return;
    const auto now = NowMs();
    if (deadline <= now) return;
    portENTER_CRITICAL(&sample_lock);
    if (deadline_ms.load(std::memory_order_relaxed) > now) {
        auto& sample = pending[static_cast<size_t>(phase)];
        ++sample.count;
        sample.uptime_ms = now;
        sample.code = code;
    }
    portEXIT_CRITICAL(&sample_lock);
}

size_t TakeSamples(Samples& samples) {
    portENTER_CRITICAL(&sample_lock);
    samples = pending;
    pending.fill({});
    portEXIT_CRITICAL(&sample_lock);
    const auto writing_since = writing_since_ms.load();
    const auto now = NowMs();
    if (writing_since && now - writing_since >= kPendingWriteMs) {
        samples[static_cast<size_t>(Phase::WritePending)] = {1, now, static_cast<int>(now - writing_since)};
    }
    size_t count = 0;
    for (const auto& sample : samples) {
        count += sample.count > 0;
    }
    return count;
}

const char* PhaseName(Phase phase) {
    constexpr const char* names[] = {
        "start_received", "start_handled", "packet_received", "packet_dropped", "decoded", "decode_error",
        "output_begin", "write_started", "write_ok", "write_error", "write_pending", "stop_received",
        "stop_handled", "drained", "channel_closed", "channel_cleaned"
    };
    static_assert(std::size(names) == static_cast<size_t>(Phase::Count));
    return names[static_cast<size_t>(phase)];
}

void SetOutputContext(bool voice) {
    voice_output = voice;
    if (voice) Record(Phase::OutputBegin);
}

void BeforeCodecWrite() {
    if (!voice_output) return;
    const auto deadline = deadline_ms.load(std::memory_order_relaxed);
    if (deadline == 0) return;
    const auto now = NowMs();
    if (deadline <= now) return;
    writing_since_ms.store(now ? now : 1);
    Record(Phase::WriteStarted);
}

void AfterCodecWrite(int result) {
    if (voice_output) {
        writing_since_ms.store(0);
        Record(result == 0 ? Phase::WriteOk : Phase::WriteError, result);
    }
}
}  // namespace SpeakerPlaybackDiagnostics

#endif
