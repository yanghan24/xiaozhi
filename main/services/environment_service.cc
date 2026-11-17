#include "environment_service.h"

#include "board.h"
#include "settings.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <sdkconfig.h>

#define TAG "EnvironmentService"

namespace {
constexpr const char* kSettingsNamespace = "env";
constexpr const char* kAmapKey = "amap_key";
constexpr const char* kAggregateUrlKey = "aqi_url";
constexpr const char* kRefreshMinutesKey = "refresh_min";
constexpr const char* kFallbackAdcodeKey = "fb_adcode";
constexpr const char* kFallbackCityKey = "fb_city";
constexpr const char* kCacheKey = "cache";
constexpr int kDefaultRefreshMinutes = 30;
constexpr int kMinRefreshMinutes = 5;
constexpr int kMaxRefreshMinutes = 24 * 60;
constexpr int64_t kUsPerMinute = 60LL * 1000000LL;

const char* JsonString(cJSON* object, const char* key) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

int JsonInt(cJSON* object, const char* key, int default_value = -1) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    if (cJSON_IsString(item)) {
        return atoi(item->valuestring);
    }
    return default_value;
}

bool IsAmapOk(cJSON* root) {
    cJSON* status = cJSON_GetObjectItem(root, "status");
    return cJSON_IsString(status) && strcmp(status->valuestring, "1") == 0;
}

std::string AppendQueryParam(const std::string& url, const std::string& key, const std::string& value) {
    if (url.empty()) {
        return url;
    }
    char separator = url.find('?') == std::string::npos ? '?' : '&';
    return url + separator + key + "=" + value;
}
} // namespace

EnvironmentService& EnvironmentService::GetInstance() {
    static EnvironmentService instance;
    return instance;
}

EnvironmentService::EnvironmentService() {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadConfigLocked();
    LoadCacheLocked();
}

EnvironmentConfig EnvironmentService::GetConfig() {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadConfigLocked();
    return config_;
}

void EnvironmentService::SaveConfig(const EnvironmentConfig& config) {
    EnvironmentConfig normalized = config;
    normalized.refresh_interval_minutes = std::clamp(
        normalized.refresh_interval_minutes, kMinRefreshMinutes, kMaxRefreshMinutes);

    {
        Settings settings(kSettingsNamespace, true);
        settings.SetString(kAmapKey, normalized.amap_api_key);
        settings.SetString(kAggregateUrlKey, normalized.aggregate_url);
        settings.SetInt(kRefreshMinutesKey, normalized.refresh_interval_minutes);
        settings.SetString(kFallbackAdcodeKey, normalized.fallback_adcode);
        settings.SetString(kFallbackCityKey, normalized.fallback_city);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    config_ = normalized;
    config_loaded_ = true;
    snapshot_.configured = !config_.amap_api_key.empty();
    snapshot_.last_error.clear();
    ESP_LOGI(TAG, "Environment config saved, key=%s, aggregate=%s, refresh=%d min",
        config_.amap_api_key.empty() ? "empty" : "set",
        config_.aggregate_url.empty() ? "empty" : "set",
        config_.refresh_interval_minutes);
}

void EnvironmentService::RefreshAsync(bool force) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t now_us = esp_timer_get_time();
    if (refresh_task_handle_ != nullptr || !ShouldRefreshLocked(now_us, force)) {
        return;
    }

    last_refresh_attempt_us_ = now_us;
    snapshot_.refresh_in_progress = true;
    BaseType_t ok = xTaskCreate(
        &EnvironmentService::RefreshTask,
        "env_refresh",
        8192,
        this,
        3,
        &refresh_task_handle_);
    if (ok != pdPASS) {
        refresh_task_handle_ = nullptr;
        snapshot_.refresh_in_progress = false;
        SetLastErrorLocked("Failed to create refresh task");
        ESP_LOGW(TAG, "Failed to create refresh task");
    }
}

esp_err_t EnvironmentService::RefreshNow() {
    EnvironmentConfig config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LoadConfigLocked();
        config = config_;
        snapshot_.configured = !config_.amap_api_key.empty();
        snapshot_.refresh_in_progress = true;
        snapshot_.last_error.clear();
    }

    if (config.amap_api_key.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.refresh_in_progress = false;
        SetLastErrorLocked("AMap API key is not configured");
        return ESP_ERR_INVALID_STATE;
    }

    std::string adcode = config.fallback_adcode;
    std::string city = config.fallback_city;
    esp_err_t ret = FetchLocation(config, adcode, city);
    if (ret != ESP_OK && adcode.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.refresh_in_progress = false;
        SetLastErrorLocked("Location unavailable");
        return ret;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.adcode = adcode;
        if (!city.empty()) {
            snapshot_.city = city;
        }
        snapshot_.location_available = !adcode.empty();
    }

    ret = FetchWeather(config, adcode);
    esp_err_t air_ret = FetchAirQuality(config, adcode);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.updated_at = time(nullptr);
        snapshot_.stale = false;
        snapshot_.refresh_in_progress = false;
        if (ret == ESP_OK) {
            snapshot_.last_error.clear();
        } else {
            SetLastErrorLocked("Weather unavailable");
        }
        SaveCacheLocked();
    }

    if (ret != ESP_OK) {
        return ret;
    }
    return air_ret == ESP_OK || config.aggregate_url.empty() ? ESP_OK : air_ret;
}

