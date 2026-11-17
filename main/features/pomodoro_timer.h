#ifndef POMODORO_TIMER_H
#define POMODORO_TIMER_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include <esp_err.h>
#include <esp_timer.h>

enum class PomodoroState {
    kIdle,
    kRunning,
    kPaused,
    kFinished,
};

struct PomodoroStatus {
    PomodoroState state = PomodoroState::kIdle;
    int duration_minutes = 0;
    int remaining_seconds = 0;
    int elapsed_seconds = 0;
};

class PomodoroTimer {
public:
    static PomodoroTimer& GetInstance();

    esp_err_t Start(int duration_minutes);
    esp_err_t Pause();
    esp_err_t Resume();
    esp_err_t Cancel();

    PomodoroStatus GetStatus();
    std::string GetStatusText();
    void SetFinishCallback(std::function<void()> callback);

    static constexpr int kMinDurationMinutes = 1;
    static constexpr int kMaxDurationMinutes = 180;

private:
    PomodoroTimer();
    ~PomodoroTimer();
    PomodoroTimer(const PomodoroTimer&) = delete;
    PomodoroTimer& operator=(const PomodoroTimer&) = delete;

    int GetRemainingSecondsLocked(int64_t now_us) const;
    void Tick();
    static void TimerCallback(void* arg);

    std::mutex mutex_;
    esp_timer_handle_t timer_ = nullptr;
    PomodoroState state_ = PomodoroState::kIdle;
    int duration_minutes_ = 0;
    int duration_seconds_ = 0;
    int paused_remaining_seconds_ = 0;
    int64_t deadline_us_ = 0;
    std::function<void()> finish_callback_;
};

#endif // POMODORO_TIMER_H
