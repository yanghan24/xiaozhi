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
constexpr int kDayWidth = 102;
constexpr int kStripWidth = 7 * kDayWidth;
constexpr int kStripHeight = 105;
constexpr int kListWidth = 710;
constexpr int kDotSize = 6;
constexpr int kDotBottomGap = 6;
constexpr sys_days kFirstDate{year{1} / January / 1};
constexpr sys_days kLastDate{year{9999} / December / 31};
constexpr int32_t kMaxOffset = ((kLastDate - kFirstDate).count() - (kStripWidth - 1) / kDayWidth) * kDayWidth;
constexpr int kMaxVelocity = 4600;
constexpr uint32_t kCoastTimeoutMs = 100;
constexpr const char* kWeekdays[] = {"一", "二", "三", "四", "五", "六", "日"};

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

int32_t OffsetForDate(sys_days date) {
    return (date - kFirstDate).count() * kDayWidth;
}

sys_days DateAtOffset(int32_t offset) {
    return kFirstDate + days(offset / kDayWidth);
}

std::array<char, 16> DateKey(sys_days date) {
    const year_month_day civil{date};
    std::array<char, 16> key{};
    std::snprintf(key.data(), key.size(), "%04d-%02u-%02u", static_cast<int>(civil.year()),
                  static_cast<unsigned>(civil.month()), static_cast<unsigned>(civil.day()));
    return key;
}
}  // namespace

