#include "afe_audio_processor.h"
#include <esp_log.h>

#define PROCESSOR_RUNNING 0x01

#define TAG "AfeAudioProcessor"

AfeAudioProcessor::AfeAudioProcessor()
    : afe_data_(nullptr) {
    event_group_ = xEventGroupCreate();
}

void AfeAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    codec_ = codec;
    frame_samples_ = frame_duration_ms * 16000 / 1000;

    int ref_num = codec_->input_reference() ? 1 : 0;

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }

    srmodel_list_t *models;
    if (models_list == nullptr) {
        models = esp_srmodel_init("model");
    } else {
        models = models_list;
    }

    char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
    char* vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, NULL);
    
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), NULL, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_min_noise_ms = 100;
    if (vad_model_name != nullptr) {
        afe_config->vad_model_name = vad_model_name;
    }

    if (ns_model_name != nullptr) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
    } else {
        afe_config->ns_init = false;
    }

    afe_config->agc_init = false;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

#ifdef CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
    afe_config->vad_init = false;
#else
    afe_config->aec_init = false;
    afe_config->vad_init = true;
#endif

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    
    xTaskCreate([](void* arg) {
        auto this_ = (AfeAudioProcessor*)arg;
        this_->AudioProcessorTask();
        vTaskDelete(NULL);
    }, "audio_communication", 4096, this, 3, NULL);
}

AfeAudioProcessor::~AfeAudioProcessor() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    vEventGroupDelete(event_group_);
}

size_t AfeAudioProcessor::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void AfeAudioProcessor::Feed(std::vector<int16_t>&& data, uint64_t generation) {
    if (afe_data_ == nullptr) {
        return;
    }

    std::scoped_lock lock(output_buffer_mutex_, input_buffer_mutex_);
    // Check running state inside lock to avoid TOCTOU race with Stop()
    if (!IsRunning() || output_buffer_.GetSourceGeneration() != generation) {
        return;
    }
    input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
    size_t chunk_size = afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
    while (input_buffer_.size() >= chunk_size) {
        afe_iface_->feed(afe_data_, input_buffer_.data());
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunk_size);
    }
}

void AfeAudioProcessor::Start(uint64_t generation) {
    std::lock_guard<std::mutex> lock(output_buffer_mutex_);
    output_buffer_.Start(generation);
    is_speaking_ = false;
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    // Detach/reset the old run without waiting for callbacks or queue capacity.
    // Already detached callbacks carry the old permission generation explicitly.
    std::lock_guard<std::mutex> output_lock(output_buffer_mutex_);
    xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
    output_buffer_.Stop();
    is_speaking_ = false;

    std::lock_guard<std::mutex> lock(input_buffer_mutex_);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    input_buffer_.clear();
}

bool AfeAudioProcessor::IsRunning() {
    return xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING;
}

void AfeAudioProcessor::OnOutput(
    std::function<void(std::vector<int16_t>&& data, uint64_t generation)> callback) {
    output_callback_ = callback;
}

void AfeAudioProcessor::OnVadStateChange(
    std::function<void(bool speaking, uint64_t generation)> callback) {
    vad_state_change_callback_ = callback;
}

void AfeAudioProcessor::AudioProcessorTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio communication task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    while (true) {
        xEventGroupWaitBits(event_group_, PROCESSOR_RUNNING, pdFALSE, pdTRUE, portMAX_DELAY);

        uint64_t generation;
        {
            std::lock_guard<std::mutex> lock(output_buffer_mutex_);
            generation = output_buffer_.GetGeneration();
        }
        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        std::vector<std::vector<int16_t>> frames;
        uint64_t source_generation = 0;
        bool vad_changed = false;
        bool speaking = false;
        {
            std::lock_guard<std::mutex> output_lock(output_buffer_mutex_);
            if (generation == 0 || generation != output_buffer_.GetGeneration() ||
                (xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING) == 0) {
                continue;
            }
            if (res == nullptr || res->ret_value == ESP_FAIL) {
                if (res != nullptr) {
                    ESP_LOGI(TAG, "Error code: %d", res->ret_value);
                }
                continue;
            }

            if (res->vad_state == VAD_SPEECH && !is_speaking_) {
                is_speaking_ = true;
                vad_changed = true;
            } else if (res->vad_state == VAD_SILENCE && is_speaking_) {
                is_speaking_ = false;
                vad_changed = true;
            }
            speaking = is_speaking_;
            source_generation = output_buffer_.GetSourceGeneration();

            if (output_callback_) {
                output_buffer_.Append(generation, res->data, res->data_size / sizeof(int16_t));
                std::vector<int16_t> frame;
                while (output_buffer_.PopFrame(generation, frame_samples_, frame)) {
                    frames.push_back(std::move(frame));
                }
            }
        }

        // A full encode/send queue must never block Stop() from resetting the
        // processor. Source metadata makes late delivery safe after unlocking.
        if (vad_changed && vad_state_change_callback_) {
            vad_state_change_callback_(speaking, source_generation);
        }
        for (auto& frame : frames) {
            output_callback_(std::move(frame), source_generation);
        }
    }
}

void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        afe_iface_->disable_vad(afe_data_);
        afe_iface_->enable_aec(afe_data_);
#else
        ESP_LOGE(TAG, "Device AEC is not supported");
#endif
    } else {
        afe_iface_->disable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
    }
}
