#include "playback_diagnostics.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3

#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

namespace SpeakerPlaybackDiagnostics {
namespace {
constexpr int64_t kPendingWriteMs = 1000;
std::atomic<int64_t> deadline_ms{0};
std::atomic<int64_t> writing_since_ms{0};
portMUX_TYPE sample_lock = portMUX_INITIALIZER_UNLOCKED;
Samples pending{};
TransportSample transport;
std::array<TransportSample, 4> completed_transports{};
size_t completed_count = 0;
bool transport_dirty = false;
bool voice_output = false;

int64_t NowMs() { return esp_timer_get_time() / 1000; }

bool ValidTrace(const char* id) {
    if (!id) return false;
    for (size_t i = 0; i < 32; ++i) {
        const char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return id[32] == '\0';
}

void RecordTransport(Phase phase, int64_t now) {
    if (!transport.trace_id.front()) return;
    if (phase == Phase::PacketReceived && !transport.stop_received && !transport.channel_closed) {
        if (!transport.received_frames) transport.first_packet_ms = now;
        else transport.max_receive_gap_ms = std::max(transport.max_receive_gap_ms, now - transport.last_packet_ms);
        transport.last_packet_ms = now;
        ++transport.received_frames;
        transport_dirty = true;
    } else if (phase == Phase::ChannelClosed && !transport.channel_closed) {
        transport.channel_closed = true;
        transport_dirty = true;
    }
}
}  // namespace

void SetWindow(int64_t remaining_ms, bool reset) {
    if (reset) {
        deadline_ms.store(0);
        writing_since_ms.store(0);
        portENTER_CRITICAL(&sample_lock);
        pending.fill({});
        transport = {};
        completed_count = 0;
        transport_dirty = false;
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
        RecordTransport(phase, now);
    }
    portEXIT_CRITICAL(&sample_lock);
}

void BeginTransport(const char* trace_id) {
    if (!deadline_ms.load(std::memory_order_relaxed)) return;
    const auto now = NowMs();
    const auto valid = ValidTrace(trace_id);
    portENTER_CRITICAL(&sample_lock);
    if (deadline_ms.load(std::memory_order_relaxed) > now) {
        if (transport_dirty) {
            if (completed_count == completed_transports.size()) {
                std::move(completed_transports.begin() + 1, completed_transports.end(), completed_transports.begin());
                --completed_count;
            }
            completed_transports[completed_count++] = transport;
        }
        transport = {};
        if (valid) std::memcpy(transport.trace_id.data(), trace_id, transport.trace_id.size());
        transport_dirty = valid;
    }
    portEXIT_CRITICAL(&sample_lock);
}

void ReceiveTransportStop(const char* trace_id) {
    if (!deadline_ms.load(std::memory_order_relaxed) || !ValidTrace(trace_id)) return;
    const auto now = NowMs();
    portENTER_CRITICAL(&sample_lock);
    if (deadline_ms.load(std::memory_order_relaxed) > now && std::strcmp(transport.trace_id.data(), trace_id) == 0) {
        transport.stop_received = true;
        transport_dirty = true;
    }
    portEXIT_CRITICAL(&sample_lock);
}

bool TakeTransport(TransportSample& sample) {
    portENTER_CRITICAL(&sample_lock);
    const bool available = completed_count || transport_dirty;
    if (completed_count) {
        sample = completed_transports.front();
        std::move(completed_transports.begin() + 1, completed_transports.begin() + completed_count,
                  completed_transports.begin());
        --completed_count;
    } else if (transport_dirty) {
        sample = transport;
        transport_dirty = false;
    }
    portEXIT_CRITICAL(&sample_lock);
    return available;
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
