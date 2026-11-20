#include "speaker_calendar_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_home_widgets.h"

#include <cstdio>
#include <initializer_list>

void SpeakerCalendarView::Create(lv_obj_t* parent) {
    using namespace SpeakerHomeWidgets;
    panel_ = Panel(parent);
    lv_obj_set_pos(Label(panel_, "电子日历", kText), 18, 7);
    auto* close = Button(panel_, "返回", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->Hide();
    }, this);
    lv_obj_set_size(close, 86, 40);
    lv_obj_set_pos(close, 464, 4);

    auto* calendar_card = Card(panel_, 18, 62, 532, 394);
    lv_obj_set_style_border_color(calendar_card, lv_color_hex(kCyan), LV_PART_MAIN);
    lv_obj_set_style_border_opa(calendar_card, LV_OPA_50, LV_PART_MAIN);
    month_ = Label(calendar_card, "", kCyan);
    lv_obj_set_pos(month_, 16, 18);
    previous_ = Button(calendar_card, "上月", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->MoveMonth(-1);
    }, this);
    lv_obj_set_size(previous_, 78, 42);
    lv_obj_set_pos(previous_, 250, 8);
    next_ = Button(calendar_card, "下月", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->MoveMonth(1);
    }, this);
    lv_obj_set_size(next_, 78, 42);
    lv_obj_set_pos(next_, 332, 8);
    today_ = Button(calendar_card, "今天", [](lv_event_t* event) {
        auto* view = static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event));
        view->current_date_ = 0;
        view->Update();
    }, this);
    lv_obj_set_size(today_, 88, 42);
    lv_obj_set_pos(today_, 414, 8);
    calendar_ = lv_calendar_create(calendar_card);
    lv_obj_set_size(calendar_, 504, 316);
    lv_obj_set_pos(calendar_, 13, 59);
    lv_obj_set_style_bg_color(calendar_, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_border_color(calendar_, lv_color_hex(kEdge), LV_PART_MAIN);
#if LV_CALENDAR_WEEK_STARTS_MONDAY
    static const char* weekdays[] = {"一", "二", "三", "四", "五", "六", "日"};
#else
    static const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
#endif
    lv_calendar_set_day_names(calendar_, weekdays);
    auto* dates = lv_calendar_get_btnmatrix(calendar_);
    lv_obj_set_style_bg_color(dates, lv_color_hex(kCard), LV_PART_ITEMS);
    lv_obj_set_style_border_color(dates, lv_color_hex(kEdge), LV_PART_ITEMS);
    lv_obj_set_style_text_color(dates, lv_color_hex(kText), LV_PART_ITEMS);
    lv_obj_remove_flag(dates, LV_OBJ_FLAG_CLICKABLE);

    auto* today_card = Card(panel_, 564, 62, 218, 394);
    lv_obj_align(Label(today_card, "今天", kMuted), LV_ALIGN_TOP_MID, 0, 38);
    day_ = Label(today_card, "--", kCyan);
#if LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(day_, &lv_font_montserrat_48, LV_PART_MAIN);
#endif
    lv_obj_center(day_);
    weekday_ = Label(today_card, "", kText);
    lv_obj_align(weekday_, LV_ALIGN_CENTER, 0, 72);
}

void SpeakerCalendarView::Show() {
    current_date_ = 0;
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
        lv_label_set_text(day_, "--");
        lv_label_set_text(weekday_, "");
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
    constexpr const char* weekdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
    lv_label_set_text_fmt(day_, "%02d", day);
    lv_label_set_text(weekday_, weekdays[date.tm_wday]);
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
