#ifndef AFE_AUDIO_PROCESSOR_H
#define AFE_AUDIO_PROCESSOR_H

#include <esp_afe_sr_models.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <string>
#include <vector>
#include <functional>
#include <mutex>

#include "audio_codec.h"
#include "audio_processor.h"
#include "processing_audio_buffer.h"

class AfeAudioProcessor : public AudioProcessor {
public:
    AfeAudioProcessor();
    ~AfeAudioProcessor();

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
    EventGroupHandle_t event_group_ = nullptr;
    const esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    std::function<void(std::vector<int16_t>&& data, uint64_t generation)> output_callback_;
    std::function<void(bool speaking, uint64_t generation)> vad_state_change_callback_;
    AudioCodec* codec_ = nullptr;
    int frame_samples_ = 0;
    bool is_speaking_ = false;
    std::vector<int16_t> input_buffer_;
    std::mutex input_buffer_mutex_;
    std::mutex output_buffer_mutex_;
    ProcessingAudioBuffer output_buffer_;

    void AudioProcessorTask();
};

#endif
