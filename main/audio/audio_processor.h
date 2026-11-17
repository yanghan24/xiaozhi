#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <model_path.h>
#include "audio_codec.h"

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    
    virtual void Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) = 0;
    virtual void Feed(std::vector<int16_t>&& data, uint64_t generation) = 0;
    virtual void Start(uint64_t generation) = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() = 0;
    virtual void OnOutput(
        std::function<void(std::vector<int16_t>&& data, uint64_t generation)> callback) = 0;
    virtual void OnVadStateChange(
        std::function<void(bool speaking, uint64_t generation)> callback) = 0;
    virtual size_t GetFeedSize() = 0;
    virtual void EnableDeviceAec(bool enable) = 0;
};

#endif
