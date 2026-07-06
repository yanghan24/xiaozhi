#include "pomodoro_timer.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include <esp_log.h>

#define TAG "PomodoroTimer"

namespace {
constexpr int64_t kUsPerSecond = 1000000;
constexpr int64_t kTimerPeriodUs = kUsPerSecond;
} // namespace

PomodoroTimer& PomodoroTimer::GetInstance() {
    static PomodoroTimer instance;
    return instance;
}

PomodoroTimer::PomodoroTimer() {
    esp_timer_create_args_t timer_args = {
        .callback = &PomodoroTimer::TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pomodoro_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, kTimerPeriodUs));
}

PomodoroTimer::~PomodoroTimer() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

esp_err_t PomodoroTimer::Start(int duration_minutes) {
    if (duration_minutes < kMinDurationMinutes || duration_minutes > kMaxDurationMinutes) {
        ESP_LOGW(TAG, "Reject duration %d minutes", duration_minutes);
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    duration_minutes_ = duration_minutes;
    duration_seconds_ = duration_minutes * 60;
    paused_remaining_seconds_ = 0;
    deadline_us_ = esp_timer_get_time() + static_cast<int64_t>(duration_seconds_) * kUsPerSecond;
    state_ = PomodoroState::kRunning;
    ESP_LOGI(TAG, "Started %d minute timer", duration_minutes_);
    return ESP_OK;
}

esp_err_t PomodoroTimer::Pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != PomodoroState::kRunning) {
        return ESP_OK;
    }

    paused_remaining_seconds_ = GetRemainingSecondsLocked(esp_timer_get_time());
    state_ = PomodoroState::kPaused;
    ESP_LOGI(TAG, "Paused with %d seconds remaining", paused_remaining_seconds_);
    return ESP_OK;
}

esp_err_t PomodoroTimer::Resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != PomodoroState::kPaused) {
        return ESP_OK;
    }

    deadline_us_ = esp_timer_get_time() + static_cast<int64_t>(paused_remaining_seconds_) * kUsPerSecond;
    state_ = PomodoroState::kRunning;
    ESP_LOGI(TAG, "Resumed with %d seconds remaining", paused_remaining_seconds_);
    return ESP_OK;
}

esp_err_t PomodoroTimer::Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = PomodoroState::kIdle;
    duration_minutes_ = 0;
    duration_seconds_ = 0;
    paused_remaining_seconds_ = 0;
    deadline_us_ = 0;
    ESP_LOGI(TAG, "Canceled");
    return ESP_OK;
}

PomodoroStatus PomodoroTimer::GetStatus() {
    std::lock_guard<std::mutex> lock(mutex_);

    PomodoroStatus status;
    status.state = state_;
    status.duration_minutes = duration_minutes_;
    status.remaining_seconds = GetRemainingSecondsLocked(esp_timer_get_time());
    status.elapsed_seconds = duration_seconds_ > 0
        ? std::max(0, duration_seconds_ - status.remaining_seconds)
        : 0;
    return status;
}

std::string PomodoroTimer::GetStatusText() {
    auto status = GetStatus();
    char buffer[96];
    int remaining_minutes = status.remaining_seconds / 60;
    int remaining_seconds = status.remaining_seconds % 60;

    switch (status.state) {
        case PomodoroState::kRunning:
            snprintf(buffer, sizeof(buffer), "Pomodoro running: %02d:%02d remaining.",
                remaining_minutes, remaining_seconds);
            break;
        case PomodoroState::kPaused:
            snprintf(buffer, sizeof(buffer), "Pomodoro paused: %02d:%02d remaining.",
                remaining_minutes, remaining_seconds);
            break;
        case PomodoroState::kFinished:
            snprintf(buffer, sizeof(buffer), "Pomodoro finished.");
            break;
        case PomodoroState::kIdle:
        default:
            snprintf(buffer, sizeof(buffer), "Pomodoro idle.");
            break;
    }
    return std::string(buffer);
}

void PomodoroTimer::SetFinishCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    finish_callback_ = std::move(callback);
}

int PomodoroTimer::GetRemainingSecondsLocked(int64_t now_us) const {
    if (state_ == PomodoroState::kPaused) {
        return std::max(0, paused_remaining_seconds_);
    }
    if (state_ == PomodoroState::kRunning) {
        int64_t remaining_us = deadline_us_ - now_us;
        if (remaining_us <= 0) {
            return 0;
        }
        return static_cast<int>((remaining_us + kUsPerSecond - 1) / kUsPerSecond);
    }
    return 0;
}

void PomodoroTimer::Tick() {
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != PomodoroState::kRunning ||
                GetRemainingSecondsLocked(esp_timer_get_time()) > 0) {
            return;
        }

        state_ = PomodoroState::kFinished;
        paused_remaining_seconds_ = 0;
        deadline_us_ = 0;
        callback = finish_callback_;
        ESP_LOGI(TAG, "Finished");
    }

    if (callback) {
        callback();
    }
}

void PomodoroTimer::TimerCallback(void* arg) {
    auto* timer = static_cast<PomodoroTimer*>(arg);
    if (timer != nullptr) {
        timer->Tick();
    }
}