EnvironmentSnapshot EnvironmentService::GetSnapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    LoadConfigLocked();
    snapshot_.configured = !config_.amap_api_key.empty();
    return snapshot_;
}

void EnvironmentService::LoadConfigLocked() {
    if (config_loaded_) {
        return;
    }

    Settings settings(kSettingsNamespace, false);
    config_.amap_api_key = settings.GetString(kAmapKey);
    config_.aggregate_url = settings.GetString(kAggregateUrlKey);
    config_.refresh_interval_minutes = settings.GetInt(kRefreshMinutesKey, kDefaultRefreshMinutes);
    config_.refresh_interval_minutes = std::clamp(
        config_.refresh_interval_minutes, kMinRefreshMinutes, kMaxRefreshMinutes);
    config_.fallback_adcode = settings.GetString(kFallbackAdcodeKey);
    config_.fallback_city = settings.GetString(kFallbackCityKey);

#if defined(CONFIG_XIAOZHI_AMAP_API_KEY)
    if (CONFIG_XIAOZHI_AMAP_API_KEY[0] != '\0' &&
        config_.amap_api_key != CONFIG_XIAOZHI_AMAP_API_KEY) {
        config_.amap_api_key = CONFIG_XIAOZHI_AMAP_API_KEY;
        Settings writable_settings(kSettingsNamespace, true);
        writable_settings.SetString(kAmapKey, config_.amap_api_key);
        ESP_LOGI(TAG, "Applied AMap API key from local build configuration");
    }
#endif

    snapshot_.configured = !config_.amap_api_key.empty();
    config_loaded_ = true;
}

void EnvironmentService::LoadCacheLocked() {
    Settings settings(kSettingsNamespace, false);
    std::string cache = settings.GetString(kCacheKey);
    if (cache.empty()) {
        return;
    }

    cJSON* root = cJSON_Parse(cache.c_str());
    if (root == nullptr) {
        return;
    }

    snapshot_.city = JsonString(root, "city");
    snapshot_.adcode = JsonString(root, "adcode");
    snapshot_.weather_text = JsonString(root, "weather");
    snapshot_.temperature_c = JsonString(root, "temp");
    snapshot_.humidity = JsonString(root, "humidity");
    snapshot_.wind_direction = JsonString(root, "wind_dir");
    snapshot_.wind_power = JsonString(root, "wind_power");
    snapshot_.aqi = JsonInt(root, "aqi");
    snapshot_.air_category = JsonString(root, "air_cat");
    snapshot_.pm25 = JsonInt(root, "pm25");
    snapshot_.pm10 = JsonInt(root, "pm10");
    cJSON* updated = cJSON_GetObjectItem(root, "updated_at");
    if (cJSON_IsNumber(updated)) {
        snapshot_.updated_at = static_cast<int64_t>(updated->valuedouble);
    }
    snapshot_.location_available = !snapshot_.adcode.empty();
    snapshot_.weather_available = !snapshot_.weather_text.empty();
    snapshot_.air_available = snapshot_.aqi >= 0 || !snapshot_.air_category.empty();
    snapshot_.stale = true;
    cJSON_Delete(root);
}

