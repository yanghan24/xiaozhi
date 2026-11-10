#include "identity_config.h"

#include <cJSON.h>
#include <algorithm>
#include <memory>
#include <set>
#include "control_model.h"

namespace device_control {
bool ParseIdentityConfig(const std::string& json, const std::string& hardware_id,
                         IdentityConfig& config) {
    config = {};
    if (json.empty() || json.size() > 2048 || json.find('\0') != std::string::npos ||
        json.find("\\u0000") != std::string::npos)
        return false;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(json.c_str(), json.size() + 1, nullptr, true), cJSON_Delete);
    if (!cJSON_IsObject(root.get()))
        return false;
    std::set<std::string> fields = {"version",    "hardware_id", "device_id",
                                    "credential", "ota_url",     "control_origin"};
    for (const auto* field = root->child; field; field = field->next)
        if (!field->string || fields.erase(field->string) != 1)
            return false;
    const auto* version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    if (!fields.empty() || !cJSON_IsNumber(version) || version->valuedouble != 1)
        return false;
    IdentityConfig next;
    auto text = [&](const char* key, std::string& value) {
        const auto* item = cJSON_GetObjectItemCaseSensitive(root.get(), key);
        if (!cJSON_IsString(item) || item->valuestring == nullptr)
            return false;
        value = item->valuestring;
        return true;
    };
    if (!text("hardware_id", next.hardware_id) || !text("device_id", next.device_id) ||
        !text("credential", next.credential) || !text("ota_url", next.ota_url) ||
        !text("control_origin", next.control_origin))
        return false;
    auto all = [](const std::string& value, const std::string& alphabet) {
        return value.find_first_not_of(alphabet) == std::string::npos;
    };
    const std::string alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    if (next.hardware_id.size() != 12 || !all(next.hardware_id, "0123456789abcdef") ||
        next.hardware_id != hardware_id || next.device_id.empty() || next.device_id.size() > 128 ||
        !all(next.device_id, alpha + "_.:-") || next.credential.size() < 43 ||
        next.credential.size() > 128 || !all(next.credential, alpha + "_-"))
        return false;
    Url ota, control;
    if (!ParseUrl(next.ota_url, ota) || ota.scheme != "https" || ota.target != "/xiaozhi/ota/")
        return false;
    if (!next.control_origin.empty() && (!ParseUrl(next.control_origin, control) ||
                                         control.scheme != "https" || control.target != "/"))
        return false;
    next.ota_origin = ota.Origin();
    if (!next.control_origin.empty())
        next.control_origin = control.Origin();
    config = std::move(next);
    return true;
}
}  // namespace device_control
