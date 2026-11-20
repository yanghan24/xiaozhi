#include "voice_timing.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <string_view>

namespace {
constexpr int64_t kWindowMs = 30 * 60 * 1000;
constexpr int64_t kMaxTurnMs = 120 * 1000;
constexpr size_t kMaxEvents = 64;
constexpr size_t kBatchSize = 16;
constexpr const char* kPhaseNames[] = {"wake", "channel_open", "listening", "first_mic_frame",
    "stt_received", "first_audio_received", "first_output", "playback_drained"};
static_assert(std::size(kPhaseNames) == SpeakerVoiceTiming::PhaseCount);

int64_t NowMs() { return esp_timer_get_time() / 1000; }

std::string Hex(const uint8_t* data, size_t size) {
    std::string result;
    result.reserve(size * 2);
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) {
        result += hex[data[i] >> 4];
        result += hex[data[i] & 15];
    }
    return result;
}

bool ValidId(std::string_view id) {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
}  // namespace

SpeakerVoiceTiming& SpeakerVoiceTiming::Get() {
    static SpeakerVoiceTiming timing;
    return timing;
}

bool SpeakerVoiceTiming::Enabled() const { return deadline_ms_.load() > NowMs(); }

void SpeakerVoiceTiming::SetLease(int64_t server_ms, int64_t until_ms) {
    const auto remaining = until_ms - server_ms;
    if (server_ms < 0 || remaining <= 0 || remaining > kWindowMs) until_ms = 0;
    std::lock_guard lock(mutex_);
    const bool changed = until_ms != until_ms_;
    if (changed) {
        until_ms_ = until_ms;
        input_.fill(0);
        output_.fill(0);
        trace_id_.clear();
        stopping_ = false;
        events_.clear();
        report_count_ = 0;
        report_id_.clear();
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        display_count_ = 0;
        next_display_sample_ms_ = 0;
        playback_count_ = 0;
#endif
    }
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    SpeakerDisplayDiagnostics::SetWindow(until_ms ? remaining : 0, changed);
    SpeakerPlaybackDiagnostics::SetWindow(until_ms ? remaining : 0, changed);
#endif
    deadline_ms_.store(until_ms ? NowMs() + remaining : 0);
}

void SpeakerVoiceTiming::RecordInput(Phase phase) {
    if (!Enabled()) return;
    std::lock_guard lock(mutex_);
    if (phase == FirstMicFrame && !input_[Listening]) return;
    if (phase == Wake || (phase == Listening && input_[Listening])) input_.fill(0);
    if (!input_[phase]) input_[phase] = NowMs();
}

void SpeakerVoiceTiming::BeginReply(const char* id) {
    if (!Enabled()) return;
    std::lock_guard lock(mutex_);
    if (!Enabled()) return;
    Finish();
    if (id && ValidId(id)) {
        trace_id_ = id;
        output_ = input_;
        output_[SttReceived] = NowMs();
        if (++generation_ == 0) ++generation_;
    }
    input_.fill(0);
}

uint32_t SpeakerVoiceTiming::ReceiveAudio() {
    if (!Enabled()) return 0;
    std::lock_guard lock(mutex_);
    if (trace_id_.empty() || stopping_) return 0;
    if (!output_[FirstAudioReceived]) output_[FirstAudioReceived] = NowMs();
    output_[PlaybackDrained] = 0;
    return generation_;
}

void SpeakerVoiceTiming::OutputAudio(uint32_t generation) {
    if (!generation || !Enabled()) return;
    std::lock_guard lock(mutex_);
    if (generation == generation_ && !trace_id_.empty() && !output_[FirstOutput]) {
        output_[FirstOutput] = NowMs();
    }
}

void SpeakerVoiceTiming::StopReply(bool playback_idle) {
    if (!Enabled()) return;
    std::lock_guard lock(mutex_);
    stopping_ = !trace_id_.empty();
    if (stopping_ && playback_idle) {
        if (output_[FirstOutput] && !output_[PlaybackDrained]) output_[PlaybackDrained] = NowMs();
        Finish();
    }
}

void SpeakerVoiceTiming::PlaybackIdle() {
    if (!Enabled()) return;
    std::lock_guard lock(mutex_);
    if (output_[FirstOutput]) output_[PlaybackDrained] = NowMs();
    if (stopping_) Finish();
}

void SpeakerVoiceTiming::Interrupt() {
    if (!Enabled()) return;
    std::lock_guard lock(mutex_);
    Finish();
}

void SpeakerVoiceTiming::Finish() {
    if (!trace_id_.empty() && Enabled() && events_.size() + PhaseCount <= kMaxEvents) {
        const auto received = output_[SttReceived];
        int64_t start = received;
        for (auto mark : output_) {
            if (mark && mark >= received - kMaxTurnMs) start = std::min(start, mark);
        }
        for (size_t phase = 0; phase < output_.size(); ++phase) {
            auto mark = output_[phase];
            if (mark && mark >= start && mark - start <= 600000) {
                events_.push_back({trace_id_, static_cast<Phase>(phase), mark, mark - start});
            }
        }
    }
    trace_id_.clear();
    output_.fill(0);
    stopping_ = false;
}