void SpeakerCalendarView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    root_ = Panel(parent);
    Header(root_, "日历", SpeakerNavigation::OnBack, &navigation);
    month_ = Label(root_, "", kMuted);
    lv_obj_align(month_, LV_ALIGN_TOP_RIGHT, -45, 26);
    today_button_ = Button(root_, "今天", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->GoToToday();
    }, this);
    lv_obj_set_pos(today_button_, 100, 13);
    lv_obj_set_size(today_button_, 100, 52);
    CardBorder(today_button_);
    lv_obj_add_flag(today_button_, LV_OBJ_FLAG_HIDDEN);

    dates_ = Surface(root_, (kWidth - kStripWidth) / 2, 87, kStripWidth, kStripHeight);
    lv_obj_add_flag(dates_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(dates_, OnDateTouch, LV_EVENT_ALL, this);
    for (auto& cell : cells_) {
        cell.root = Surface(dates_, 0, 0, kDayWidth, kStripHeight);
        cell.weekday = Label(cell.root, "", kMuted);
        lv_obj_align(cell.weekday, LV_ALIGN_TOP_MID, 0, 6);
        cell.selection = Surface(cell.root, (kDayWidth - 78) / 2, 48, 78, kStripHeight - 48);
        lv_obj_set_style_radius(cell.selection, 15, LV_PART_MAIN);
        lv_obj_set_style_bg_color(cell.selection, lv_color_hex(kCyan), LV_PART_MAIN);
        cell.number = Label(cell.root, "");
        lv_obj_align(cell.number, LV_ALIGN_TOP_MID, 0, 56);
        cell.memo_dot = Surface(cell.root, (kDayWidth - kDotSize) / 2,
                                kStripHeight - kDotSize - kDotBottomGap, kDotSize, kDotSize);
        lv_obj_set_style_radius(cell.memo_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell.memo_dot, LV_OPA_COVER, LV_PART_MAIN);
    }

    constexpr int kSideMargin = (kWidth - kListWidth) / 2;
    list_ = Surface(root_, kSideMargin, 209, kListWidth, 239);
    lv_obj_add_flag(list_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    memo_hint_ = Surface(root_, 0, 0, kListWidth, 60);
    lv_obj_align(memo_hint_, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_set_style_radius(memo_hint_, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(memo_hint_, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(memo_hint_, LV_OPA_COVER, LV_PART_MAIN);
    CardBorder(memo_hint_);
    lv_obj_center(Label(memo_hint_, "请说「明天上午九点提醒我开会」", kMuted));
    lv_obj_add_flag(memo_hint_, LV_OBJ_FLAG_HIDDEN);
    clock_hint_ = Label(root_, "正在校时", kMuted);
    lv_obj_center(clock_hint_);
}

void SpeakerCalendarView::Show() {
    today_.reset();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    Update();
}

void SpeakerCalendarView::Hide() {
    lv_anim_delete(this, AnimateOffset);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    tracking_ = false;
}

void SpeakerCalendarView::Update() {
    if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const auto local = LocalDate();
    lv_obj_set_flag(clock_hint_, LV_OBJ_FLAG_HIDDEN, local.has_value());
    lv_obj_set_flag(dates_, LV_OBJ_FLAG_HIDDEN, !local);
    lv_obj_set_flag(month_, LV_OBJ_FLAG_HIDDEN, !local);
    lv_obj_set_flag(list_, LV_OBJ_FLAG_HIDDEN, !local);
    if (!local) {
        lv_anim_delete(this, AnimateOffset);
        lv_obj_add_flag(today_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(memo_hint_, LV_OBJ_FLAG_HIDDEN);
        tracking_ = false;
        today_.reset();
        return;
    }
    const bool day_changed = today_ != local;
    const bool follow_today = !today_ || (day_changed && selected_ == *today_ && DateIsVisible(*today_));
    today_ = local;
    if (follow_today) {
        GoToToday();
    } else if (RefreshEntries()) {
        RenderDates(true);
        RenderMemos();
    } else if (day_changed) {
        RenderDates();
    }
}

void SpeakerCalendarView::OnDateTouch(lv_event_t* event) {
    auto* view = static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    auto* input = lv_indev_active();
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        lv_anim_delete(view, AnimateOffset);
        lv_indev_get_point(input, &view->touch_start_);
        view->drag_start_offset_ = view->offset_;
        view->last_touch_x_ = view->touch_start_.x;
        view->last_motion_tick_ = lv_tick_get();
        view->velocity_ = 0;
        view->tracking_ = true;
        view->dragging_ = false;
    } else if (code == LV_EVENT_PRESS_LOST && view->tracking_) {
        view->tracking_ = false;
        view->Settle(0);
    } else if (view->tracking_ && (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED)) {
        lv_point_t point{};
        lv_indev_get_point(input, &point);
        view->DragTo(point);
        if (code == LV_EVENT_RELEASED) {
            view->tracking_ = false;
            view->FinishTouch(point);
        }
    }
}

void SpeakerCalendarView::DragTo(const lv_point_t& point) {
    const int dx = point.x - touch_start_.x;
    if (!dragging_ && (std::abs(dx) < 10 || std::abs(dx) <= std::abs(point.y - touch_start_.y))) {
        return;
    }
    dragging_ = true;
    const auto elapsed = lv_tick_elaps(last_motion_tick_);
    if (elapsed > 0 && point.x != last_touch_x_) {
        const int sample = elapsed > kCoastTimeoutMs ? 0 : (last_touch_x_ - point.x) * 1000 / static_cast<int>(elapsed);
        velocity_ = std::clamp(sample, -kMaxVelocity, kMaxVelocity);
        last_touch_x_ = point.x;
        last_motion_tick_ = lv_tick_get();
    }
    SetOffset(drag_start_offset_ - dx);
}

void SpeakerCalendarView::FinishTouch(const lv_point_t& point) {
    if (dragging_) {
        const bool moving = lv_tick_elaps(last_motion_tick_) <= kCoastTimeoutMs;
        Settle(moving ? velocity_ * 3 / 10 : 0);
        return;
    }
    lv_area_t area{};
    lv_obj_get_coords(dates_, &area);
    if (std::abs(point.x - touch_start_.x) < 18 && std::abs(point.y - touch_start_.y) < 18 &&
        point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2) {
        const Date date = DateAtOffset(offset_ + point.x - area.x1);
        if (const bool memos_changed = RefreshEntries(); date != selected_ || memos_changed) {
            selected_ = date;
            RenderDates(true);
            RenderMemos();
        }
    }
    Settle(0);
}

void SpeakerCalendarView::AnimateOffset(void* view, int32_t offset) {
    static_cast<SpeakerCalendarView*>(view)->SetOffset(offset);
}

void SpeakerCalendarView::SetOffset(int32_t offset) {
    const auto bounded = std::clamp(offset, int32_t{0}, kMaxOffset);
    if (bounded != offset_) {
        offset_ = bounded;
        RenderDates();
    }
}

void SpeakerCalendarView::Settle(int32_t coast) {
    const int32_t target = std::clamp((offset_ + coast + kDayWidth / 2) / kDayWidth * kDayWidth,
                                      int32_t{0}, kMaxOffset);
    if (target == offset_) {
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, this);
    lv_anim_set_exec_cb(&animation, AnimateOffset);
    lv_anim_set_values(&animation, offset_, target);
    lv_anim_set_duration(&animation, std::clamp<int32_t>(160 + std::abs(target - offset_) / 4, 160, 440));
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

void SpeakerCalendarView::GoToToday() {
    if (!today_) {
        return;
    }
    lv_anim_delete(this, AnimateOffset);
    tracking_ = false;
    selected_ = *today_;
    const auto monday = selected_ - days(DayInWeek(selected_));
    offset_ = std::clamp(OffsetForDate(monday), int32_t{0}, kMaxOffset);
    RefreshEntries();
    RenderDates(true);
    RenderMemos();
}

bool SpeakerCalendarView::DateIsVisible(Date date) const {
    const int32_t left = OffsetForDate(date) - offset_;
    return left < kStripWidth && left + kDayWidth > 0;
}

void SpeakerCalendarView::RenderDates(bool refresh) {
    const int first = offset_ / kDayWidth;
    const int partial = offset_ % kDayWidth;
    // Keep each visible date in the same cell until it scrolls out of the strip.
    for (int i = 0; i < static_cast<int>(cells_.size()); ++i) {
        auto& cell = cells_[(first + i) % cells_.size()];
        const Date date = kFirstDate + days(first + i);
        lv_obj_set_x(cell.root, i * kDayWidth - partial);
        if (refresh || cell.date != date) {
            RenderCell(cell, date);
        }
    }
    const year_month_day center{DateAtOffset(offset_ + kStripWidth / 2)};
    const year_month month = center.year() / center.month();
    if (month != visible_month_) {
        visible_month_ = month;
        lv_label_set_text_fmt(month_, "%04d年%u月", static_cast<int>(center.year()), static_cast<unsigned>(center.month()));
    }
    lv_obj_set_flag(today_button_, LV_OBJ_FLAG_HIDDEN, !today_ || DateIsVisible(*today_));
}

void SpeakerCalendarView::RenderCell(DateCell& cell, Date date) {
    cell.date = date;
    const year_month_day civil{date};
    UpdateLabel(cell.weekday, kWeekdays[DayInWeek(date)]);
    char number[4];
    std::snprintf(number, sizeof(number), "%u", static_cast<unsigned>(civil.day()));
    UpdateLabel(cell.number, number);
    const bool selected = date == selected_;
    lv_obj_set_style_bg_opa(cell.selection, selected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_color(cell.number, lv_color_hex(selected ? kDeep : kText), LV_PART_MAIN);
    const auto key = DateKey(date);
    const bool has_memo = std::any_of(entries_.begin(), entries_.end(), [&key](const auto& entry) {
        return std::string_view(entry.when).starts_with(key.data());
    });
    lv_obj_set_flag(cell.memo_dot, LV_OBJ_FLAG_HIDDEN, !has_memo);
    if (has_memo) {
        lv_obj_set_style_bg_color(cell.memo_dot, lv_color_hex(selected ? kDeep : kCyan), LV_PART_MAIN);
    }
}

bool SpeakerCalendarView::RefreshEntries() {
    const auto binding = SpeakerAccount::Get().MemoBindingVersion();
    const auto revision = SpeakerMemos::Get().Revision();
    if (binding == binding_ && revision == memo_revision_) {
        return false;
    }
    binding_ = binding;
    memo_revision_ = revision;
    entries_ = SpeakerMemos::Get().Entries(binding_);
    return true;
}

void SpeakerCalendarView::RenderMemos() {
    lv_obj_clean(list_);
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
    const auto key = DateKey(selected_);

    for (const auto& entry : entries_) {
        if (!std::string_view(entry.when).starts_with(key.data())) {
            continue;
        }
        auto* row = Surface(list_, 0, 0, kListWidth, LV_SIZE_CONTENT);
        Rule(row, 0, 0, kListWidth, 1);
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
    const bool empty_day = lv_obj_get_child_count(list_) == 0;
    lv_obj_set_flag(memo_hint_, LV_OBJ_FLAG_HIDDEN, !empty_day || binding_ <= 0);
    if (empty_day) {
        Rule(list_, 0, 0, kListWidth, 1);
        auto* empty = Label(list_, binding_ > 0 ? "当天暂无提醒" : "登录后可查看记事", kMuted);
        lv_obj_set_style_pad_top(empty, 25, LV_PART_MAIN);
    }
}

#endif
