#pragma once
#include <sdkconfig.h>
#include <lvgl.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_navigation.h"
#include "speaker_memos.h"

#include <array>
#include <chrono>
#include <optional>
#include <vector>

class SpeakerCalendarView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void Show();
    void Hide();
    void Update();

private:
    using Date = std::chrono::sys_days;
    struct DateCell {
        lv_obj_t* root;
        lv_obj_t* weekday;
        lv_obj_t* number;
        lv_obj_t* selection;
        lv_obj_t* memo_dot;
        std::optional<Date> date;
    };

    static void OnDateTouch(lv_event_t* event);
    static void AnimateOffset(void* view, int32_t offset);
    void DragTo(const lv_point_t& point);
    void FinishTouch(const lv_point_t& point);
    void SetOffset(int32_t offset);
    void Settle(int32_t coast);
    void GoToToday();
    bool DateIsVisible(Date date) const;
    void RenderDates(bool refresh = false);
    void RenderCell(DateCell& cell, Date date);
    bool RefreshEntries();
    void RenderMemos();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* dates_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* clock_hint_ = nullptr;
    lv_obj_t* memo_hint_ = nullptr;
    lv_obj_t* month_ = nullptr;
    lv_obj_t* today_button_ = nullptr;
    std::array<DateCell, 8> cells_{};
    std::optional<Date> today_;
    std::chrono::year_month visible_month_{};
    Date selected_{};
    std::vector<SpeakerMemos::Entry> entries_;
    int64_t binding_ = 0;
    uint32_t memo_revision_ = 0;
    int32_t offset_ = 0;
    int32_t drag_start_offset_ = 0;
    lv_point_t touch_start_{};
    int32_t last_touch_x_ = 0;
    uint32_t last_motion_tick_ = 0;
    int32_t velocity_ = 0;
    bool tracking_ = false;
    bool dragging_ = false;
};

#endif
