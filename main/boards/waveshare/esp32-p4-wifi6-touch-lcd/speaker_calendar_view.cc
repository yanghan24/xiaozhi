#include "speaker_calendar_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_home_widgets.h"
#include "speaker_account/speaker_account.h"

#include <cstdio>
#include <initializer_list>
#include <string>

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
    lv_obj_add_event_cb(calendar_, [](lv_event_t* event) {
        auto* view = static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event));
        lv_calendar_date_t date{};
        if (lv_calendar_get_pressed_date(view->calendar_, &date) == LV_RESULT_OK && date.day > 0) {
            view->selected_ = date;
            view->selection_changed_ = true;
            view->UpdateMemos();
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* memo_card = Card(panel_, 564, 62, 218, 394);
    lv_obj_set_pos(Label(memo_card, "日程记事", kCyan), 14, 14);
    memo_date_ = Label(memo_card, "", kMuted);
    lv_obj_set_pos(memo_date_, 14, 49);
    auto* scroll = lv_obj_create(memo_card);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_size(scroll, 190, 300);
    lv_obj_set_pos(scroll, 14, 82);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    memo_text_ = Label(scroll, "", kText);
    lv_obj_set_width(memo_text_, 184);
    lv_label_set_long_mode(memo_text_, LV_LABEL_LONG_WRAP);
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
        SpeakerHomeWidgets::UpdateLabel(memo_date_, "");
        SpeakerHomeWidgets::UpdateLabel(memo_text_, "正在校时");
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
        UpdateMemos();
        return;
    }
    lv_calendar_set_today_date(calendar_, year, month, day);
    if (current_date_ == 0) {
        for (auto* object : {calendar_, today_, previous_, next_}) {
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    }
    const int selected_date = (selected_.year * 100 + selected_.month) * 100 + selected_.day;
    if (current_date_ == 0 || selected_date == current_date_) {
        ShowMonth(year, month, day);
    } else {
        UpdateMemos();
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
        ShowMonth(year, month, 1);
    }
}

void SpeakerCalendarView::ShowMonth(int year, int month, int day) {
    char title[40];
    std::snprintf(title, sizeof(title), "%d 年 %d 月", year, month);
    lv_label_set_text(month_, title);
    lv_calendar_set_month_shown(calendar_, year, month);
    selected_ = {static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)};
    selection_changed_ = true;
    UpdateMemos();
}

void SpeakerCalendarView::UpdateMemos() {
    const auto binding = SpeakerAccount::Get().MemoBindingVersion();
    auto& memos = SpeakerMemos::Get();
    const auto revision = memos.Revision();
    if (!selection_changed_ && binding == memo_binding_ && revision == memo_revision_) {
        return;
    }
    selection_changed_ = false;
    memo_binding_ = binding;
    memo_revision_ = revision;

    lv_label_set_text_fmt(memo_date_, "%02d 月 %02d 日", selected_.month, selected_.day);
    size_t highlighted = 0;
    std::string text;
    for (const auto& entry : memos.Entries(binding)) {
        int year, month, day;
        if (std::sscanf(entry.when, "%d-%d-%d", &year, &month, &day) != 3 ||
            year != selected_.year || month != selected_.month) {
            continue;
        }
        if (highlighted == 0 || highlights_[highlighted - 1].day != day) {
            highlights_[highlighted++] = {static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                                          static_cast<uint8_t>(day)};
        }
        if (day != selected_.day) {
            continue;
        }
        if (!text.empty()) text += "\n\n";
        if (entry.when[10] == ' ') {
            text.append(entry.when + 11, 5);
            text += "  ";
        }
        text += entry.content;
    }
    lv_calendar_set_highlighted_dates(calendar_, highlights_.data(), highlighted);
    if (text.empty()) text = binding > 0 ? "当天暂无记事" : "登录后可查看记事";
    SpeakerHomeWidgets::UpdateLabel(memo_text_, text.c_str());
    lv_obj_scroll_to_y(lv_obj_get_parent(memo_text_), 0, LV_ANIM_OFF);
}

#endif
