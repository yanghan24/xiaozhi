#pragma once

#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3

#include <array>
#include <cstddef>
#include <cstdint>

namespace SpeakerPlaybackDiagnostics {

enum class Phase {
    StartReceived, StartHandled, PacketReceived, PacketDropped, Decoded, DecodeError, OutputBegin,
    WriteStarted, WriteOk, WriteError, WritePending, StopReceived, StopHandled, Drained,
    ChannelClosed, ChannelCleaned, Count
};

struct Sample {
    uint32_t count = 0;
    int64_t uptime_ms = 0;
    int code = 0;
};
using Samples = std::array<Sample, static_cast<size_t>(Phase::Count)>;

struct TransportSample {
    std::array<char, 33> trace_id{};
    uint32_t received_frames = 0;
    int64_t first_packet_ms = 0;
    int64_t last_packet_ms = 0;
    int64_t max_receive_gap_ms = 0;
    bool stop_received = false;
    bool channel_closed = false;
};

void SetWindow(int64_t remaining_ms, bool reset);
void Record(Phase phase, int code = 0);
size_t TakeSamples(Samples& samples);
const char* PhaseName(Phase phase);
void BeginTransport(const char* trace_id);
void ReceiveTransportStop(const char* trace_id);
bool TakeTransport(TransportSample& sample);

// Called by the audio output task and its synchronous board codec call.
void SetOutputContext(bool voice);
void BeforeCodecWrite();
void AfterCodecWrite(int result);

}  // namespace SpeakerPlaybackDiagnostics

#endif
