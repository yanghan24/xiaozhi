#include "account_protocol.h"
#include <cJSON.h>
#include <memory>
#include <cmath>
#include <algorithm>

namespace account {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json Parse(const std::string& text, size_t maximum = 65536) {
    if (text.empty() || text.size() > maximum || text.find('\0') != std::string::npos || text.find("\\u0000") != std::string::npos)
        return Json(nullptr, cJSON_Delete);
    return Json(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true), cJSON_Delete);
}
std::string Text(const cJSON* node, const char* name, size_t maximum = 2048) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return {};
    std::string text(value->valuestring);
    return text.size() <= maximum ? text : std::string();
}
bool Integer(const cJSON* node, const char* name, uint64_t& value) {
    const auto* number = cJSON_GetObjectItemCaseSensitive(node, name);
    if (!cJSON_IsNumber(number) || !std::isfinite(number->valuedouble) || number->valuedouble < 0
        || number->valuedouble > 9007199254740991.0 || std::floor(number->valuedouble) != number->valuedouble) return false;
    value = static_cast<uint64_t>(number->valuedouble);
    return true;
}
bool Characters(const std::string& value, const std::string& allowed, size_t minimum, size_t maximum) {
    return value.size() >= minimum && value.size() <= maximum && value.find_first_not_of(allowed) == std::string::npos;
}
bool SecureUrl(const std::string& value, const char* prefix) {
    if (value.rfind(prefix, 0) != 0 || value.size() > 2048 || value.find_first_of("\r\n\t @#\\") != std::string::npos) return false;
    auto host = value.substr(std::char_traits<char>::length(prefix));
    return !host.empty() && host[0] != '/' && host[0] != '?';
}
const std::string kAlpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
}
bool ParseIdentity(const std::string& text, const std::string& hardware_id, Identity& out) {
    out = {};
    auto root = Parse(text, 2048);
    uint64_t version = 0;
    if (!root || !Integer(root.get(), "version", version) || version != 1
        || Text(root.get(), "hardware_id", 12) != hardware_id || hardware_id.size() != 12) return false;
    Identity next;
    next.device_id = Text(root.get(), "device_id", 128);next.credential = Text(root.get(), "credential", 128);
    next.ota_url = Text(root.get(), "ota_url");next.origin = Text(root.get(), "control_origin");
    if (!Characters(next.device_id, kAlpha + "_.:-", 1, 128) || !Characters(next.credential, kAlpha + "_-", 43, 128)
        || !SecureUrl(next.origin, "https://") || next.origin.substr(8).find_first_of("/?") != std::string::npos
        || next.ota_url != next.origin + "/xiaozhi/ota/") return false;
    out = std::move(next);return true;
}
bool ParseState(const std::string& text, const std::string& device_id, uint64_t now, State& out) {
    out = {};
    auto root = Parse(text);uint64_t protocol = 0;
    if (!root || Text(root.get(), "device_id", 128) != device_id
        || !Integer(root.get(), "protocol_version", protocol) || protocol != 2) return false;
    State next;next.use_status = Text(root.get(), "use_status", 64);next.customer_name = Text(root.get(), "customer_name", 255);
    if (next.use_status.empty() || !Integer(root.get(), "permission_version", next.permission_version)) return false;
    if (next.use_status == "ready") {
        uint64_t lease = 0, server_time = 0, expiry = 0;
        auto* voice = cJSON_GetObjectItemCaseSensitive(root.get(), "voice");
        if (Text(root.get(), "binding_state") != "bound" || !Integer(root.get(), "lease_expires_in_ms", lease) || lease == 0 || lease > 60000
            || !Integer(root.get(), "server_time_ms", server_time) || !Integer(voice, "expires_at_ms", expiry) || expiry <= server_time
            || !Integer(voice, "protocol_version", protocol) || protocol != 1) return false;
        next.websocket_url = Text(voice, "url");next.websocket_token = Text(voice, "token", 128);
        if (!SecureUrl(next.websocket_url, "wss://") || !Characters(next.websocket_token, kAlpha + "_-", 43, 128)) return false;
        next.ready = true;next.valid_until_ms = now + std::min(lease, expiry - server_time);
    }
    out = std::move(next);return true;
}
bool ParseLogin(const std::string& text, uint64_t now, Login& out) {
    out = {};
    auto root = Parse(text);if (!root) return false;
    Login next;next.id = Text(root.get(), "id", 36);next.status = Text(root.get(), "status", 24);next.error = Text(root.get(), "error_code", 64);
    if (next.id.size() != 36 || !Characters(next.id, kAlpha + "-", 36, 36) || next.status.empty()) return false;
    if (next.status == "pending" || next.status == "polling") {
        uint64_t expires = 0, server_time = 0;
        if (!Integer(root.get(), "expires_at_ms", expires) || !Integer(root.get(), "server_time_ms", server_time) || expires <= server_time
            || expires - server_time > 600000) return false;
        next.verification_uri = Text(root.get(), "verification_uri");next.user_code = Text(root.get(), "user_code", 64);
        if (!SecureUrl(next.verification_uri, "https://") || next.user_code.empty()) return false;
        next.valid_until_ms = now + expires - server_time;
    }
    out = std::move(next);return true;
}
}
