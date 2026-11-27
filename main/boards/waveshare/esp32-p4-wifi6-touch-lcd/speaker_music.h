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
    enum class State { Idle, Scanning, NoCard, Playing, Paused, Failed, Deleting, DeleteFailed };
    struct Snapshot {
        State state = State::Idle;
        std::shared_ptr<const Tracks> tracks;
        int selected = -1;
        bool UpdatingFiles() const { return state == State::Scanning || state == State::Deleting; }
    };

    static SpeakerMusic& Get();
    Snapshot GetSnapshot() const;
    void Refresh();
    void Select(const std::shared_ptr<const Tracks>& tracks, int index);
    void Toggle();
    void Step(int offset);
    void Stop();
    void DeleteSelected(const std::shared_ptr<const Tracks>& tracks, int selected);

private:
    void Run();
    bool Play(const std::string& path, uint32_t generation);
    bool WaitToPlay(uint32_t generation);
    void EnsureTaskLocked();
    void StopPlaybackLocked();

    mutable std::mutex mutex_;
    TaskHandle_t task_ = nullptr;
    Snapshot snapshot_;
    uint32_t generation_ = 0;
    uint32_t pending_ = 0;
};

#endif
