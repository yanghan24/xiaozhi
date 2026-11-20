#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class SpeakerMusic {
public:
    using Tracks = std::vector<std::string>;
    enum class State { Idle, Scanning, NoCard, Playing, Paused, Failed };
    struct Snapshot {
        State state = State::Idle;
        std::shared_ptr<const Tracks> tracks;
        int selected = -1;
    };

    static SpeakerMusic& Get();
    Snapshot GetSnapshot() const;
    void Refresh();
    void Select(int index);
    void Toggle();
    void Step(int offset);
    void Stop();

private:
    void Run();
    bool Play(const std::string& path, uint32_t generation);
    bool WaitToPlay(uint32_t generation);
    void EnsureTaskLocked();

    mutable std::mutex mutex_;
    TaskHandle_t task_ = nullptr;
    Snapshot snapshot_;
    bool scan_ = false;
    uint32_t generation_ = 0;
    uint32_t pending_ = 0;
};

#endif
