#ifndef DUMMY_AUDIO_PROCESSOR_H
#define DUMMY_AUDIO_PROCESSOR_H

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "audio_codec.h"
#include "audio_processor.h"
#include "processing_audio_buffer.h"

class NoAudioProcessor : public AudioProcessor {
public:
    NoAudioProcessor() = default;
    ~NoAudioProcessor() = default;

    void Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) override;
    void Feed(std::vector<int16_t>&& data, uint64_t generation) override;
    void Start(uint64_t generation) override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(
        std::function<void(std::vector<int16_t>&& data, uint64_t generation)> callback) override;
    void OnVadStateChange(
        std::function<void(bool speaking, uint64_t generation)> callback) override;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;

private:
    AudioCodec* codec_ = nullptr;
    int frame_samples_ = 0;
    ProcessingAudioBuffer output_buffer_;
    std::mutex output_buffer_mutex_;
    std::function<void(std::vector<int16_t>&& data, uint64_t generation)> output_callback_;
    std::function<void(bool speaking, uint64_t generation)> vad_state_change_callback_;
    std::atomic<bool> is_running_ = false;
};

#endif
