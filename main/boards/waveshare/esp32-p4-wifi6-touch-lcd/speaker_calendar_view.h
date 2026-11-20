#pragma once
#include <sdkconfig.h>
#include <lvgl.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_navigation.h"

#include <array>
#include <chrono>
#include <optional>

class SpeakerCalendarView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void Show();
    void Hide();
    void Update();

private:
    using Date = std::chrono::sys_days;
    static void OnWeekTouch(lv_event_t* event);
    void RenderWeek();
    void RenderMemos();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* week_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* clock_hint_ = nullptr;
    std::array<lv_obj_t*, 7> days_{};
    std::array<lv_obj_t*, 7> selection_{};
    std::optional<Date> today_;
    Date selected_{};
    int64_t binding_ = 0;
    uint32_t memo_revision_ = 0;
    lv_point_t touch_start_{};
    bool tracking_ = false;
};

#endif
