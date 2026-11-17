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
bool ParseAccountDetails(const std::string& json, const std::string& device_id,
                         uint64_t permission_version, std::string& username, std::string& balance);
bool ValidateSignOutResponse(const std::string& json, uint64_t expected_permission_version);
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

// Keep a stopped-state acknowledgement across response loss and QR-only revisions.
class PendingAck {
public:
    void Set(uint64_t revision, uint64_t permission, bool audio_stopped);
    void Rebase(const DeviceControlView& view);
    void Confirm(uint64_t revision, uint64_t permission, bool audio_stopped);
    void Clear() { pending_ = false; }
    bool Pending() const { return pending_; }
    uint64_t Revision() const { return revision_; }
    uint64_t Permission() const { return permission_; }
    bool AudioStopped() const { return audio_stopped_; }

private:
    bool pending_ = false;
    uint64_t revision_ = 0;
    uint64_t permission_ = 0;
    bool audio_stopped_ = false;
};

// Keep the original permission version across timeouts. Only a fresh state can
// confirm unbinding or that a later permission superseded the request. Rejection
// keeps the local audio stop until explicit retry or a subsequent permission.
class SignOutRequest {
public:
    bool Begin(uint64_t permission);
    void Reject(DeviceSignOutStatus status);
    bool Observe(const DeviceControlView& state);
    bool Pending() const { return expected_ != 0; }
    bool CanSend() const { return Pending() && rejection_ == DeviceSignOutStatus::Idle; }
    uint64_t Expected() const { return expected_; }
    DeviceSignOutStatus Status() const {
        if (!Pending())
            return status_;
        return rejection_ == DeviceSignOutStatus::Idle ? DeviceSignOutStatus::Pending : rejection_;
    }

private:
    uint64_t expected_ = 0;
    DeviceSignOutStatus rejection_ = DeviceSignOutStatus::Idle;
    DeviceSignOutStatus status_ = DeviceSignOutStatus::Idle;
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
