#include "product_weather.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <cJSON.h>

namespace product_weather {
namespace {
std::string Text(const cJSON* object, const char* key) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}

std::string Number(const cJSON* object, const char* key, double min, double max) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < min || item->valuedouble > max)
        return {};
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%.1f", item->valuedouble);
    std::string value(buffer);
    if (value.size() >= 2 && value.compare(value.size() - 2, 2, ".0") == 0)
        value.resize(value.size() - 2);
    return value;
}

bool HasReading(const std::string& status) { return status == "fresh" || status == "stale"; }

std::string Encode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
    }
    return result;
}
}  // namespace

std::string CityQuery(const std::string& city, const std::string& province) {
    return "?city=" + Encode(city) + (province.empty() ? "" : "&province=" + Encode(province));
}

bool Parse(const std::string& body, EnvironmentSnapshot& out, std::string& candidates) {
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_Parse(body.c_str()), cJSON_Delete);
    const auto* version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    const auto* data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || Text(root.get(), "code") != "OK" ||
        !cJSON_IsObject(data))
        return false;
    EnvironmentSnapshot next;
    next.configured = true;
    next.location_status = Text(data, "location_status");
    next.location_source = Text(data, "location_source");
    candidates.clear();
    if (next.location_status == "ambiguous") {
        const auto* list = cJSON_GetObjectItemCaseSensitive(data, "candidates");
        const cJSON* candidate;
        cJSON_ArrayForEach(candidate, list) {
            if (candidates.size() > 512)
                break;
            if (!candidates.empty()) candidates += ", ";
            candidates += Text(candidate, "province") + " " + Text(candidate, "name") +
                          " [" + Text(candidate, "id") + "]";
        }
    } else if (next.location_status == "found") {
        const auto* city = cJSON_GetObjectItemCaseSensitive(data, "city");
        next.city_id = Text(city, "id");
        next.city = Text(city, "name");
        if (next.city.empty() || next.city_id.empty() || next.city_id.size() > 16 ||
            next.city_id.find_first_not_of("0123456789") != std::string::npos)
            return false;
        next.location_available = true;
        const auto* weather = cJSON_GetObjectItemCaseSensitive(data, "weather");
        next.weather_status = Text(weather, "status");
        if (HasReading(next.weather_status)) {
            next.weather_valid_at = Text(weather, "valid_at");
            next.temperature_c = Number(weather, "temperature_c", -100, 100);
            next.humidity = Number(weather, "humidity_pct", 0, 100);
            next.weather_text = Text(weather, "description");
            const auto* code = cJSON_GetObjectItemCaseSensitive(weather, "weather_code");
            if (cJSON_IsNumber(code) && code->valueint == code->valuedouble &&
                code->valueint >= 0 && code->valueint <= 99)
                next.weather_code = code->valueint;
            next.weather_available = !next.weather_valid_at.empty() &&
                (!next.temperature_c.empty() || !next.humidity.empty() || !next.weather_text.empty() ||
                 next.weather_code >= 0);
            if (!next.weather_available) {
                next.temperature_c.clear();
                next.humidity.clear();
                next.weather_text.clear();
            }
        }
        const auto* air = cJSON_GetObjectItemCaseSensitive(data, "air_quality");
        next.air_status = Text(air, "status");
        if (HasReading(next.air_status) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(air, "model_estimate")) &&
            Text(air, "unit") == "μg/m³") {
            next.air_valid_at = Text(air, "valid_at");
            next.air_model = Text(air, "model");
            if (!next.air_valid_at.empty() && !next.air_model.empty()) {
                next.pm25_text = Number(air, "pm25", 0, 10000);
                next.pm10_text = Number(air, "pm10", 0, 10000);
                next.air_available = !next.pm25_text.empty() || !next.pm10_text.empty();
            }
        }
        next.stale = next.weather_status != "fresh" || next.air_status != "fresh";
    } else if (next.location_status != "unknown") {
        return false;
    }
    out = std::move(next);
    return true;
}

void RetainUnavailable(std::string& response, const std::string& previous) {
    if (previous.empty()) return;
    using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
    Json fresh(cJSON_Parse(response.c_str()), cJSON_Delete);
    Json cached(cJSON_Parse(previous.c_str()), cJSON_Delete);
    auto* data = cJSON_GetObjectItemCaseSensitive(fresh.get(), "data");
    const auto* old = cJSON_GetObjectItemCaseSensitive(cached.get(), "data");
    auto city_id = [](const cJSON* item) { return Text(cJSON_GetObjectItemCaseSensitive(item, "city"), "id"); };
    if (Text(data, "location_status") != "found" || city_id(data) != city_id(old) ||
        Text(data, "location_source") != Text(old, "location_source"))
        return;
    bool changed = false;
    for (const auto* key : {"weather", "air_quality"}) {
        const auto* current = cJSON_GetObjectItemCaseSensitive(data, key);
        const auto* saved = cJSON_GetObjectItemCaseSensitive(old, key);
        const auto status = Text(current, "status");
        if ((status != "loading" && status != "unavailable") || !HasReading(Text(saved, "status")) ||
            Text(saved, "valid_at").empty())
            continue;
        Json copy(cJSON_Duplicate(saved, true), cJSON_Delete);
        if (copy && cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(copy.get(), "status"), "stale") &&
            cJSON_ReplaceItemInObjectCaseSensitive(data, key, copy.get())) {
            copy.release();
            changed = true;
        }
    }
    if (changed) {
        char* body = cJSON_PrintUnformatted(fresh.get());
        if (body) {
            response = body;
            cJSON_free(body);
        }
    }
}

void MarkCached(EnvironmentSnapshot& snapshot) {
    snapshot.stale = true;
    if (snapshot.weather_available) snapshot.weather_status = "stale";
    if (snapshot.air_available) snapshot.air_status = "stale";
}
}  // namespace product_weather
