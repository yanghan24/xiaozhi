#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct DeviceControlView {
    bool voice_allowed = false;
    uint64_t revision = 0;
    uint64_t permission_version = 0;
    uint64_t generation = 0;
    std::string reason = "not_configured";
    std::string verification_uri;
    std::string purpose;
    int64_t qr_deadline_ms = 0;
};

struct DeviceVoiceConfig {
    std::string url;
    std::string token;
    std::string device_id;
    int protocol_version = 1;
    uint64_t permission_version = 0;
    uint64_t generation = 0;
    int64_t handshake_deadline_ms = 0;
};

// All public methods are thread safe. Callbacks never perform network requests.
class DeviceControl {
public:
    static DeviceControl& GetInstance();
    bool Enabled() const;
    void SetCallbacks(std::function<void(const DeviceControlView&)> on_changed,
                      std::function<void(uint64_t)> on_revoked);
    void Start(const std::string& trusted_ota_url, const std::string& discovered_base_url);
    void NotifyNetwork(bool connected);
    bool IsVoiceAllowed() const;
    bool IsCurrentGeneration(uint64_t generation) const;
    uint64_t GetGeneration() const;
    bool GetVoiceConfig(DeviceVoiceConfig& config);
    DeviceControlView GetView() const;
    void Refresh();
    void RequestAccount();
    void AcknowledgeState(uint64_t revision, uint64_t permission_version, bool audio_stopped);

    DeviceControl(const DeviceControl&) = delete;
    DeviceControl& operator=(const DeviceControl&) = delete;

private:
    DeviceControl();
    ~DeviceControl();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
