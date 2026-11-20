#ifndef ENVIRONMENT_SERVICE_H
#define ENVIRONMENT_SERVICE_H

#include <cstdint>
#include <mutex>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <sdkconfig.h>
#include "environment_snapshot.h"

struct EnvironmentConfig {
    std::string amap_api_key;
    std::string aggregate_url;
    int refresh_interval_minutes = 30;
    std::string fallback_adcode;
    std::string fallback_city;
};

class EnvironmentService {
public:
    static EnvironmentService& GetInstance();

#if CONFIG_XIAOZHI_PRODUCT_ENVIRONMENT
    std::string SelectCity(const std::string& city, const std::string& province);
    std::string CitySelectionStatus();
#else
    EnvironmentConfig GetConfig();
    void SaveConfig(const EnvironmentConfig& config);
#endif
    void RefreshAsync(bool force = false);
    esp_err_t RefreshNow();
    EnvironmentSnapshot GetSnapshot();

private:
    EnvironmentService();
    EnvironmentService(const EnvironmentService&) = delete;
    EnvironmentService& operator=(const EnvironmentService&) = delete;

#if CONFIG_XIAOZHI_PRODUCT_ENVIRONMENT
    std::string cached_response_;
    std::string manual_city_id_;
    std::string selection_query_;
    std::string selection_status_;
    uint32_t selection_revision_ = 0;
    int64_t next_refresh_us_ = 0;
#else
    void LoadConfigLocked();
    void LoadCacheLocked();
    void SaveCacheLocked();
    esp_err_t FetchLocation(const EnvironmentConfig& config, std::string& adcode, std::string& city);
    esp_err_t FetchWeather(const EnvironmentConfig& config, const std::string& adcode);
    esp_err_t FetchAirQuality(const EnvironmentConfig& config, const std::string& adcode);
    esp_err_t HttpGetJson(const std::string& url, std::string& response);
    void SetLastErrorLocked(const std::string& error);
#endif
    bool ShouldRefreshLocked(int64_t now_us, bool force) const;
    static void RefreshTask(void* arg);

    mutable std::mutex mutex_;
#if !CONFIG_XIAOZHI_PRODUCT_ENVIRONMENT
    EnvironmentConfig config_;
    bool config_loaded_ = false;
#endif
    EnvironmentSnapshot snapshot_;
    int64_t last_refresh_attempt_us_ = 0;
    TaskHandle_t refresh_task_handle_ = nullptr;
};

#endif // ENVIRONMENT_SERVICE_H