void EnvironmentService::SaveCacheLocked() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "city", snapshot_.city.c_str());
    cJSON_AddStringToObject(root, "adcode", snapshot_.adcode.c_str());
    cJSON_AddStringToObject(root, "weather", snapshot_.weather_text.c_str());
    cJSON_AddStringToObject(root, "temp", snapshot_.temperature_c.c_str());
    cJSON_AddStringToObject(root, "humidity", snapshot_.humidity.c_str());
    cJSON_AddStringToObject(root, "wind_dir", snapshot_.wind_direction.c_str());
    cJSON_AddStringToObject(root, "wind_power", snapshot_.wind_power.c_str());
    cJSON_AddNumberToObject(root, "aqi", snapshot_.aqi);
    cJSON_AddStringToObject(root, "air_cat", snapshot_.air_category.c_str());
    cJSON_AddNumberToObject(root, "pm25", snapshot_.pm25);
    cJSON_AddNumberToObject(root, "pm10", snapshot_.pm10);
    cJSON_AddNumberToObject(root, "updated_at", static_cast<double>(snapshot_.updated_at));
    char* json = cJSON_PrintUnformatted(root);
    if (json != nullptr) {
        Settings settings(kSettingsNamespace, true);
        settings.SetString(kCacheKey, json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

bool EnvironmentService::ShouldRefreshLocked(int64_t now_us, bool force) const {
    if (config_.amap_api_key.empty()) {
        return false;
    }
    if (force) {
        return true;
    }
    if (last_refresh_attempt_us_ == 0) {
        return true;
    }
    return now_us - last_refresh_attempt_us_ >=
        static_cast<int64_t>(config_.refresh_interval_minutes) * kUsPerMinute;
}

esp_err_t EnvironmentService::FetchLocation(const EnvironmentConfig& config,
        std::string& adcode, std::string& city) {
    std::string response;
    std::string url = "https://restapi.amap.com/v3/ip?key=" + config.amap_api_key;
    esp_err_t ret = HttpGetJson(url, response);
    if (ret != ESP_OK) {
        return ret;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (IsAmapOk(root)) {
        std::string new_adcode = JsonString(root, "adcode");
        std::string new_city = JsonString(root, "city");
        if (!new_adcode.empty()) {
            adcode = new_adcode;
        }
        if (!new_city.empty()) {
            city = new_city;
        }
    } else {
        ret = ESP_ERR_INVALID_RESPONSE;
    }
    cJSON_Delete(root);
    return ret;
}

esp_err_t EnvironmentService::FetchWeather(const EnvironmentConfig& config,
        const std::string& adcode) {
    if (adcode.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    std::string response;
    std::string url = "https://restapi.amap.com/v3/weather/weatherInfo?key=" +
        config.amap_api_key + "&city=" + adcode + "&extensions=base";
    esp_err_t ret = HttpGetJson(url, response);
    if (ret != ESP_OK) {
        return ret;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!IsAmapOk(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON* lives = cJSON_GetObjectItem(root, "lives");
    cJSON* live = cJSON_IsArray(lives) ? cJSON_GetArrayItem(lives, 0) : nullptr;
    if (!cJSON_IsObject(live)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::string city = JsonString(live, "city");
    std::string live_adcode = JsonString(live, "adcode");
    if (!city.empty()) {
        snapshot_.city = city;
    }
    if (!live_adcode.empty()) {
        snapshot_.adcode = live_adcode;
    }
    snapshot_.weather_text = JsonString(live, "weather");
    snapshot_.temperature_c = JsonString(live, "temperature");
    snapshot_.humidity = JsonString(live, "humidity");
    snapshot_.wind_direction = JsonString(live, "winddirection");
    snapshot_.wind_power = JsonString(live, "windpower");
    snapshot_.weather_available = !snapshot_.weather_text.empty();
    snapshot_.location_available = !snapshot_.adcode.empty();
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t EnvironmentService::FetchAirQuality(const EnvironmentConfig& config,
        const std::string& adcode) {
    if (config.aggregate_url.empty() || adcode.empty()) {
        return ESP_ERR_INVALID_STATE;
    }

    std::string response;
    esp_err_t ret = HttpGetJson(AppendQueryParam(config.aggregate_url, "adcode", adcode), response);
    if (ret != ESP_OK) {
        return ret;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON* air = cJSON_GetObjectItem(root, "air");
    if (!cJSON_IsObject(air)) {
        air = root;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.aqi = JsonInt(air, "aqi");
    snapshot_.air_category = JsonString(air, "category");
    snapshot_.pm25 = JsonInt(air, "pm25");
    snapshot_.pm10 = JsonInt(air, "pm10");
    snapshot_.air_available = snapshot_.aqi >= 0 || !snapshot_.air_category.empty();
    cJSON_Delete(root);
    return snapshot_.air_available ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t EnvironmentService::HttpGetJson(const std::string& url, std::string& response) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    auto http = network->CreateHttp(2);
    http->SetTimeout(10000);
    http->SetHeader("Accept", "application/json");
    if (!http->Open("GET", url)) {
        int err = http->GetLastError();
        ESP_LOGW(TAG, "HTTP open failed: 0x%x", err);
        return err == 0 ? ESP_FAIL : err;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGW(TAG, "HTTP status %d", status_code);
        http->Close();
        return ESP_FAIL;
    }

    response = http->ReadAll();
    http->Close();
    return response.empty() ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

void EnvironmentService::SetLastErrorLocked(const std::string& error) {
    snapshot_.last_error = error;
    snapshot_.stale = true;
}

void EnvironmentService::RefreshTask(void* arg) {
    auto* service = static_cast<EnvironmentService*>(arg);
    if (service != nullptr) {
        service->RefreshNow();
        std::lock_guard<std::mutex> lock(service->mutex_);
        service->refresh_task_handle_ = nullptr;
        service->snapshot_.refresh_in_progress = false;
    }
    vTaskDelete(nullptr);
}
