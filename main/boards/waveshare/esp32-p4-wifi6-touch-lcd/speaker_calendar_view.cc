#include "speaker_calendar_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_account/speaker_account.h"
#include "speaker_account/speaker_ui_widgets.h"
#include "speaker_memos.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>

using namespace SpeakerUi;
using namespace std::chrono;

namespace {
constexpr int kStripWidth = 710;

std::optional<sys_days> LocalDate() {
    const auto now = std::time(nullptr);
    std::tm local{};
    if (now < 1704067200 || !localtime_r(&now, &local)) {
        return std::nullopt;
    }
    return sys_days(year{local.tm_year + 1900} / month{static_cast<unsigned>(local.tm_mon + 1)} /
                    day{static_cast<unsigned>(local.tm_mday)});
}

unsigned DayInWeek(sys_days date) {
    return weekday(date).iso_encoding() - 1;
}
}  // namespace

void SpeakerCalendarView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    root_ = Panel(parent);
    Header(root_, "日历", SpeakerNavigation::OnBack, &navigation);
    week_ = Surface(root_, 45, 87, kStripWidth, 105);
    lv_obj_add_flag(week_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(week_, OnWeekTouch, LV_EVENT_ALL, this);
    constexpr const char* names[] = {"一", "二", "三", "四", "五", "六", "日"};
    for (size_t i = 0; i < days_.size(); ++i) {
        const int left = kStripWidth * i / days_.size();
        const int width = kStripWidth * (i + 1) / days_.size() - left;
        auto* cell = Surface(week_, left, 0, width, 105);
        lv_obj_align(Label(cell, names[i], kMuted), LV_ALIGN_TOP_MID, 0, 6);
        auto* background = Surface(cell, (width - 78) / 2, 48, 78, 52);
        lv_obj_set_style_radius(background, 15, LV_PART_MAIN);
        lv_obj_set_style_bg_color(background, lv_color_hex(kCyan), LV_PART_MAIN);
        selection_[i] = background;
        days_[i] = Label(cell, "");
        lv_obj_align(days_[i], LV_ALIGN_TOP_MID, 0, 56);
    }

    list_ = Surface(root_, 45, 209, 710, 239);
    lv_obj_add_flag(list_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    clock_hint_ = Label(root_, "正在校时", kMuted);
    lv_obj_center(clock_hint_);
}

void SpeakerCalendarView::Show() {
    today_.reset();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    Update();
}

void SpeakerCalendarView::Hide() {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    tracking_ = false;
}

void SpeakerCalendarView::Update() {
    if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const auto local = LocalDate();
    lv_obj_set_flag(clock_hint_, LV_OBJ_FLAG_HIDDEN, local.has_value());
    lv_obj_set_flag(week_, LV_OBJ_FLAG_HIDDEN, !local);
    lv_obj_set_flag(list_, LV_OBJ_FLAG_HIDDEN, !local);
    if (!local) {
        today_.reset();
        return;
    }
    bool selection_changed = !today_ || (*today_ != *local && selected_ == *today_);
    today_ = local;
    if (selection_changed) {
        selected_ = *today_;
        RenderWeek();
    }
    if (selection_changed || binding_ != SpeakerAccount::Get().MemoBindingVersion() ||
        memo_revision_ != SpeakerMemos::Get().Revision()) {
        RenderMemos();
    }
}

void SpeakerCalendarView::OnWeekTouch(lv_event_t* event) {
    auto* view = static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    auto* input = lv_indev_active();
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(input, &view->touch_start_);
        view->tracking_ = true;
    } else if (code == LV_EVENT_PRESS_LOST) {
        view->tracking_ = false;
    } else if (code == LV_EVENT_RELEASED && view->tracking_) {
        view->tracking_ = false;
        lv_point_t point;
        lv_indev_get_point(input, &point);
        const int dx = point.x - view->touch_start_.x;
        const int dy = point.y - view->touch_start_.y;
        if (std::abs(dx) >= 48 && std::abs(dx) > std::abs(dy)) {
            view->selected_ += days(dx < 0 ? 7 : -7);
        } else if (std::abs(dx) < 18 && std::abs(dy) < 18) {
            lv_area_t area;
            lv_obj_get_coords(view->week_, &area);
            const int relative_x = static_cast<int>(point.x - area.x1);
            const int cell = std::clamp(relative_x * 7 / kStripWidth, 0, 6);
            view->selected_ += days(cell - static_cast<int>(DayInWeek(view->selected_)));
        } else {
            return;
        }
        view->RenderWeek();
        view->RenderMemos();
    }
}

void SpeakerCalendarView::RenderWeek() {
    const unsigned selected_index = DayInWeek(selected_);
    const auto monday = selected_ - days(selected_index);
    for (unsigned i = 0; i < days_.size(); ++i) {
        const year_month_day day_of_week{monday + days(i)};
        lv_label_set_text_fmt(days_[i], "%u", static_cast<unsigned>(day_of_week.day()));
        lv_obj_set_style_bg_opa(selection_[i], i == selected_index ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(days_[i], lv_color_hex(i == selected_index ? kDeep : kText), LV_PART_MAIN);
    }
}

void SpeakerCalendarView::RenderMemos() {
    binding_ = SpeakerAccount::Get().MemoBindingVersion();
    memo_revision_ = SpeakerMemos::Get().Revision();
    lv_obj_clean(list_);
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
    const year_month_day date{selected_};
    char key[16];
    std::snprintf(key, sizeof(key), "%04d-%02u-%02u", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));

    for (const auto& entry : SpeakerMemos::Get().Entries(binding_)) {
        if (!std::string_view(entry.when).starts_with(key)) {
            continue;
        }
        auto* row = Surface(list_, 0, 0, kStripWidth, LV_SIZE_CONTENT);
        Rule(row, 0, 0, kStripWidth, 1);
        const auto time = entry.when[10] == ' ' ? std::string_view(entry.when + 11, 5) : std::string_view("全天");
        auto* time_label = Label(row, "", kMuted);
        lv_label_set_text_fmt(time_label, "%.*s", static_cast<int>(time.size()), time.data());
        lv_obj_set_pos(time_label, 14, 24);
        auto* content = Label(row, entry.content);
        lv_obj_set_width(content, 530);
        lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(content, 158, 24);
        lv_obj_set_style_pad_bottom(row, 22, LV_PART_MAIN);
    }
    if (!lv_obj_get_child_count(list_)) {
        Rule(list_, 0, 0, kStripWidth, 1);
        auto* empty = Label(list_, binding_ > 0 ? "当天暂无提醒" : "登录后可查看记事", kMuted);
        lv_obj_set_style_pad_top(empty, 25, LV_PART_MAIN);
    }
}

#endif
