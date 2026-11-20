#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>

class SpeakerRecorder {
public:
    enum class State { Idle, Starting, Recording, Saving, Saved, NoCard, Failed };
    struct Snapshot {
        State state = State::Idle;
        uint32_t seconds = 0;
        std::string file;
    };

    static SpeakerRecorder& Get();
    Snapshot GetSnapshot() const;
    void Start();
    void Stop();

private:
    struct Frame {
        std::array<int16_t, 384> pcm;
        uint16_t samples;
    };
    void Run();
    void Record();
    void Finish(State state, uint32_t seconds, std::string filename = {});
    void Push(const int16_t* pcm, size_t size, int channels);

    mutable std::mutex mutex_;
    Snapshot snapshot_;
    TaskHandle_t task_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    std::atomic<bool> accepting_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> overflow_{false};
};

#endif
