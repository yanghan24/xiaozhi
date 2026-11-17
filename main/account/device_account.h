#pragma once
#include "account_protocol.h"
#include <atomic>
#include <mutex>
#include <string>
#include <esp_err.h>

namespace account {
class DeviceAccount {
public:
    static DeviceAccount& GetInstance() { static DeviceAccount instance; return instance; }
    void NetworkChanged(bool connected);
    bool Ready();
    bool Connection(std::string& url, std::string& token);
    const std::string& DeviceId() const { return identity_.device_id; }
    const std::string& Credential() const { return identity_.credential; }
    const std::string& OtaUrl() const { return identity_.ota_url; }
    static const char* BoardType() { return "waveshare-esp32-p4-wifi6-touch-lcd-4.3"; }
    void RetryLogin();
    void RequestUnbind();
    void ShowAccount();
    static esp_err_t ResetUserSettings();
private:
    DeviceAccount() = default;
    bool LoadIdentity();
    void Worker();
    int Request(const char* method, const std::string& path, const std::string& body, std::string& response);
    void Apply(const State& state);
    void Render();
    Identity identity_;
    State state_;
    Login login_;
    std::string message_;
    std::string pending_unbind_;
    uint64_t pending_version_ = 0;
    std::mutex mutex_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> retry_{false};
    std::atomic<bool> unbind_{false};
    bool started_ = false;
    bool initialized_ = false;
};
}
