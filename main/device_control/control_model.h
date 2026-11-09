#pragma once

#include <cstdint>
#include <string>
#include "device_control.h"

namespace device_control {

struct Url {
    std::string scheme;
    std::string host;
    std::string authority;
    std::string target;
    int port = 0;
    std::string Origin() const;
};

bool ParseUrl(const std::string& text, Url& url);
bool IsTrustedOtaUrl(const std::string& url, const std::string& ota_origin);
bool TrustedDiscovery(const std::string& ota_url, const std::string& base_url,
                      const std::string& ota_origin, const std::string& control_origin);
bool HeaderValue(const std::string& value);
bool ValidateAckResponse(const std::string& json, uint64_t revision, uint64_t permission_version);
int64_t RetryDelayMs(const std::string& retry_after, const std::string& json,
                     int64_t trusted_unix_ms, const std::string& response_date = {});

enum class ApplyResult { Applied, Stale, Invalid };

class PendingCreate {
public:
    std::string Select(const std::string& intent, uint64_t permission_version,
                       const std::string& fresh_key);
    void Complete(int http_status);
    void Clear() { key_.clear(); }

private:
    std::string intent_;
    std::string key_;
    uint64_t permission_version_ = 0;
};

// Portable protocol boundary: responses and monotonic time in; UI/voice permission out.
class Model {
public:
    Model(std::string device_id, std::string control_origin);
    ApplyResult ApplyState(const std::string& json, int64_t received_ms, int64_t elapsed_ms);
    ApplyResult ApplyRequest(const std::string& json, int64_t received_ms, int64_t elapsed_ms);
    bool Tick(int64_t now_ms);
    bool DiscardRequest();
    bool Revoke(const std::string& reason);
    void AdvanceGeneration(uint64_t floor = 0);
    bool Allowed(int64_t now_ms) const;
    bool Voice(int64_t now_ms, DeviceVoiceConfig& out) const;
    DeviceControlView View() const { return view_; }
    int64_t LeaseDeadline() const { return lease_deadline_ms_; }
    std::string AutoIntent() const;
    bool HasServerRequest() const { return server_request_active_; }
    int64_t EstimatedServerTime(int64_t now) const {
        return have_state_ ? last_server_ms_ + now - last_received_ms_ : 0;
    }

private:
    std::string device_id_;
    std::string origin_;
    DeviceControlView view_;
    DeviceVoiceConfig voice_;
    std::string stable_;
    std::string permission_stable_;
    int64_t last_server_ms_ = 0;
    int64_t last_received_ms_ = 0;
    int64_t lease_deadline_ms_ = 0;
    bool have_state_ = false;
    bool server_request_active_ = false;
    std::string known_request_id_;
    std::string discarded_request_id_;
    int64_t known_request_deadline_ms_ = 0;
};

}  // namespace device_control
