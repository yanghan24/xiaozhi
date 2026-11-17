#pragma once

#include <string>

namespace device_control {

struct IdentityConfig {
    std::string hardware_id;
    std::string device_id;
    std::string credential;
    std::string ota_url;
    std::string ota_origin;
    std::string control_origin;
};

// A complete private record, validated before any flash write or network access.
// Failure clears the output. Never put the input or the credential in a diagnostic.
bool ParseIdentityConfig(const std::string& json, const std::string& hardware_id,
                         IdentityConfig& config);

}  // namespace device_control