SpeakerVoiceTiming::Report SpeakerVoiceTiming::PrepareReport() {
    std::lock_guard lock(mutex_);
    if (!Enabled()) return {};
    if (!trace_id_.empty() && NowMs() - output_[SttReceived] > kMaxTurnMs) Finish();
    if (report_id_.empty()) {
        size_t available = kBatchSize;
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        static_assert(std::tuple_size_v<SpeakerDisplayDiagnostics::Samples> + 1 < kBatchSize);
        display_sample_ms_ = NowMs();
        display_count_ = SpeakerDisplayDiagnostics::TakeSamples(
            display_samples_, display_sample_ms_ >= next_display_sample_ms_);
        if (display_count_) next_display_sample_ms_ = display_sample_ms_ + 60 * 1000;
        playback_sample_ms_ = display_sample_ms_;
        playback_count_ = SpeakerPlaybackDiagnostics::TakeSamples(playback_samples_);
        available -= display_count_ + (playback_count_ != 0);
#endif
        report_count_ = std::min(available, events_.size());
        if (available == kBatchSize && report_count_ == 0) return {};
        std::array<uint8_t, 16> random;
        esp_fill_random(random.data(), random.size());
        report_id_ = Hex(random.data(), random.size());
    }
    auto* root = cJSON_CreateObject();
    if (!root) return {};
    const auto* app = esp_app_get_description();
    cJSON_AddStringToObject(root, "report_id", report_id_.c_str());
    cJSON_AddNumberToObject(root, "diagnostics_until_ms", until_ms_);
    cJSON_AddStringToObject(root, "firmware_version", app->version);
    cJSON_AddStringToObject(root, "app_sha256", Hex(app->app_elf_sha256, sizeof(app->app_elf_sha256)).c_str());
    auto* entries = cJSON_AddArrayToObject(root, "entries");
    for (size_t i = 0; i < report_count_ && entries; ++i) {
        const auto& event = events_[i];
        auto* entry = cJSON_CreateObject();
        if (!entry) break;
        cJSON_AddItemToArray(entries, entry);
        cJSON_AddStringToObject(entry, "event", "voice_timing");
        cJSON_AddStringToObject(entry, "phase", kPhaseNames[event.phase]);
        cJSON_AddStringToObject(entry, "trace_id", event.trace_id.c_str());
        cJSON_AddNumberToObject(entry, "uptime_ms", event.uptime_ms);
        cJSON_AddNumberToObject(entry, "elapsed_ms", event.elapsed_ms);
    }
    size_t entry_count = report_count_;
    bool complete = true;
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    entry_count += display_count_;
    for (size_t i = 0; i < display_count_ && entries; ++i) {
        const auto& sample = display_samples_[i];
        auto* entry = cJSON_CreateObject();
        if (!entry) break;
        cJSON_AddItemToArray(entries, entry);
        cJSON_AddStringToObject(entry, "event", "display_underrun");
        cJSON_AddNumberToObject(entry, "uptime_ms", display_sample_ms_);
        cJSON_AddStringToObject(entry, "display_mode", SpeakerDisplayDiagnostics::ModeName(sample.mode));
        cJSON_AddBoolToObject(entry, "animation_active", sample.animation_active);
        cJSON_AddNumberToObject(entry, "underrun_count", sample.underruns);
    }
    if (playback_count_ && entries) {
        ++entry_count;
        auto* entry = cJSON_CreateObject();
        if (entry) {
            cJSON_AddItemToArray(entries, entry);
            cJSON_AddStringToObject(entry, "event", "tts_playback");
            cJSON_AddNumberToObject(entry, "uptime_ms", playback_sample_ms_);
            auto* samples = cJSON_AddArrayToObject(entry, "playback_stages");
            for (size_t i = 0; i < playback_samples_.size() && samples; ++i) {
                const auto& sample = playback_samples_[i];
                if (!sample.count) continue;
                auto* stage = cJSON_CreateObject();
                if (!stage) break;
                cJSON_AddItemToArray(samples, stage);
                const auto phase = static_cast<SpeakerPlaybackDiagnostics::Phase>(i);
                cJSON_AddStringToObject(stage, "phase", SpeakerPlaybackDiagnostics::PhaseName(phase));
                cJSON_AddNumberToObject(stage, "count", sample.count);
                cJSON_AddNumberToObject(stage, "uptime_ms", sample.uptime_ms);
                cJSON_AddNumberToObject(stage, "code", sample.code);
            }
            complete = samples && cJSON_GetArraySize(samples) == playback_count_;
        }
    }
#endif
    char* text = complete && entries && cJSON_GetArraySize(entries) == entry_count ? cJSON_PrintUnformatted(root) : nullptr;
    Report result{until_ms_, text ? text : ""};
    cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

void SpeakerVoiceTiming::Acknowledge(int64_t until_ms) {
    std::lock_guard lock(mutex_);
    if (until_ms != until_ms_) return;
    for (size_t i = 0; i < report_count_; ++i) events_.pop_front();
    report_count_ = 0;
    report_id_.clear();
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    display_count_ = 0;
    playback_count_ = 0;
#endif
}
