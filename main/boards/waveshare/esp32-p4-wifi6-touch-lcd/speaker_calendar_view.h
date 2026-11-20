#pragma once

#include <lvgl.h>
#include <ctime>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_memos.h"

class SpeakerCalendarView {
public:
    void Create(lv_obj_t* parent);
    void Show();
    void Hide();
    void Update();

private:
    void MoveMonth(int offset);
    void ShowMonth(int year, int month, int day);
    void UpdateMemos();

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* calendar_ = nullptr;
    lv_obj_t* month_ = nullptr;
    lv_obj_t* memo_date_ = nullptr;
    lv_obj_t* memo_text_ = nullptr;
    lv_obj_t* previous_ = nullptr;
    lv_obj_t* next_ = nullptr;
    lv_obj_t* today_ = nullptr;
    int current_date_ = 0;
    lv_calendar_date_t selected_{};
    std::array<lv_calendar_date_t, SpeakerMemos::kCapacity> highlights_{};
    int64_t memo_binding_ = 0;
    uint32_t memo_revision_ = 0;
    bool selection_changed_ = true;
};

#endif
