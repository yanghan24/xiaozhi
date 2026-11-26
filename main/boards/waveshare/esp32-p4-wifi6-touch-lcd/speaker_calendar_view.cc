#include "speaker_calendar_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && LV_USE_CALENDAR

#include "speaker_account/speaker_account.h"
#include "speaker_account/speaker_clock.h"
#include "speaker_account/speaker_ui_widgets.h"
#include "speaker_memos.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>

using namespace SpeakerUi;
using namespace std::chrono;

namespace {
constexpr int kDayWidth = 102;
constexpr int kStripWidth = 7 * kDayWidth;
constexpr int kStripHeight = 118;
constexpr int kListWidth = 710;
constexpr int kDotSize = 8;
constexpr int kDotBottomGap = 11;
#if LV_USE_ROLLER
constexpr int kPickerFirstYear = 1900;
constexpr int kPickerLastYear = 2100;

std::string NumberOptions(int first, int last) {
    std::string options;
    for (int value = first; value <= last; ++value) {
        if (!options.empty()) options += '\n';
        options += std::to_string(value);
    }
    return options;
}

lv_obj_t* DateRoller(lv_obj_t* panel, const char* title, int x) {
    auto* caption = Label(panel, title, kMuted);
    lv_obj_set_pos(caption, x + 63, 87);
    auto* roller = lv_roller_create(panel);
    lv_obj_set_pos(roller, x, 130);
    lv_obj_set_width(roller, 155);
    lv_obj_set_style_text_font(roller, lv_obj_get_style_text_font(panel, LV_PART_MAIN), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(kMuted), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(kDeep), LV_PART_SELECTED);
    lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(roller, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, lv_color_hex(kDeep), LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, lv_color_hex(kCyan), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_border_color(roller, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 12, LV_PART_MAIN);
    lv_roller_set_visible_row_count(roller, 3);
    return roller;
}
#endif
constexpr sys_days kFirstDate{year{1} / January / 1};
constexpr sys_days kLastDate{year{9999} / December / 31};
constexpr int32_t kMaxOffset = ((kLastDate - kFirstDate).count() - (kStripWidth - 1) / kDayWidth) * kDayWidth;
constexpr int kMaxVelocity = 4600;
constexpr uint32_t kCoastTimeoutMs = 100;
constexpr const char* kWeekdays[] = {"一", "二", "三", "四", "五", "六", "日"};

std::optional<sys_days> LocalDate() {
    const auto now = SpeakerClock::Now();
    std::tm local{};
    if (!now || !localtime_r(&now, &local)) {
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
    lv_obj_set_pos(month_, 100, 24);
    today_button_ = Button(root_, "今天", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->GoToToday();
    }, this);
    lv_obj_set_pos(today_button_, 558, 16);
    lv_obj_set_size(today_button_, 80, 49);
    Outline(today_button_, kEdge);
#if LV_USE_ROLLER
    picker_button_ = Button(root_, "选日期", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->OpenDatePicker();
    }, this);
    lv_obj_set_pos(picker_button_, 647, 16);
    lv_obj_set_size(picker_button_, 109, 49);
    Outline(picker_button_, kEdge);
#endif

    dates_ = Surface(root_, (kWidth - kStripWidth) / 2, 84, kStripWidth, kStripHeight);
    lv_obj_add_flag(dates_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(dates_, OnDateTouch, LV_EVENT_ALL, this);
    for (auto& cell : cells_) {
        cell.root = Surface(dates_, 0, 0, kDayWidth, kStripHeight);
        cell.selection = Surface(cell.root, 3, 0, kDayWidth - 6, kStripHeight);
        lv_obj_set_style_radius(cell.selection, 14, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell.selection, LV_OPA_COVER, LV_PART_MAIN);
        cell.weekday = Label(cell.root, "", kMuted);
        lv_obj_align(cell.weekday, LV_ALIGN_TOP_MID, 0, 8);
        cell.number = Label(cell.root, "");
        lv_obj_align(cell.number, LV_ALIGN_CENTER, 0, 9);
        cell.memo_dot = Surface(cell.root, (kDayWidth - kDotSize) / 2,
                                kStripHeight - kDotSize - kDotBottomGap, kDotSize, kDotSize);
        lv_obj_set_style_radius(cell.memo_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell.memo_dot, LV_OPA_COVER, LV_PART_MAIN);
    }

    constexpr int kSideMargin = (kWidth - kListWidth) / 2;
    Rule(root_, kSideMargin, 219, kListWidth, 1);
    agenda_date_ = Label(root_, "");
    lv_obj_set_pos(agenda_date_, kSideMargin, 229);
    agenda_count_ = Label(root_, "", kCyan);
    lv_obj_align(agenda_count_, LV_ALIGN_TOP_RIGHT, -kSideMargin, 229);
    list_ = Surface(root_, kSideMargin, 280, kListWidth, 168);
    lv_obj_add_flag(list_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list_, lv_color_hex(kEdge), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_, LV_OPA_COVER, LV_PART_SCROLLBAR);

    memo_hint_ = Surface(root_, kSideMargin, 280, kListWidth, 168);
    lv_obj_set_style_radius(memo_hint_, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(memo_hint_, lv_color_hex(0x101E3C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(memo_hint_, LV_OPA_COVER, LV_PART_MAIN);
    CardBorder(memo_hint_);
    empty_title_ = Label(memo_hint_, "");
    lv_obj_align(empty_title_, LV_ALIGN_CENTER, 0, -26);
    empty_detail_ = Label(memo_hint_, "", kMuted);
    Scale(empty_detail_, 213);
    lv_obj_align(empty_detail_, LV_ALIGN_CENTER, 0, 25);
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
#if LV_USE_ROLLER
    CloseDatePicker();
#endif
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    tracking_ = false;
}

bool SpeakerCalendarView::CanLeave() const {
#if LV_USE_ROLLER
    return !picker_;
#else
    return true;
#endif
}

void SpeakerCalendarView::Update() {
    if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const auto local = LocalDate();
    lv_obj_set_flag(clock_hint_, LV_OBJ_FLAG_HIDDEN, local.has_value());
    lv_obj_set_flag(dates_, LV_OBJ_FLAG_HIDDEN, !local);
    lv_obj_set_flag(month_, LV_OBJ_FLAG_HIDDEN, !local);
    lv_obj_set_flag(today_button_, LV_OBJ_FLAG_HIDDEN, !local);
    lv_obj_set_flag(agenda_date_, LV_OBJ_FLAG_HIDDEN, !local);
#if LV_USE_ROLLER
    lv_obj_set_flag(picker_button_, LV_OBJ_FLAG_HIDDEN, !local);
#endif
    if (!local) {
        lv_anim_delete(this, AnimateOffset);
#if LV_USE_ROLLER
        CloseDatePicker();
#endif
        lv_obj_add_flag(list_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(agenda_count_, LV_OBJ_FLAG_HIDDEN);
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
    } else if (RefreshEntries() || day_changed) {
        RenderDates(true);
        RenderMemos();
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
    if (today_) {
        GoToDate(*today_);
    }
}

void SpeakerCalendarView::GoToDate(Date date) {
    lv_anim_delete(this, AnimateOffset);
    tracking_ = false;
    dragging_ = false;
    selected_ = date;
    const auto monday = selected_ - days(DayInWeek(selected_));
    offset_ = std::clamp(OffsetForDate(monday), int32_t{0}, kMaxOffset);
    RefreshEntries();
    RenderDates(true);
    RenderMemos();
}

#if LV_USE_ROLLER
void SpeakerCalendarView::CreateDatePicker() {
    picker_ = Surface(root_, 0, 0, kWidth, kHeight);
    lv_obj_add_flag(picker_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(picker_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(picker_, LV_OPA_60, LV_PART_MAIN);
    auto* panel = Surface(picker_, 109, 47, 582, 386);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101E3C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 18, LV_PART_MAIN);
    CardBorder(panel);
    lv_obj_align(Label(panel, "选择日期"), LV_ALIGN_TOP_MID, 0, 24);
    year_roller_ = DateRoller(panel, "年", 43);
    month_roller_ = DateRoller(panel, "月", 213);
    day_roller_ = DateRoller(panel, "日", 383);
    const auto years = NumberOptions(kPickerFirstYear, kPickerLastYear);
    const auto months = NumberOptions(1, 12);
    lv_roller_set_options(year_roller_, years.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_options(month_roller_, months.c_str(), LV_ROLLER_MODE_NORMAL);
    for (auto* roller : {year_roller_, month_roller_}) {
        lv_obj_add_event_cb(roller, [](lv_event_t* event) {
            auto* view = static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event));
            if (view->picker_) {
                view->RefreshPickerDays(1 + lv_roller_get_selected(view->day_roller_));
            }
        }, LV_EVENT_VALUE_CHANGED, this);
    }
    auto* cancel = Button(panel, "取消", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->CloseDatePicker();
    }, this);
    Outline(cancel, kEdge);
    lv_obj_set_pos(cancel, 43, 309);
    lv_obj_set_size(cancel, 235, 55);
    auto* confirm = Button(panel, "确定", [](lv_event_t* event) {
        static_cast<SpeakerCalendarView*>(lv_event_get_user_data(event))->ConfirmDatePicker();
    }, this, kCyan, kDeep);
    lv_obj_set_pos(confirm, 303, 309);
    lv_obj_set_size(confirm, 235, 55);
}

void SpeakerCalendarView::OpenDatePicker() {
    if (!today_ || picker_) {
        return;
    }
    lv_anim_delete(this, AnimateOffset);
    tracking_ = false;
    dragging_ = false;
    // Keep the year options out of memory when the dialog is closed.
    CreateDatePicker();
    const year_month_day date{selected_};
    const int y = std::clamp(static_cast<int>(date.year()), kPickerFirstYear, kPickerLastYear);
    lv_roller_set_selected(year_roller_, y - kPickerFirstYear, LV_ANIM_OFF);
    lv_roller_set_selected(month_roller_, static_cast<unsigned>(date.month()) - 1, LV_ANIM_OFF);
    RefreshPickerDays(static_cast<unsigned>(date.day()));
}

year_month SpeakerCalendarView::PickerMonth() const {
    return year{kPickerFirstYear + static_cast<int>(lv_roller_get_selected(year_roller_))} /
           month{1 + lv_roller_get_selected(month_roller_)};
}

void SpeakerCalendarView::RefreshPickerDays(unsigned preferred_day) {
    const auto last_day = static_cast<unsigned>((PickerMonth() / last).day());
    if (lv_roller_get_option_count(day_roller_) != last_day) {
        const auto options = NumberOptions(1, last_day);
        lv_roller_set_options(day_roller_, options.c_str(), LV_ROLLER_MODE_NORMAL);
    }
    lv_roller_set_selected(day_roller_, std::clamp(preferred_day, 1u, last_day) - 1, LV_ANIM_OFF);
}

void SpeakerCalendarView::ConfirmDatePicker() {
    const auto date = PickerMonth() / day{1 + lv_roller_get_selected(day_roller_)};
    if (!date.ok()) {
        return;
    }
    CloseDatePicker();
    GoToDate(Date{date});
}

void SpeakerCalendarView::CloseDatePicker() {
    if (!picker_) {
        return;
    }
    lv_obj_add_flag(picker_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(picker_);
    picker_ = nullptr;
    year_roller_ = nullptr;
    month_roller_ = nullptr;
    day_roller_ = nullptr;
}
#endif

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
}

void SpeakerCalendarView::RenderCell(DateCell& cell, Date date) {
    cell.date = date;
    const year_month_day civil{date};
    char weekday_text[12];
    std::snprintf(weekday_text, sizeof(weekday_text), "周%s", kWeekdays[DayInWeek(date)]);
    UpdateLabel(cell.weekday, weekday_text);
    char number[4];
    std::snprintf(number, sizeof(number), "%u", static_cast<unsigned>(civil.day()));
    UpdateLabel(cell.number, number);
    const bool selected = date == selected_;
    const bool is_today = today_ && date == *today_;
    lv_obj_set_style_bg_color(cell.selection, lv_color_hex(selected ? kCyan : 0x101E3C), LV_PART_MAIN);
    lv_obj_set_style_border_width(cell.selection, is_today ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cell.selection, lv_color_hex(is_today ? (selected ? kText : kCyan) : kEdge),
                                  LV_PART_MAIN);
    lv_obj_set_style_text_color(cell.weekday, lv_color_hex(selected ? kDeep : kMuted), LV_PART_MAIN);
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
    const year_month_day civil{selected_};
    lv_label_set_text_fmt(agenda_date_, "%u月%u日  星期%s%s", static_cast<unsigned>(civil.month()),
                          static_cast<unsigned>(civil.day()), kWeekdays[DayInWeek(selected_)],
                          today_ && selected_ == *today_ ? "  今天" : "");
    lv_obj_clean(list_);
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
    const auto key = DateKey(selected_);
    unsigned count = 0;
    for (const auto& entry : entries_) {
        if (!std::string_view(entry.when).starts_with(key.data())) {
            continue;
        }
        ++count;
        auto* row = Surface(list_, 0, 0, kListWidth - 12, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, 64, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(row, 12, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x102143), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        Rule(row, 12, 18, 3, 28);
        const auto time = entry.when[10] == ' ' ? std::string_view(entry.when + 11, 5) : std::string_view("全天");
        auto* time_label = Label(row, "", kCyan);
        lv_label_set_text_fmt(time_label, "%.*s", static_cast<int>(time.size()), time.data());
        lv_obj_set_pos(time_label, 28, 12);
        auto* content = Label(row, entry.content);
        lv_obj_set_width(content, kListWidth - 182);
        lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(content, 148, 12);
    }

    lv_label_set_text_fmt(agenda_count_, "%u 条提醒", count);
    lv_obj_set_flag(agenda_count_, LV_OBJ_FLAG_HIDDEN, binding_ <= 0);
    lv_obj_set_flag(list_, LV_OBJ_FLAG_HIDDEN, count == 0);
    lv_obj_set_flag(memo_hint_, LV_OBJ_FLAG_HIDDEN, count != 0);
    if (count == 0) {
        UpdateLabel(empty_title_, binding_ > 0 ? "当天暂无提醒" : "登录后可查看记事");
        UpdateLabel(empty_detail_, binding_ > 0 ? "请说「你好小智，明天上午九点提醒我开会」" : "请先在音箱上登录账号");
    }
}

#endif
