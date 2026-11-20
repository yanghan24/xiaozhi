#include "speaker_music.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "speaker_sd_storage.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <esp_audio_simple_dec.h>
#include <esp_mp3_dec.h>
#include <esp_pcm_dec.h>
#include <impl/esp_wav_dec.h>

namespace {
AudioService& Audio() { return Application::GetInstance().GetAudioService(); }

struct Resampler {
    esp_ae_rate_cvt_handle_t handle = nullptr;
    ~Resampler() { if (handle) esp_ae_rate_cvt_close(handle); }

    bool Open(unsigned input, unsigned output) {
        if (input == output) return true;
        esp_ae_rate_cvt_cfg_t config{
            .src_rate = input, .dest_rate = output, .channel = 1, .bits_per_sample = 16,
            .complexity = 2, .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,
        };
        return esp_ae_rate_cvt_open(&config, &handle) == ESP_AE_ERR_OK;
    }

    bool Convert(std::vector<int16_t>& pcm) {
        if (!handle) return true;
        uint32_t capacity = 0;
        if (esp_ae_rate_cvt_get_max_out_sample_num(handle, pcm.size(), &capacity) != ESP_AE_ERR_OK ||
            capacity > 32768) return false;
        std::vector<int16_t> output(capacity);
        if (esp_ae_rate_cvt_process(handle, pcm.data(), pcm.size(), output.data(), &capacity) != ESP_AE_ERR_OK) {
            return false;
        }
        output.resize(capacity);
        pcm.swap(output);
        return true;
    }
};
}  // namespace

SpeakerMusic& SpeakerMusic::Get() {
    static SpeakerMusic music;
    return music;
}

SpeakerMusic::Snapshot SpeakerMusic::GetSnapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void SpeakerMusic::EnsureTaskLocked() {
    if (!task_) {
        xTaskCreate([](void* context) { static_cast<SpeakerMusic*>(context)->Run(); },
                    "sd_music", 8192, this, 2, &task_);
    }
}

void SpeakerMusic::Refresh() {
    std::lock_guard lock(mutex_);
    if (generation_ || scan_) return;
    EnsureTaskLocked();
    if (!task_) {
        snapshot_.state = State::Failed;
        return;
    }
    scan_ = true;
    snapshot_.state = State::Scanning;
    xTaskNotifyGive(task_);
}

void SpeakerMusic::Select(int index) {
    std::lock_guard lock(mutex_);
    if (!snapshot_.tracks || index < 0 || static_cast<size_t>(index) >= snapshot_.tracks->size() ||
        Application::GetInstance().GetDeviceState() != kDeviceStateIdle || scan_ || !task_) return;
    generation_ = Audio().BeginLocalPlayback();
    pending_ = generation_;
    snapshot_.selected = index;
    snapshot_.state = State::Playing;
    xTaskNotifyGive(task_);
}

void SpeakerMusic::Toggle() {
    const auto view = GetSnapshot();
    if (view.state != State::Playing && view.state != State::Paused) {
        Select(view.selected < 0 ? 0 : view.selected);
        return;
    }
    std::lock_guard lock(mutex_);
    if (snapshot_.state == State::Playing) snapshot_.state = State::Paused;
    else if (snapshot_.state == State::Paused) snapshot_.state = State::Playing;
    if (task_) xTaskNotifyGive(task_);
}

void SpeakerMusic::Step(int offset) {
    const auto view = GetSnapshot();
    if (!view.tracks || view.tracks->empty()) return;
    const auto count = static_cast<int>(view.tracks->size());
    Select(view.selected < 0 ? 0 : (view.selected + offset + count) % count);
}

void SpeakerMusic::Stop() {
    std::lock_guard lock(mutex_);
    if (!generation_) return;
    Audio().CancelLocalPlayback();
    generation_ = pending_ = 0;
    snapshot_.state = State::Idle;
    if (task_) xTaskNotifyGive(task_);
}

