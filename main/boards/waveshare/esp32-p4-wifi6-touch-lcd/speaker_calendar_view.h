#pragma once

#include <lvgl.h>
#include <ctime>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

class SpeakerCalendarView {
public:
    void Create(lv_obj_t* parent);
    void Show();
    void Hide();
    void Update();

private:
    void MoveMonth(int offset);
    void ShowMonth(int year, int month);

    lv_obj_t* panel_ = nullptr;
    lv_obj_t* calendar_ = nullptr;
    lv_obj_t* month_ = nullptr;
    lv_obj_t* previous_ = nullptr;
    lv_obj_t* next_ = nullptr;
    lv_obj_t* today_ = nullptr;
    int current_date_ = 0;
};

#endif
