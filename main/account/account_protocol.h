#pragma once
#include <cstdint>
#include <string>

namespace account {
struct Identity {
    std::string device_id, credential, ota_url, origin;
};
struct State {
    std::string use_status, customer_name, websocket_url, websocket_token;
    uint64_t permission_version = 0;
    uint64_t valid_until_ms = 0;
    bool ready = false;
};
struct Login {
    std::string id, status, verification_uri, user_code, error;
    uint64_t valid_until_ms = 0;
};
// Parse only the public device identity protocol. OAuth tokens/device_code and
// model credentials never enter this module or the firmware.
bool ParseIdentity(const std::string& json, const std::string& hardware_id, Identity& out);
bool ParseState(const std::string& json, const std::string& device_id, uint64_t now_ms, State& out);
bool ParseLogin(const std::string& json, uint64_t now_ms, Login& out);
}
