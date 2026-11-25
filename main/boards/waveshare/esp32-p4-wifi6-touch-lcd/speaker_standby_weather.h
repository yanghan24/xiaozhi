#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <string>

class SpeakerStandbyWeather {
public:
    enum class State { Loading, Ready, Unavailable, Offline };
    struct Snapshot {
        State state = State::Loading;
        std::string city;
        std::string condition;
        std::string temperature;
        std::string humidity;
        std::string wind_direction;
        std::string wind_power;
        int aqi = -1;  // Optional in the weather response.

        bool operator==(const Snapshot&) const = default;
    };

    static SpeakerStandbyWeather& Get();
    void Start();
    Snapshot GetSnapshot() const;

private:
    void Run();
    mutable std::mutex mutex_;
    TaskHandle_t task_ = nullptr;
    Snapshot snapshot_;
};

#endif
