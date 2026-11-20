#include "speaker_calendar_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_home_widgets.h"

#include <cstdio>
#include <initializer_list>

void SpeakerCalendarView::Create(lv_obj_t* parent) {
    panel_ = SpeakerHomeWidgets::Panel(parent);
    auto* close = SpeakerHomeWidgets::Button(panel_, "返回", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->Hide();
    }, this);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -20, 14);
    month_ = lv_label_create(panel_);
    lv_obj_align(month_, LV_ALIGN_TOP_MID, 0, 20);

    previous_ = SpeakerHomeWidgets::Button(panel_, "上月", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->MoveMonth(-1);
    }, this);
    lv_obj_align(previous_, LV_ALIGN_LEFT_MID, 24, 0);
    next_ = SpeakerHomeWidgets::Button(panel_, "下月", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->MoveMonth(1);
    }, this);
    lv_obj_align(next_, LV_ALIGN_RIGHT_MID, -24, 0);
    today_ = SpeakerHomeWidgets::Button(panel_, "今天", [](lv_event_t* event) {
        auto* view = static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event));
        view->current_date_ = 0;
        view->Update();
    }, this);
    lv_obj_align(today_, LV_ALIGN_TOP_LEFT, 20, 14);
    calendar_ = lv_calendar_create(panel_);
    lv_obj_set_size(calendar_, lv_pct(70), lv_pct(75));
    lv_obj_align(calendar_, LV_ALIGN_BOTTOM_MID, 0, -12);
#if LV_CALENDAR_WEEK_STARTS_MONDAY
    static const char* weekdays[] = {"一", "二", "三", "四", "五", "六", "日"};
#else
    static const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
#endif
    lv_calendar_set_day_names(calendar_, weekdays);
    lv_obj_set_style_text_color(lv_calendar_get_btnmatrix(calendar_), lv_color_hex(0x172334), LV_PART_ITEMS);
    lv_obj_remove_flag(lv_calendar_get_btnmatrix(calendar_), LV_OBJ_FLAG_CLICKABLE);
}

void SpeakerCalendarView::Show() {
    current_date_ = 0;
    lv_obj_move_foreground(panel_);
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    Update();
}

void SpeakerCalendarView::Hide() {
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerCalendarView::Update() {
    if (lv_obj_has_flag(panel_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const auto now = std::time(nullptr);
    std::tm date{};
    if (now < 1704067200 || !localtime_r(&now, &date)) {
        lv_label_set_text(month_, "正在校时");
        for (auto* object : {calendar_, today_, previous_, next_}) {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
        current_date_ = 0;
        return;
    }

    const int year = date.tm_year + 1900;
    const int month = date.tm_mon + 1;
    const int day = date.tm_mday;
    const int today = (year * 100 + month) * 100 + day;
    if (today == current_date_) {
        return;
    }
    lv_calendar_set_today_date(calendar_, year, month, day);
    if (current_date_ == 0) {
        for (auto* object : {calendar_, today_, previous_, next_}) {
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
        ShowMonth(year, month);
    }
    current_date_ = today;
}

void SpeakerCalendarView::MoveMonth(int offset) {
    const auto* date = lv_calendar_get_showed_date(calendar_);
    int year = date->year;
    int month = date->month + offset;
    if (month == 0) {
        month = 12;
        --year;
    } else if (month == 13) {
        month = 1;
        ++year;
    }
    if (year >= 1900 && year <= 2099) {
        ShowMonth(year, month);
    }
}

void SpeakerCalendarView::ShowMonth(int year, int month) {
    char title[40];
    std::snprintf(title, sizeof(title), "%d 年 %d 月", year, month);
    lv_label_set_text(month_, title);
    lv_calendar_set_month_shown(calendar_, year, month);
}

#endif
