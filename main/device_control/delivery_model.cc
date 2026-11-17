#include "delivery_model.h"
#include <cJSON.h>
#include <cmath>
#include <memory>
#include <set>

namespace device_control {
namespace {
bool Fields(const cJSON* value, std::set<std::string> expected) {
    if (!cJSON_IsObject(value))
        return false;
    for (auto field = value->child; field; field = field->next) {
        if (!field->string || expected.erase(field->string) != 1)
            return false;
    }
    return expected.empty();
}
bool Text(const cJSON* value, const char* field, const std::string& expected) {
    auto item = cJSON_GetObjectItemCaseSensitive(value, field);
    return cJSON_IsString(item) && item->valuestring && expected == item->valuestring;
}
}  // namespace

DeliveryStatus ParseDeliveryCheck(const std::string& body, const std::string& device_id) {
    if (body.size() > 4096 || body.find('\0') != std::string::npos ||
        body.find("\\u0000") != std::string::npos)
        return DeliveryStatus::Invalid;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, nullptr, true), cJSON_Delete);
    if (!Fields(root.get(), {"version", "code", "data"}) || !Text(root.get(), "code", "OK"))
        return DeliveryStatus::Invalid;
    auto version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    auto data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 ||
        !Fields(data, {"device_id", "status", "verified", "server_time_ms"}) ||
        !Text(data, "device_id", device_id) ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data, "verified")))
        return DeliveryStatus::Invalid;
    auto time = cJSON_GetObjectItemCaseSensitive(data, "server_time_ms");
    if (!cJSON_IsNumber(time) || !std::isfinite(time->valuedouble) || time->valuedouble < 0 ||
        time->valuedouble > 9007199254740991.0 ||
        std::floor(time->valuedouble) != time->valuedouble)
        return DeliveryStatus::Invalid;
    if (Text(data, "status", "pending"))
        return DeliveryStatus::Pending;
    if (Text(data, "status", "delivered"))
        return DeliveryStatus::Delivered;
    return DeliveryStatus::Invalid;
}
}  // namespace device_control
