#include "environment_service.h"

#include "device_control/device_control.h"
#include "product_weather.h"
#include <esp_random.h>
#include <esp_timer.h>
#include <nvs.h>

// A cache write must never prevent the speaker from running when user NVS is full.
namespace {
constexpr char kNamespace[] = "p4_env";
constexpr int64_t kSecond = 1000000;

std::string ReadString(nvs_handle_t handle, const char* key) {
    size_t length = 0;
    if (nvs_get_str(handle, key, nullptr, &length) != ESP_OK || !length || length > 3800)
        return {};
    std::string value(length, '\0');
    if (nvs_get_str(handle, key, value.data(), &length) != ESP_OK)
        return {};
    value.resize(length - 1);
    return value;
}

bool Persist(const char* key, const std::string& value) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto result = value.empty() ? nvs_erase_key(handle, key) : nvs_set_str(handle, key, value.c_str());
    if (result == ESP_ERR_NVS_NOT_FOUND && value.empty())
        result = ESP_OK;
    bool ok = result == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}
}

EnvironmentService::EnvironmentService() {
    snapshot_.configured = true;
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
        manual_city_id_ = ReadString(handle, "manual_city");
        std::string cached = ReadString(handle, "response");
        nvs_close(handle);
        EnvironmentSnapshot saved;
        std::string unused;
        if (product_weather::Parse(cached, saved, unused) && saved.location_available &&
            ((manual_city_id_.empty() && saved.location_source == "ip") ||
             (!manual_city_id_.empty() && saved.city_id == manual_city_id_ && saved.location_source == "manual"))) {
            product_weather::MarkCached(saved);
            snapshot_ = std::move(saved);
            cached_response_ = std::move(cached);
        }
    }
    // Spread first requests across wakeups, including a simultaneous power cycle.
    next_refresh_us_ = esp_timer_get_time() + (1 + esp_random() % 15) * kSecond;
}

EnvironmentSnapshot EnvironmentService::GetSnapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto view = snapshot_;
    if (!DeviceControl::GetInstance().IsNetworkConnected()) product_weather::MarkCached(view);
    return view;
}

std::string EnvironmentService::SelectCity(const std::string& city, const std::string& province) {
    if (city.empty() || city.size() > 96 || province.size() > 96)
        return "Provide a city or 'auto' to restore IP location.";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (city == "auto") {
            if (!Persist("manual_city", "")) return "Could not save automatic location.";
            manual_city_id_.clear();
            selection_query_.clear();
            // A previous manual reading must never masquerade as the IP location.
            snapshot_ = {};
            snapshot_.configured = true;
            selection_status_ = "Automatic location enabled.";
        } else {
            const bool numeric = city.find_first_not_of("0123456789") == std::string::npos;
            if (numeric && (city.size() > 10 || city.front() == '0'))
                return "Invalid GeoNames city ID.";
            selection_query_ = numeric ? "?city_id=" + city : product_weather::CityQuery(city, province);
            selection_status_ = "Looking up city. Call get_status for the result before confirming.";
        }
        ++selection_revision_;
        next_refresh_us_ = 0;
    }
    RefreshAsync(true);
    return CitySelectionStatus();
}

std::string EnvironmentService::CitySelectionStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    return selection_status_;
}

bool EnvironmentService::ShouldRefreshLocked(int64_t now_us, bool force) const {
    // Wait for device-control discovery and stagger ordinary startup refreshes.
    return DeviceControl::GetInstance().IsNetworkConnected() &&
           (now_us >= next_refresh_us_ || (force && last_refresh_attempt_us_ != 0));
}

esp_err_t EnvironmentService::RefreshNow() {
    std::string query, manual_id, previous;
    uint32_t revision;
    bool selecting;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        revision = selection_revision_;
        selecting = !selection_query_.empty();
        manual_id = manual_city_id_;
        previous = cached_response_;
        query = selecting ? selection_query_ : manual_id.empty() ? "" : "?city_id=" + manual_id;
    }
    std::string response, candidates;
    EnvironmentSnapshot next;
    bool ok = DeviceControl::GetInstance().GetEnvironment(query, response);
    if (ok) {
        product_weather::RetainUnavailable(response, previous);
        ok = product_weather::Parse(response, next, candidates);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (revision != selection_revision_) {
        next_refresh_us_ = 0;
        return ESP_ERR_INVALID_STATE;
    }

    // A failed lookup leaves the previous confirmed location intact.
    if (ok && next.location_available) {
        bool manual = selecting || !manual_id.empty();
        ok = next.location_source == (manual ? "manual" : "ip") &&
             (selecting || manual_id.empty() || next.city_id == manual_id);
    }
    int retry_seconds = 45;
    if (!ok) {
        product_weather::MarkCached(snapshot_);
        snapshot_.last_error = "Environment service unavailable";
        if (selecting) selection_status_ = "City lookup failed. Please retry.";
    } else if (selecting && !next.location_available) {
        selection_status_ = next.location_status == "ambiguous"
            ? "Ask which province or city was intended and retry with the province: " + candidates
            : "City not found. Ask for the province and city.";
    } else if (selecting && !Persist("manual_city", next.city_id)) {
        selection_status_ = "Could not save city.";
    } else {
        if (selecting) {
            manual_city_id_ = next.city_id;
            selection_status_ = "Saved city: " + next.city;
        }
        snapshot_ = std::move(next);
        if (snapshot_.location_available && response != cached_response_) {
            cached_response_ = std::move(response);
            if (cached_response_.size() >= 3800 || !Persist("response", cached_response_))
                snapshot_.last_error = "Offline cache could not be updated";
        }
        if (snapshot_.weather_status == "fresh" && snapshot_.air_status == "fresh")
            retry_seconds = 30 * 60;
    }
    selection_query_.clear();
    next_refresh_us_ = esp_timer_get_time() + (retry_seconds + esp_random() % 31) * kSecond;
    return ok ? ESP_OK : ESP_FAIL;
}
