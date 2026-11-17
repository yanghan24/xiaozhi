#pragma once
#include "control_model.h"

namespace device_control {
// Device-facing v2 only. OAuth polling and customer credentials stay on the server.
class SpeakerModel {
public:
    explicit SpeakerModel(std::string id = {}, std::string = {}) : id_(std::move(id)) {}
    ApplyResult State(const std::string& json, int64_t now, int64_t elapsed);
    ApplyResult Login(const std::string& json, int64_t now, int64_t elapsed);
    bool Tick(int64_t now);
    bool Revoke(const std::string& reason);
    void AdvanceGeneration(uint64_t floor = 0);
    void RetryLogin();
    bool DiscardRequest() { RetryLogin(); return true; }
    bool Allowed(int64_t now) const;
    bool Voice(int64_t now, DeviceVoiceConfig& out) const;
    bool CanLogin() const;
    DeviceControlView View() const { return view_; }
    std::string LoginId() const { return login_id_; }
    std::string CustomerName() const { return customer_name_; }
    uint64_t BindingVersion() const { return binding_; }
private:
    std::string id_, login_id_, customer_name_;
    DeviceControlView view_;
    DeviceVoiceConfig voice_;
    uint64_t binding_ = 0;
    int64_t server_ms_ = 0, deadline_ = 0;
    bool terminal_ = false;
    void ClearQr();
};
}
