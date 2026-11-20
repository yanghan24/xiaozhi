#include "environment_service.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "EnvironmentService"

EnvironmentService& EnvironmentService::GetInstance() {
    static EnvironmentService instance;
    return instance;
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
        snapshot_.last_error = "Failed to create refresh task";
        snapshot_.stale = true;
        ESP_LOGW(TAG, "Failed to create refresh task");
    }
}

void EnvironmentService::RefreshTask(void* arg) {
    auto* service = static_cast<EnvironmentService*>(arg);
    while (service != nullptr) {
        service->RefreshNow();
        std::lock_guard<std::mutex> lock(service->mutex_);
#if CONFIG_XIAOZHI_PRODUCT_ENVIRONMENT
        // A voice selection arriving during TLS supersedes the in-flight response.
        if (service->next_refresh_us_ == 0) continue;
#endif
        service->refresh_task_handle_ = nullptr;
        service->snapshot_.refresh_in_progress = false;
        break;
    }
    vTaskDelete(nullptr);
}
