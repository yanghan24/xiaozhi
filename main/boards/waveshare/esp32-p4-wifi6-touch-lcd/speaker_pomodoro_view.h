#pragma once

#include <lvgl.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"

#include <atomic>
#include <cstdint>

class SpeakerPomodoroView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void Show();
    void Hide();
    void Update();
    void RequestStopAlarm();

private:
    enum class State { Ready, Running, Paused, Finished };
    enum class Mode { Focus, Rest };

    void Toggle();
    void Reset(int minutes);
    void SetMode(Mode mode);
    void SetMinutes(int minutes);
    void ChangeMinutes(int delta);
    void RefreshMode();
    void Render(int64_t now);
    void DispatchSound();
    static void AlarmTask(void* context);
    void RunAlarm();

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* clock_ = nullptr;
#if LV_USE_ARC
    lv_obj_t* progress_ = nullptr;
#endif
    lv_obj_t* state_label_ = nullptr;
    lv_obj_t* action_label_ = nullptr;
    lv_obj_t* work_ = nullptr;
    lv_obj_t* rest_ = nullptr;
    lv_obj_t* minus_ = nullptr;
    lv_obj_t* plus_ = nullptr;
    lv_obj_t* minutes_label_ = nullptr;
    lv_obj_t* slider_ = nullptr;
    State state_ = State::Ready;
    Mode mode_ = Mode::Focus;
    int minutes_ = 25;
    int64_t remaining_us_ = 0;
    int64_t deadline_us_ = 0;
    int64_t next_sound_check_us_ = 0;
    int displayed_seconds_ = -1;
    State displayed_state_ = State::Finished;
    bool adjusting_ = false;

    // LVGL owns the timer state. The application's main task dispatches the sound.
    std::atomic<bool> sound_pending_{false};
    std::atomic<bool> sound_scheduled_{false};
    std::atomic<uint32_t> alarm_generation_{0};
    std::atomic<uint32_t> alarm_epoch_{0};
};

#endif
