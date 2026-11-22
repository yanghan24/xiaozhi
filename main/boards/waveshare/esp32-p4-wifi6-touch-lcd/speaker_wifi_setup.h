#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class SpeakerWifiSetup {
public:
    enum class Action { Inspect, OpenHotspot, CloseHotspot, Scan, Refresh, Connect };
    enum class Submission { Accepted, Busy, Unavailable };

    struct Request {
        Action action;
        std::string ssid;
        std::string password;
    };

    struct Network {
        std::string ssid;
        int rssi;
        bool secured;
        bool operator==(const Network&) const = default;
    };

    struct Result {
        Action action;
        std::string connected;
        std::string hotspot;
        std::string portal_url;
        std::vector<Network> networks;
        bool ok = false;
    };

    static SpeakerWifiSetup& Get();
    Submission Submit(Request&& request);
    std::optional<Result> TakeResult();

private:
    void Run();
    static Result Perform(const Request& request);

    TaskHandle_t task_ = nullptr;
    std::mutex mutex_;
    std::optional<Request> pending_;
    std::optional<Result> result_;
    bool busy_ = false;
};

#endif
