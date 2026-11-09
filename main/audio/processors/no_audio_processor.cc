#include "no_audio_processor.h"
#include <esp_log.h>

#define TAG "NoAudioProcessor"

void NoAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    codec_ = codec;
    frame_samples_ = frame_duration_ms * 16000 / 1000;
}

void NoAudioProcessor::Feed(std::vector<int16_t>&& data, uint64_t generation) {
    if (codec_->input_channels() == 2) {
        for (size_t i = 0, j = 0; i < data.size() / 2; ++i, j += 2) {
            data[i] = data[j];
        }
        data.resize(data.size() / 2);
    }

    std::vector<std::vector<int16_t>> frames;
    {
        std::lock_guard<std::mutex> lock(output_buffer_mutex_);
        if (!is_running_ || !output_callback_ ||
            generation != output_buffer_.GetSourceGeneration()) {
            return;
        }
        const auto run = output_buffer_.GetGeneration();
        output_buffer_.Append(run, data.data(), data.size());
        std::vector<int16_t> frame;
        while (output_buffer_.PopFrame(run, frame_samples_, frame)) {
            frames.push_back(std::move(frame));
        }
    }
    for (auto& frame : frames) {
        output_callback_(std::move(frame), generation);
    }
}

void NoAudioProcessor::Start(uint64_t generation) {
    std::lock_guard<std::mutex> lock(output_buffer_mutex_);
    output_buffer_.Start(generation);
    is_running_ = true;
}

void NoAudioProcessor::Stop() {
    std::lock_guard<std::mutex> lock(output_buffer_mutex_);
    is_running_ = false;
    output_buffer_.Stop();
}

bool NoAudioProcessor::IsRunning() {
    return is_running_;
}

void NoAudioProcessor::OnOutput(
    std::function<void(std::vector<int16_t>&& data, uint64_t generation)> callback) {
    output_callback_ = callback;
}

void NoAudioProcessor::OnVadStateChange(
    std::function<void(bool speaking, uint64_t generation)> callback) {
    vad_state_change_callback_ = callback;
}

size_t NoAudioProcessor::GetFeedSize() {
    if (!codec_) {
        return 0;
    }
    return frame_samples_;
}

void NoAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
        ESP_LOGE(TAG, "Device AEC is not supported");
    }
}
