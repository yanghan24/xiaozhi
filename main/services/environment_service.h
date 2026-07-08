#ifndef ENVIRONMENT_SERVICE_H
#define ENVIRONMENT_SERVICE_H

#include <cstdint>
#include <mutex>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>

struct EnvironmentConfig {
    std::string amap_api_key;
    std::string aggregate_url;
    int refresh_interval_minutes = 30;
    std::string fallback_adcode;
    std::string fallback_city;
};

struct EnvironmentSnapshot {
    bool configured = false;
    bool location_available = false;
    bool weather_available = false;
    bool air_available = false;
    bool stale = true;
    bool refresh_in_progress = false;

    std::string city;
    std::string adcode;
    std::string weather_text;
    std::string temperature_c;
    std::string humidity;
    std::string wind_direction;
    std::string wind_power;
    int aqi = -1;
    std::string air_category;
    int pm25 = -1;
    int pm10 = -1;
    int64_t updated_at = 0;
    std::string last_error;
};

class EnvironmentService {
public:
    static EnvironmentService& GetInstance();

    EnvironmentConfig GetConfig();
    void SaveConfig(const EnvironmentConfig& config);
    void RefreshAsync(bool force = false);
    esp_err_t RefreshNow();
    EnvironmentSnapshot GetSnapshot();

private:
    EnvironmentService();
    EnvironmentService(const EnvironmentService&) = delete;
    EnvironmentService& operator=(const EnvironmentService&) = delete;

    void LoadConfigLocked();
    void LoadCacheLocked();
    void SaveCacheLocked();
    bool ShouldRefreshLocked(int64_t now_us, bool force) const;
    esp_err_t FetchLocation(const EnvironmentConfig& config, std::string& adcode, std::string& city);
    esp_err_t FetchWeather(const EnvironmentConfig& config, const std::string& adcode);
    esp_err_t FetchAirQuality(const EnvironmentConfig& config, const std::string& adcode);
    esp_err_t HttpGetJson(const std::string& url, std::string& response);
    void SetLastErrorLocked(const std::string& error);
    static void RefreshTask(void* arg);

    mutable std::mutex mutex_;
    EnvironmentConfig config_;
    EnvironmentSnapshot snapshot_;
    bool config_loaded_ = false;
    int64_t last_refresh_attempt_us_ = 0;
    TaskHandle_t refresh_task_handle_ = nullptr;
};

#endif // ENVIRONMENT_SERVICE_H
