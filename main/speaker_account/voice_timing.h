#pragma once

#include "display_diagnostics.h"
#include "playback_diagnostics.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

class SpeakerVoiceTiming {
public:
    enum Phase { Wake, ChannelOpen, Listening, FirstMicFrame, SttReceived,
                 FirstAudioReceived, FirstOutput, PlaybackDrained, PhaseCount };
    struct Report { int64_t until_ms; std::string body; };

    static SpeakerVoiceTiming& Get();
    void SetLease(int64_t server_ms, int64_t until_ms);
    void RecordInput(Phase phase);
    void BeginReply(const char* trace_id);
    uint32_t ReceiveAudio();
    void OutputAudio(uint32_t generation);
    void StopReply(bool playback_idle);
    void PlaybackIdle();
    void Interrupt();
    Report PrepareReport();
    void Acknowledge(int64_t until_ms);

private:
    using Marks = std::array<int64_t, PhaseCount>;
    struct Event { std::string trace_id; Phase phase; int64_t uptime_ms; int64_t elapsed_ms; };
    bool Enabled() const;
    void Finish();  // Holds mutex_.

    std::mutex mutex_;
    std::atomic<int64_t> deadline_ms_{0};
    int64_t until_ms_ = 0;
    Marks input_{};
    Marks output_{};
    std::string trace_id_;
    uint32_t generation_ = 0;
    bool stopping_ = false;
    std::deque<Event> events_;
    std::string report_id_;
    size_t report_count_ = 0;
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    SpeakerDisplayDiagnostics::Samples display_samples_{};
    size_t display_count_ = 0;
    int64_t display_sample_ms_ = 0;
    int64_t next_display_sample_ms_ = 0;
    SpeakerPlaybackDiagnostics::Samples playback_samples_{};
    size_t playback_count_ = 0;
    int64_t playback_sample_ms_ = 0;
    SpeakerPlaybackDiagnostics::TransportSample transport_sample_{};
    bool has_transport_sample_ = false;
#endif
};