void SpeakerMusic::Run() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        std::unique_lock lock(mutex_);
        if (scan_) {
            lock.unlock();
            auto& storage = SpeakerSdStorage::Get();
            const bool mounted = storage.Mount();
            auto tracks = mounted ? std::make_shared<Tracks>(storage.ListAudio()) : nullptr;
            lock.lock();
            snapshot_ = {mounted ? State::Idle : State::NoCard, std::move(tracks), -1};
            scan_ = false;
        }
        if (!pending_) continue;
        const uint32_t generation = pending_;
        pending_ = 0;
        const auto path = (*snapshot_.tracks)[snapshot_.selected];
        lock.unlock();
        const bool ok = Play(path, generation);
        const bool current = Audio().IsLocalPlaybackCurrent(generation);
        lock.lock();
        if (generation == generation_) {
            generation_ = 0;
            snapshot_.state = ok || !current ? State::Idle : State::Failed;
        }
    }
}

bool SpeakerMusic::WaitToPlay(uint32_t generation) {
    while (Audio().IsLocalPlaybackCurrent(generation)) {
        {
            std::lock_guard lock(mutex_);
            if (generation != generation_) return false;
            if (snapshot_.state != State::Paused) return true;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    return false;
}

bool SpeakerMusic::Play(const std::string& path, uint32_t generation) {
    auto close_file = [](FILE* file) { fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file(fopen(path.c_str(), "rb"), close_file);
    if (!file) return false;
    static const bool registered = [] {
        const auto mp3 = esp_mp3_dec_register();
        const auto pcm = esp_pcm_dec_register();
        const auto wav = esp_wav_dec_register();
        return mp3 == ESP_AUDIO_ERR_OK && pcm == ESP_AUDIO_ERR_OK && wav == ESP_AUDIO_ERR_OK;
    }();
    if (!registered) return false;

    const char extension = path[path.size() - 3];
    esp_audio_simple_dec_cfg_t config{};
    config.dec_type = extension == 'm' || extension == 'M' ? ESP_AUDIO_SIMPLE_DEC_TYPE_MP3
                                                          : ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    esp_audio_simple_dec_handle_t handle = nullptr;
    if (esp_audio_simple_dec_open(&config, &handle) != ESP_AUDIO_ERR_OK || !handle) return false;
    std::unique_ptr<void, decltype(&esp_audio_simple_dec_close)> decoder(handle, esp_audio_simple_dec_close);
    std::vector<uint8_t> input(4096);
    std::vector<int16_t> output(4096);
    Resampler resampler;
    esp_audio_simple_dec_info_t info{};
    bool heard_audio = false;
    const auto out_rate = static_cast<unsigned>(Audio().LocalOutputSampleRate());
    for (;;) {
        const auto read = fread(input.data(), 1, input.size(), file.get());
        if (read == 0) break;
        esp_audio_simple_dec_raw_t raw{};
        raw.buffer = input.data();
        raw.len = read;
        raw.eos = read < input.size();
        while (raw.len) {
            if (!WaitToPlay(generation)) return true;
            esp_audio_simple_dec_out_t decoded{};
            decoded.buffer = reinterpret_cast<uint8_t*>(output.data());
            decoded.len = output.size() * sizeof(int16_t);
            if (esp_audio_simple_dec_process(decoder.get(), &raw, &decoded) != ESP_AUDIO_ERR_OK ||
                raw.consumed == 0 || raw.consumed > raw.len || decoded.decoded_size > decoded.len) return false;
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
            if (!decoded.decoded_size) continue;
            if (!heard_audio) {
                if (esp_audio_simple_dec_get_info(decoder.get(), &info) != ESP_AUDIO_ERR_OK ||
                    info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) ||
                    info.sample_rate < 8000 || info.sample_rate > 96000 ||
                    !resampler.Open(info.sample_rate, out_rate)) return false;
                heard_audio = true;
            }
            const size_t frames = decoded.decoded_size / sizeof(int16_t) / info.channel;
            std::vector<int16_t> mono(frames);
            for (size_t i = 0; i < frames; ++i) {
                mono[i] = info.channel == 1 ? output[i] : (output[2 * i] + output[2 * i + 1]) / 2;
            }
            if (!resampler.Convert(mono)) return false;
            const size_t block = std::max<size_t>(1, out_rate / 50);
            for (size_t start = 0; start < mono.size(); start += block) {
                if (!WaitToPlay(generation)) return true;
                const auto end = std::min(start + block, mono.size());
                if (!Audio().QueueLocalPcm(generation, {mono.begin() + start, mono.begin() + end})) return true;
            }
        }
    }
    while (Audio().IsLocalPlaybackCurrent(generation) && !Audio().IsPlaybackIdle()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return heard_audio && !ferror(file.get());
}

#endif
