#include "standby_dashboard.h"

#include "application.h"
#include "board.h"
#include "device_control/device_control.h"
#include "features/music_player.h"
#include "features/pomodoro_timer.h"
#include "services/environment_service.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_timer.h>
#include <font_awesome.h>

namespace {
constexpr uint32_t kColorBackground = 0x050A14;
constexpr uint32_t kColorBackgroundDeep = 0x071525;
constexpr uint32_t kColorPanel = 0x061426;
constexpr uint32_t kColorPanelSecondary = 0x081B31;
constexpr uint32_t kColorCyan = 0x27E5FF;
constexpr uint32_t kColorBlue = 0x3487FF;
constexpr uint32_t kColorViolet = 0x8A6CFF;
constexpr uint32_t kColorText = 0xF4F8FF;
constexpr uint32_t kColorMuted = 0xA8B6CB;
constexpr uint32_t kColorGrid = 0x0C3553;
constexpr uint32_t kColorSegmentOff = 0x0A3145;
constexpr uint32_t kColorSun = 0xFFC21A;
constexpr uint32_t kColorFocus = 0xFF5A67;
constexpr uint32_t kColorAmber = 0xFFB020;
constexpr uint32_t kColorGreen = 0x5CFF73;
constexpr int64_t kEnvironmentUpdateIntervalUs = 10LL * 1000 * 1000;

lv_obj_t* CreateObject(lv_obj_t* parent, int x, int y, int width, int height) {
    lv_obj_t* object = lv_obj_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
    return object;
}

void MakeTransparent(lv_obj_t* object) {
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
}

lv_obj_t* CreateLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

void ReplaceLabelFonts(lv_obj_t* object, const lv_font_t* old_text_font,
                       const lv_font_t* new_text_font, const lv_font_t* old_icon_font,
                       const lv_font_t* new_icon_font) {
    if (lv_obj_check_type(object, &lv_label_class)) {
        const lv_font_t* current_font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        if (current_font == old_text_font) {
            lv_obj_set_style_text_font(object, new_text_font, 0);
        } else if (current_font == old_icon_font) {
            lv_obj_set_style_text_font(object, new_icon_font, 0);
        }
    }

    uint32_t child_count = lv_obj_get_child_cnt(object);
    for (uint32_t i = 0; i < child_count; ++i) {
        ReplaceLabelFonts(lv_obj_get_child(object, i), old_text_font, new_text_font, old_icon_font,
                          new_icon_font);
    }
}

lv_obj_t* CreateTechButton(lv_obj_t* parent, const lv_font_t* font, const char* text, int x, int y,
                           int width, int height, lv_color_t accent,
                           lv_obj_t** label_out = nullptr) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorPanelSecondary), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, 0);
    lv_obj_set_style_border_color(button, accent, 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, accent, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_PRESSED);

    lv_obj_t* label = CreateLabel(button, font, lv_color_hex(kColorText), text);
    lv_obj_center(label);
    if (label_out != nullptr) {
        *label_out = label;
    }
    return button;
}

void StylePanel(lv_obj_t* panel, uint32_t background, lv_opa_t opacity = LV_OPA_90) {
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(panel, opacity, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kColorBlue), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
}

void StyleRule(lv_obj_t* rule, uint32_t color, lv_opa_t opacity) {
    lv_obj_set_style_bg_color(rule, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(rule, opacity, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (hidden)
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void SetButtonEnabled(lv_obj_t* button, bool enabled) {
    if (enabled)
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    else
        lv_obj_add_state(button, LV_STATE_DISABLED);
}

void SetLabelText(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0)
        lv_label_set_text(label, text);
}

bool WifiSettingsAvailable() {
    const auto state = Application::GetInstance().GetDeviceState();
    return state != kDeviceStateUpgrading && state != kDeviceStateActivating &&
           state != kDeviceStateAudioTesting;
}

bool SettingsFontHasChinese(const lv_font_t* font) {
    for (uint32_t letter : {0x7f6eu, 0x8d26u, 0x8fdeu, 0x79efu}) {  // 置、账、连、积
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(font, &glyph, letter, 0) || glyph.is_placeholder)
            return false;
    }
    return true;
}

const char* BatteryIconForLevel(int level, bool charging) {
    if (charging) {
        return FONT_AWESOME_BATTERY_BOLT;
    }
    if (level < 20) {
        return FONT_AWESOME_BATTERY_EMPTY;
    }
    if (level < 40) {
        return FONT_AWESOME_BATTERY_QUARTER;
    }
    if (level < 60) {
        return FONT_AWESOME_BATTERY_HALF;
    }
    if (level < 80) {
        return FONT_AWESOME_BATTERY_THREE_QUARTERS;
    }
    return FONT_AWESOME_BATTERY_FULL;
}

constexpr uint32_t kLunarYearInfo[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2,
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977,
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970,
    0x06566, 0x0d4a0, 0x0ea50, 0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950,
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557,
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0,
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0,
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b5a0, 0x195a6,
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0,
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930,
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530,
    0x05aa0, 0x076a3, 0x096d0, 0x04bd7, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45,
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0,
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0,
    0x0a2e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4,
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160,
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252,
    0x0d520,
};

bool IsGregorianLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int64_t GregorianDayNumber(int year, int month, int day) {
    static constexpr int kDaysBeforeMonth[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
    };
    int64_t previous_year = year - 1;
    int64_t days =
        previous_year * 365 + previous_year / 4 - previous_year / 100 + previous_year / 400;
    days += kDaysBeforeMonth[month - 1] + day - 1;
    if (month > 2 && IsGregorianLeapYear(year)) {
        ++days;
    }
    return days;
}

int LunarLeapMonth(int year) { return static_cast<int>(kLunarYearInfo[year - 1900] & 0x0f); }

int LunarLeapDays(int year) {
    if (LunarLeapMonth(year) == 0) {
        return 0;
    }
    return (kLunarYearInfo[year - 1900] & 0x10000) != 0 ? 30 : 29;
}

int LunarMonthDays(int year, int month) {
    return (kLunarYearInfo[year - 1900] & (0x10000 >> month)) != 0 ? 30 : 29;
}

int LunarYearDays(int year) {
    int days = 348;
    for (uint32_t mask = 0x8000; mask > 0x08; mask >>= 1) {
        if ((kLunarYearInfo[year - 1900] & mask) != 0) {
            ++days;
        }
    }
    return days + LunarLeapDays(year);
}
}  // namespace

StandbyDashboard::StandbyDashboard(lv_obj_t* parent, const lv_font_t* text_font,
                                   const lv_font_t* icon_font)
    : text_font_(text_font), icon_font_(icon_font) {
    settings_chinese_ = SettingsFontHasChinese(text_font_);
    root_ = CreateObject(parent, 0, 0, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_grad_color(root_, lv_color_hex(kColorBackgroundDeep), 0);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);

    CreateBackground();
    CreateTopBar();
    CreateClockPanel();
    CreateWeatherPanel();
    CreatePulseIndicator();
    CreatePomodoroEntry();
    CreateCalendarEntry();
    CreateMusicEntry();
    CreateSettingsEntry();
    CreatePomodoroView();
    CreateCalendarView();
    CreateMusicView();
    CreateNotificationChip();

    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    notification_timer_ = lv_timer_create(NotificationTimerCallback, 3000, this);
    lv_timer_set_auto_delete(notification_timer_, false);
    lv_timer_pause(notification_timer_);
}

StandbyDashboard::~StandbyDashboard() {
    music_refresh_shutdown_.store(true, std::memory_order_release);
    while (music_refresh_in_progress_.load(std::memory_order_acquire)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    StopPulseAnimation();
    CloseSettingsView();
    DestroySettingsView();
    if (settings_shortcut_ != nullptr && lv_obj_is_valid(settings_shortcut_))
        lv_obj_delete(settings_shortcut_);
    if (notification_timer_ != nullptr) {
        lv_timer_delete(notification_timer_);
        notification_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_del(root_);
        root_ = nullptr;
    }
}

void StandbyDashboard::SetFonts(const lv_font_t* text_font, const lv_font_t* icon_font) {
    if (root_ == nullptr || text_font == nullptr || icon_font == nullptr) {
        return;
    }
    if (text_font_ == text_font && icon_font_ == icon_font) {
        return;
    }

    const bool chinese = SettingsFontHasChinese(text_font);
    const bool relabel_settings = settings_chinese_ != chinese;
    const bool reopen_settings = relabel_settings && settings_view_ != nullptr &&
                                 !lv_obj_has_flag(settings_view_, LV_OBJ_FLAG_HIDDEN);
    ReplaceLabelFonts(root_, text_font_, text_font, icon_font_, icon_font);
    if (relabel_settings)
        DestroySettingsView();
    else if (settings_view_ != nullptr)
        ReplaceLabelFonts(settings_view_, text_font_, text_font, icon_font_, icon_font);
    ReplaceLabelFonts(settings_shortcut_, text_font_, text_font, icon_font_, icon_font);
    text_font_ = text_font;
    icon_font_ = icon_font;
    settings_chinese_ = chinese;
    if (relabel_settings) {
        for (auto* entry : {settings_entry_button_, settings_shortcut_})
            SetLabelText(lv_obj_get_child(entry, 0), SettingsText("设置", "Setup"));
        if (reopen_settings)
            ShowSettingsView();
    }
    lv_obj_invalidate(root_);
}

void StandbyDashboard::CreateBackground() {
    for (int x = 20; x < 800; x += 52) {
        lv_obj_t* line = CreateObject(root_, x, 48, 1, 432);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }
    for (int y = 74; y < 480; y += 44) {
        lv_obj_t* line = CreateObject(root_, 0, y, 800, 1);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }

    lv_obj_t* top_rule = CreateObject(root_, 0, 47, 800, 1);
    StyleRule(top_rule, kColorBlue, LV_OPA_80);

    lv_obj_t* divider = CreateObject(root_, 399, 76, 2, 350);
    StyleRule(divider, kColorCyan, LV_OPA_70);

    lv_obj_t* divider_glow = CreateObject(root_, 397, 235, 6, 34);
    lv_obj_set_style_bg_color(divider_glow, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_bg_opa(divider_glow, LV_OPA_60, 0);
    lv_obj_set_style_border_width(divider_glow, 0, 0);
    lv_obj_set_style_radius(divider_glow, LV_RADIUS_CIRCLE, 0);

    lv_obj_t* bottom_left = CreateObject(root_, 0, 454, 180, 2);
    StyleRule(bottom_left, kColorBlue, LV_OPA_70);
    lv_obj_t* bottom_right = CreateObject(root_, 620, 454, 180, 2);
    StyleRule(bottom_right, kColorBlue, LV_OPA_70);
}

void StandbyDashboard::CreateTopBar() {
    lv_obj_t* location_icon =
        CreateLabel(root_, icon_font_, lv_color_hex(kColorCyan), FONT_AWESOME_LOCATION_DOT);
    lv_obj_set_pos(location_icon, 18, 8);

    city_label_ = CreateLabel(root_, text_font_, lv_color_hex(kColorText), "本地天气");
    lv_obj_set_pos(city_label_, 50, 6);
    lv_obj_set_width(city_label_, 250);
    lv_label_set_long_mode(city_label_, LV_LABEL_LONG_DOT);

    lv_obj_t* right_status = CreateObject(root_, 690, 5, 93, 38);
    MakeTransparent(right_status);
    lv_obj_set_flex_flow(right_status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right_status, 12, 0);

    network_label_ =
        CreateLabel(right_status, icon_font_, lv_color_hex(kColorText), FONT_AWESOME_WIFI_SLASH);

    battery_group_ = lv_obj_create(right_status);
    lv_obj_set_size(battery_group_, LV_SIZE_CONTENT, 36);
    MakeTransparent(battery_group_);
    lv_obj_remove_flag(battery_group_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(battery_group_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_flex_flow(battery_group_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(battery_group_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(battery_group_, 6, 0);

    battery_icon_label_ = CreateLabel(battery_group_, icon_font_, lv_color_hex(kColorCyan),
                                      FONT_AWESOME_BATTERY_FULL);
    battery_percent_label_ = CreateLabel(battery_group_, text_font_, lv_color_hex(kColorText), "");
    lv_obj_add_flag(battery_group_, LV_OBJ_FLAG_HIDDEN);
}

void StandbyDashboard::CreateClockPanel() {
    lv_obj_t* frame = CreateObject(root_, 18, 72, 364, 160);
    StylePanel(frame, kColorPanel, LV_OPA_50);
    lv_obj_set_style_border_color(frame, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_50, 0);

    CreateDigit(0, 38, 91);
    CreateDigit(1, 108, 91);
    CreateDigit(2, 204, 91);
    CreateDigit(3, 274, 91);

    for (int i = 0; i < 2; ++i) {
        colon_dots_[i] = CreateObject(root_, 182, 122 + i * 50, 11, 11);
        lv_obj_set_style_bg_color(colon_dots_[i], lv_color_hex(kColorCyan), 0);
        lv_obj_set_style_bg_opa(colon_dots_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(colon_dots_[i], 0, 0);
        lv_obj_set_style_radius(colon_dots_[i], 3, 0);
    }

    seconds_label_ = CreateLabel(root_, text_font_, lv_color_hex(kColorCyan), "--");
    lv_obj_set_pos(seconds_label_, 340, 174);
    lv_obj_set_width(seconds_label_, 42);
    lv_obj_set_style_text_align(seconds_label_, LV_TEXT_ALIGN_CENTER, 0);

    date_label_ = CreateLabel(root_, text_font_, lv_color_hex(kColorText), "等待网络校时");
    lv_obj_set_pos(date_label_, 24, 240);
    lv_obj_set_width(date_label_, 352);
    lv_obj_set_style_text_align(date_label_, LV_TEXT_ALIGN_CENTER, 0);
}

void StandbyDashboard::CreateDigit(int index, int x, int y) {
    constexpr int digit_width = 62;
    constexpr int digit_height = 122;
    constexpr int thickness = 10;
    constexpr int horizontal_width = 44;
    constexpr int vertical_height = 47;

    lv_obj_t* digit = CreateObject(root_, x, y, digit_width, digit_height);
    MakeTransparent(digit);

    const int segment_geometry[kSegmentCount][4] = {
        {9, 0, horizontal_width, thickness},
        {digit_width - thickness, 8, thickness, vertical_height},
        {digit_width - thickness, 67, thickness, vertical_height},
        {9, digit_height - thickness, horizontal_width, thickness},
        {0, 67, thickness, vertical_height},
        {0, 8, thickness, vertical_height},
        {9, 56, horizontal_width, thickness},
    };

    for (int segment = 0; segment < kSegmentCount; ++segment) {
        const auto& geometry = segment_geometry[segment];
        lv_obj_t* bar = CreateObject(digit, geometry[0], geometry[1], geometry[2], geometry[3]);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kColorSegmentOff), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_50, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, thickness / 2, 0);
        digit_segments_[index][segment] = bar;
    }
}

void StandbyDashboard::CreatePomodoroDigit(int index, int x, int y) {
    constexpr int digit_width = 46;
    constexpr int digit_height = 88;
    constexpr int thickness = 7;
    constexpr int horizontal_width = 32;
    constexpr int vertical_height = 33;

    lv_obj_t* digit = CreateObject(pomodoro_view_, x, y, digit_width, digit_height);
    MakeTransparent(digit);

    const int segment_geometry[kSegmentCount][4] = {
        {7, 0, horizontal_width, thickness},
        {digit_width - thickness, 6, thickness, vertical_height},
        {digit_width - thickness, 49, thickness, vertical_height},
        {7, digit_height - thickness, horizontal_width, thickness},
        {0, 49, thickness, vertical_height},
        {0, 6, thickness, vertical_height},
        {7, 40, horizontal_width, thickness},
    };

    for (int segment = 0; segment < kSegmentCount; ++segment) {
        const auto& geometry = segment_geometry[segment];
        lv_obj_t* bar = CreateObject(digit, geometry[0], geometry[1], geometry[2], geometry[3]);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kColorSegmentOff), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, thickness / 2, 0);
        pomodoro_digit_segments_[index][segment] = bar;
    }
}

void StandbyDashboard::CreateWeatherPanel() {
    lv_obj_t* panel = CreateObject(root_, 420, 72, 364, 356);
    StylePanel(panel, kColorPanelSecondary, LV_OPA_90);

    weather_icon_accent_ =
        CreateLabel(panel, icon_font_, lv_color_hex(kColorSun), FONT_AWESOME_SUN);
    lv_obj_set_pos(weather_icon_accent_, 80, 25);
    lv_obj_set_style_transform_scale(weather_icon_accent_, 390, 0);

    weather_icon_primary_ =
        CreateLabel(panel, icon_font_, lv_color_hex(kColorText), FONT_AWESOME_CLOUD);
    lv_obj_set_pos(weather_icon_primary_, 43, 55);
    lv_obj_set_style_transform_scale(weather_icon_primary_, 500, 0);

    lv_obj_t* icon_divider = CreateObject(panel, 134, 30, 1, 108);
    StyleRule(icon_divider, kColorCyan, LV_OPA_70);

    temperature_label_ = CreateLabel(panel, text_font_, lv_color_hex(kColorText), "--°C");
    lv_obj_set_pos(temperature_label_, 158, 30);
    lv_obj_set_width(temperature_label_, 184);
    lv_obj_set_style_text_align(temperature_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_scale(temperature_label_, 380, 0);

    weather_label_ = CreateLabel(panel, text_font_, lv_color_hex(kColorText), "等待天气");
    lv_obj_set_pos(weather_label_, 158, 104);
    lv_obj_set_width(weather_label_, 184);
    lv_obj_set_style_text_align(weather_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(weather_label_, LV_LABEL_LONG_DOT);

    lv_obj_t* horizontal_rule = CreateObject(panel, 20, 150, 324, 1);
    StyleRule(horizontal_rule, kColorCyan, LV_OPA_60);

    const int metric_x[] = {10, 122, 234};
    const char* metric_icons[] = {
        FONT_AWESOME_TEMPERATURE_HALF,
        FONT_AWESOME_CLOUD_RAIN,
        FONT_AWESOME_WIND,
    };
    const char* metric_titles[] = {"体感", "湿度", "风向"};
    lv_obj_t** metric_values[] = {&feels_like_label_, &humidity_label_, &wind_power_label_};

    for (int i = 0; i < 3; ++i) {
        lv_obj_t* metric = CreateObject(panel, metric_x[i], 164, 110, 108);
        MakeTransparent(metric);
        if (i > 0) {
            lv_obj_t* separator = CreateObject(panel, metric_x[i] - 2, 173, 1, 86);
            StyleRule(separator, kColorCyan, LV_OPA_50);
        }

        lv_obj_t* icon = CreateLabel(metric, icon_font_, lv_color_hex(kColorCyan), metric_icons[i]);
        lv_obj_set_pos(icon, 40, 0);

        lv_obj_t* title =
            CreateLabel(metric, text_font_, lv_color_hex(kColorMuted), metric_titles[i]);
        lv_obj_set_pos(title, 0, 34);
        lv_obj_set_width(title, 110);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        if (i == 2) {
            wind_title_label_ = title;
        }

        *metric_values[i] = CreateLabel(metric, text_font_, lv_color_hex(kColorText), "--");
        lv_obj_set_pos(*metric_values[i], 0, 69);
        lv_obj_set_width(*metric_values[i], 110);
        lv_obj_set_style_text_align(*metric_values[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(*metric_values[i], LV_LABEL_LONG_DOT);
    }

    lv_obj_t* air_capsule = CreateObject(panel, 40, 286, 284, 52);
    lv_obj_set_style_bg_color(air_capsule, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_opa(air_capsule, LV_OPA_70, 0);
    lv_obj_set_style_border_color(air_capsule, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_border_opa(air_capsule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(air_capsule, 1, 0);
    lv_obj_set_style_radius(air_capsule, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(air_capsule, 0, 0);
    lv_obj_set_flex_flow(air_capsule, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(air_capsule, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(air_capsule, 14, 0);

    lv_obj_t* air_title = CreateLabel(air_capsule, text_font_, lv_color_hex(kColorText), "空气");
    air_category_label_ = CreateLabel(air_capsule, text_font_, lv_color_hex(0x5CFF73), "--");
    air_value_label_ = CreateLabel(air_capsule, text_font_, lv_color_hex(kColorText), "--");
    (void)air_title;
}

void StandbyDashboard::CreatePulseIndicator() {
    pulse_ring_ = CreateObject(root_, 103, 304, 194, 72);
    lv_obj_set_style_bg_color(pulse_ring_, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_opa(pulse_ring_, LV_OPA_40, 0);
    lv_obj_set_style_border_color(pulse_ring_, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_border_width(pulse_ring_, 3, 0);
    lv_obj_set_style_border_opa(pulse_ring_, LV_OPA_70, 0);
    lv_obj_set_style_radius(pulse_ring_, LV_RADIUS_CIRCLE, 0);

    lv_obj_t* inner_ring = CreateObject(root_, 126, 318, 148, 44);
    lv_obj_set_style_bg_opa(inner_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(inner_ring, lv_color_hex(kColorViolet), 0);
    lv_obj_set_style_border_width(inner_ring, 1, 0);
    lv_obj_set_style_border_opa(inner_ring, LV_OPA_80, 0);
    lv_obj_set_style_radius(inner_ring, LV_RADIUS_CIRCLE, 0);

    const int heights[] = {20, 32, 45, 60, 45, 32, 20};
    for (int i = 0; i < 7; ++i) {
        lv_obj_t* bar = CreateObject(root_, 165 + i * 12, 340 - heights[i] / 2, 4, heights[i]);
        lv_obj_set_style_bg_color(bar, lv_color_hex(i == 3 ? kColorViolet : kColorCyan), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 2, 0);
    }

    lv_obj_t* standby_label =
        CreateLabel(root_, text_font_, lv_color_hex(kColorText), "筑音 · 待机中");
    lv_obj_set_pos(standby_label, 34, 395);
    lv_obj_set_width(standby_label, 332);
    lv_obj_set_style_text_align(standby_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* left_accent = CreateObject(root_, 68, 414, 42, 2);
    StyleRule(left_accent, kColorCyan, LV_OPA_80);
    lv_obj_t* right_accent = CreateObject(root_, 290, 414, 42, 2);
    StyleRule(right_accent, kColorCyan, LV_OPA_80);
}

void StandbyDashboard::CreatePomodoroEntry() {
    pomodoro_entry_button_ =
        CreateTechButton(root_, text_font_, "番茄钟", 304, 5, 110, 38, lv_color_hex(kColorCyan));
    lv_obj_add_event_cb(pomodoro_entry_button_, PomodoroEntryEventCallback, LV_EVENT_CLICKED, this);
}

void StandbyDashboard::CreateCalendarEntry() {
    calendar_entry_button_ =
        CreateTechButton(root_, text_font_, "日历", 420, 5, 84, 38, lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(calendar_entry_button_, CalendarEntryEventCallback, LV_EVENT_CLICKED, this);
}

void StandbyDashboard::CreateMusicEntry() {
    music_entry_button_ =
        CreateTechButton(root_, text_font_, "音乐", 510, 5, 84, 38, lv_color_hex(kColorViolet));
    lv_obj_add_event_cb(music_entry_button_, MusicEntryEventCallback, LV_EVENT_CLICKED, this);
}

void StandbyDashboard::CreateSettingsEntry() {
    auto open = [](lv_event_t* e) {
        static_cast<StandbyDashboard*>(lv_event_get_user_data(e))->ShowSettingsView();
    };
    const char* title = SettingsText("设置", "Setup");
    settings_entry_button_ =
        CreateTechButton(root_, text_font_, title, 600, 5, 84, 38, lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(settings_entry_button_, open, LV_EVENT_CLICKED, this);
    auto* layer = lv_display_get_layer_top(lv_obj_get_display(root_));
    settings_shortcut_ =
        CreateTechButton(layer, text_font_, title, 698, 5, 84, 38, lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(settings_shortcut_, open, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(settings_shortcut_, LV_OBJ_FLAG_HIDDEN);
}

void StandbyDashboard::SetMainEntryButtonsEnabled(bool enabled) {
    const std::array<lv_obj_t*, 4> buttons = {pomodoro_entry_button_, calendar_entry_button_,
                                              music_entry_button_, settings_entry_button_};
    for (auto* button : buttons) {
        if (button == nullptr) {
            continue;
        }
        if (enabled) {
            lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

void StandbyDashboard::CreatePomodoroView() {
    pomodoro_view_ = CreateObject(root_, 0, 0, 800, 480);
    lv_obj_set_style_bg_color(pomodoro_view_, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_grad_color(pomodoro_view_, lv_color_hex(kColorBackgroundDeep), 0);
    lv_obj_set_style_bg_grad_dir(pomodoro_view_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(pomodoro_view_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pomodoro_view_, 0, 0);
    lv_obj_set_style_pad_all(pomodoro_view_, 0, 0);

    for (int x = 20; x < 800; x += 52) {
        lv_obj_t* line = CreateObject(pomodoro_view_, x, 48, 1, 432);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }
    for (int y = 74; y < 480; y += 44) {
        lv_obj_t* line = CreateObject(pomodoro_view_, 0, y, 800, 1);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }

    lv_obj_t* top_rule = CreateObject(pomodoro_view_, 0, 47, 800, 1);
    StyleRule(top_rule, kColorBlue, LV_OPA_80);

    lv_obj_t* timer_icon =
        CreateLabel(pomodoro_view_, icon_font_, lv_color_hex(kColorFocus), FONT_AWESOME_CLOCK);
    lv_obj_set_pos(timer_icon, 18, 8);
    lv_obj_t* title = CreateLabel(pomodoro_view_, text_font_, lv_color_hex(kColorText), "番茄专注");
    lv_obj_set_pos(title, 54, 6);

    pomodoro_state_label_ =
        CreateLabel(pomodoro_view_, text_font_, lv_color_hex(kColorCyan), "选择专注时长");
    lv_obj_set_pos(pomodoro_state_label_, 286, 6);
    lv_obj_set_width(pomodoro_state_label_, 228);
    lv_obj_set_style_text_align(pomodoro_state_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* left_panel = CreateObject(pomodoro_view_, 18, 72, 424, 356);
    StylePanel(left_panel, kColorPanel, LV_OPA_50);
    lv_obj_set_style_border_color(left_panel, lv_color_hex(kColorCyan), 0);

    pomodoro_progress_arc_ = lv_arc_create(pomodoro_view_);
    lv_obj_set_pos(pomodoro_progress_arc_, 48, 80);
    lv_obj_set_size(pomodoro_progress_arc_, 350, 350);
    lv_arc_set_rotation(pomodoro_progress_arc_, 270);
    lv_arc_set_bg_angles(pomodoro_progress_arc_, 0, 360);
    lv_arc_set_range(pomodoro_progress_arc_, 0, 1000);
    lv_arc_set_value(pomodoro_progress_arc_, 0);
    lv_obj_remove_flag(pomodoro_progress_arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pomodoro_progress_arc_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_arc_color(pomodoro_progress_arc_, lv_color_hex(kColorSegmentOff),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_width(pomodoro_progress_arc_, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(pomodoro_progress_arc_, lv_color_hex(kColorFocus),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(pomodoro_progress_arc_, 9, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(pomodoro_progress_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(pomodoro_progress_arc_, 0, LV_PART_KNOB);

    lv_obj_t* inner_ring = CreateObject(pomodoro_view_, 84, 116, 278, 278);
    lv_obj_set_style_bg_opa(inner_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(inner_ring, lv_color_hex(kColorViolet), 0);
    lv_obj_set_style_border_width(inner_ring, 1, 0);
    lv_obj_set_style_border_opa(inner_ring, LV_OPA_60, 0);
    lv_obj_set_style_radius(inner_ring, LV_RADIUS_CIRCLE, 0);

    const int digit_x[kPomodoroDigitCount] = {62, 112, 182, 232, 302, 352};
    for (int i = 0; i < kPomodoroDigitCount; ++i) {
        CreatePomodoroDigit(i, digit_x[i], 184);
    }
    const int colon_x[2] = {165, 285};
    for (int colon = 0; colon < 2; ++colon) {
        for (int dot = 0; dot < 2; ++dot) {
            int index = colon * 2 + dot;
            pomodoro_colon_dots_[index] =
                CreateObject(pomodoro_view_, colon_x[colon], 210 + dot * 42, 8, 8);
            lv_obj_set_style_bg_color(pomodoro_colon_dots_[index], lv_color_hex(kColorFocus), 0);
            lv_obj_set_style_bg_opa(pomodoro_colon_dots_[index], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pomodoro_colon_dots_[index], 0, 0);
            lv_obj_set_style_radius(pomodoro_colon_dots_[index], 2, 0);
        }
    }

    pomodoro_detail_label_ = CreateLabel(pomodoro_view_, text_font_, lv_color_hex(kColorMuted),
                                         "选择快捷时长，或通过语音自定义");
    lv_obj_set_pos(pomodoro_detail_label_, 40, 329);
    lv_obj_set_width(pomodoro_detail_label_, 380);
    lv_obj_set_style_text_align(pomodoro_detail_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(pomodoro_detail_label_, LV_LABEL_LONG_WRAP);

    lv_obj_t* right_panel = CreateObject(pomodoro_view_, 462, 72, 322, 356);
    StylePanel(right_panel, kColorPanelSecondary, LV_OPA_90);

    lv_obj_t* shortcut_title =
        CreateLabel(right_panel, text_font_, lv_color_hex(kColorText), "快捷时长");
    lv_obj_set_pos(shortcut_title, 18, 14);

    const int preset_minutes[] = {15, 30, 45};
    const char* preset_labels[] = {"15m", "30m", "45m"};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* preset = CreateTechButton(right_panel, text_font_, preset_labels[i], 18 + i * 98,
                                            58, 90, 58, lv_color_hex(kColorCyan));
        lv_obj_set_user_data(preset, this);
        lv_obj_add_event_cb(preset, PomodoroPresetEventCallback, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(preset_minutes[i])));
    }

    lv_obj_t* divider = CreateObject(right_panel, 18, 137, 286, 1);
    StyleRule(divider, kColorCyan, LV_OPA_60);

    lv_obj_t* control_title =
        CreateLabel(right_panel, text_font_, lv_color_hex(kColorMuted), "计时控制");
    lv_obj_set_pos(control_title, 18, 151);

    pomodoro_pause_button_ = CreateTechButton(right_panel, text_font_, "待启动", 18, 195, 136, 62,
                                              lv_color_hex(kColorBlue), &pomodoro_pause_label_);
    lv_obj_add_event_cb(pomodoro_pause_button_, PomodoroPauseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* close_button = CreateTechButton(right_panel, text_font_, "关闭", 168, 195, 136, 62,
                                              lv_color_hex(kColorAmber));
    lv_obj_add_event_cb(close_button, PomodoroCloseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* voice_hint = CreateLabel(right_panel, text_font_, lv_color_hex(kColorMuted),
                                       "语音支持\n开始 · 暂停 · 继续 · 关闭");
    lv_obj_set_pos(voice_hint, 18, 281);
    lv_obj_set_width(voice_hint, 286);
    lv_obj_set_style_text_align(voice_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(voice_hint, LV_LABEL_LONG_WRAP);

    lv_obj_add_flag(pomodoro_view_, LV_OBJ_FLAG_HIDDEN);
}

void StandbyDashboard::CreateCalendarView() {
    calendar_view_ = CreateObject(root_, 0, 0, 800, 480);
    lv_obj_set_style_bg_color(calendar_view_, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_grad_color(calendar_view_, lv_color_hex(kColorBackgroundDeep), 0);
    lv_obj_set_style_bg_grad_dir(calendar_view_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(calendar_view_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(calendar_view_, 0, 0);
    lv_obj_set_style_pad_all(calendar_view_, 0, 0);

    for (int x = 20; x < 800; x += 52) {
        lv_obj_t* line = CreateObject(calendar_view_, x, 48, 1, 432);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }
    for (int y = 74; y < 480; y += 44) {
        lv_obj_t* line = CreateObject(calendar_view_, 0, y, 800, 1);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }
    lv_obj_t* top_rule = CreateObject(calendar_view_, 0, 47, 800, 1);
    StyleRule(top_rule, kColorBlue, LV_OPA_80);

    lv_obj_t* calendar_icon =
        CreateLabel(calendar_view_, icon_font_, lv_color_hex(kColorCyan), FONT_AWESOME_CALENDAR);
    lv_obj_set_pos(calendar_icon, 18, 8);
    lv_obj_t* title = CreateLabel(calendar_view_, text_font_, lv_color_hex(kColorText), "电子日历");
    lv_obj_set_pos(title, 54, 6);
    lv_obj_t* subtitle =
        CreateLabel(calendar_view_, text_font_, lv_color_hex(kColorCyan), "CALENDAR");
    lv_obj_set_pos(subtitle, 232, 6);
    lv_obj_set_style_transform_scale(subtitle, 160, 0);

    calendar_clock_label_ =
        CreateLabel(calendar_view_, text_font_, lv_color_hex(kColorText), "--:--");
    lv_obj_set_pos(calendar_clock_label_, 604, 10);
    lv_obj_set_width(calendar_clock_label_, 88);
    lv_obj_set_style_text_align(calendar_clock_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_scale(calendar_clock_label_, 190, 0);

    lv_obj_t* close_button = CreateTechButton(calendar_view_, text_font_, "关闭", 706, 5, 76, 38,
                                              lv_color_hex(kColorAmber));
    lv_obj_add_event_cb(close_button, CalendarCloseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* calendar_panel = CreateObject(calendar_view_, 18, 62, 532, 394);
    StylePanel(calendar_panel, kColorPanel, LV_OPA_80);
    lv_obj_set_style_border_color(calendar_panel, lv_color_hex(kColorCyan), 0);

    calendar_month_label_ =
        CreateLabel(calendar_panel, text_font_, lv_color_hex(kColorText), "2026 / 01  一月");
    lv_obj_set_pos(calendar_month_label_, 18, 12);
    lv_obj_set_width(calendar_month_label_, 248);

    lv_obj_t* previous_button =
        CreateTechButton(calendar_panel, icon_font_, FONT_AWESOME_ANGLE_LEFT, 276, 10, 48, 34,
                         lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(previous_button, CalendarPreviousMonthEventCallback, LV_EVENT_CLICKED,
                        this);

    lv_obj_t* next_button = CreateTechButton(calendar_panel, icon_font_, FONT_AWESOME_ANGLE_RIGHT,
                                             332, 10, 48, 34, lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(next_button, CalendarNextMonthEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* today_button = CreateTechButton(calendar_panel, text_font_, "今日", 390, 10, 124, 34,
                                              lv_color_hex(kColorGreen), &calendar_today_label_);
    lv_obj_set_style_transform_scale(calendar_today_label_, 190, 0);
    lv_obj_add_event_cb(today_button, CalendarTodayEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* month_rule = CreateObject(calendar_panel, 14, 55, 504, 1);
    StyleRule(month_rule, kColorCyan, LV_OPA_50);

    static constexpr const char* kWeekdayLabels[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int column = 0; column < 7; ++column) {
        lv_color_t color =
            lv_color_hex(column == 0 ? kColorFocus : (column == 6 ? kColorBlue : kColorMuted));
        lv_obj_t* weekday = CreateLabel(calendar_panel, text_font_, color, kWeekdayLabels[column]);
        lv_obj_set_pos(weekday, 14 + column * 72, 65);
        lv_obj_set_width(weekday, 64);
        lv_obj_set_style_text_align(weekday, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_transform_scale(weekday, 160, 0);
    }

    for (int row = 0; row <= 6; ++row) {
        lv_obj_t* rule = CreateObject(calendar_panel, 14, 88 + row * 43, 504, 1);
        StyleRule(rule, kColorGrid, LV_OPA_70);
    }
    for (int column = 1; column < 7; ++column) {
        lv_obj_t* rule = CreateObject(calendar_panel, 10 + column * 72, 88, 1, 258);
        StyleRule(rule, kColorGrid, LV_OPA_70);
    }

    for (int index = 0; index < kCalendarCellCount; ++index) {
        int row = index / 7;
        int column = index % 7;
        lv_obj_t* button = lv_button_create(calendar_panel);
        lv_obj_set_pos(button, 14 + column * 72, 91 + row * 43);
        lv_obj_set_size(button, 64, 38);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_outline_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(kColorCyan), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(button, CalendarDayEventCallback, LV_EVENT_CLICKED, this);

        lv_obj_t* label = CreateLabel(button, text_font_, lv_color_hex(kColorText), "--");
        lv_obj_center(label);
        lv_obj_set_style_transform_scale(label, 180, 0);
        calendar_day_buttons_[index] = button;
        calendar_day_labels_[index] = label;
    }

    lv_obj_t* footer_accent_left = CreateObject(calendar_panel, 34, 370, 94, 1);
    StyleRule(footer_accent_left, kColorCyan, LV_OPA_60);
    lv_obj_t* footer =
        CreateLabel(calendar_panel, text_font_, lv_color_hex(kColorMuted), "宜忌为本地生活建议");
    lv_obj_set_pos(footer, 116, 361);
    lv_obj_set_width(footer, 300);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(footer, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_transform_scale(footer, 150, 0);
    lv_obj_t* footer_accent_right = CreateObject(calendar_panel, 404, 370, 94, 1);
    StyleRule(footer_accent_right, kColorCyan, LV_OPA_60);

    lv_obj_t* info_panel = CreateObject(calendar_view_, 566, 62, 218, 394);
    StylePanel(info_panel, kColorPanelSecondary, LV_OPA_90);
    lv_obj_set_style_border_color(info_panel, lv_color_hex(kColorCyan), 0);

    lv_obj_t* selected_caption =
        CreateLabel(info_panel, text_font_, lv_color_hex(kColorMuted), "SELECTED DATE");
    lv_obj_set_pos(selected_caption, 16, 10);
    lv_obj_set_style_transform_scale(selected_caption, 150, 0);

    calendar_selected_date_label_ =
        CreateLabel(info_panel, text_font_, lv_color_hex(kColorText), "2026.01.01");
    lv_obj_set_pos(calendar_selected_date_label_, 16, 36);
    lv_obj_set_width(calendar_selected_date_label_, 186);

    calendar_selected_weekday_label_ =
        CreateLabel(info_panel, text_font_, lv_color_hex(kColorCyan), "星期四");
    lv_obj_set_pos(calendar_selected_weekday_label_, 108, 10);
    lv_obj_set_width(calendar_selected_weekday_label_, 100);
    lv_obj_set_style_text_align(calendar_selected_weekday_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_transform_scale(calendar_selected_weekday_label_, 160, 0);

    calendar_lunar_label_ =
        CreateLabel(info_panel, text_font_, lv_color_hex(kColorMuted), "农历信息");
    lv_obj_set_pos(calendar_lunar_label_, -41, 73);
    lv_obj_set_width(calendar_lunar_label_, 300);
    lv_obj_set_style_text_align(calendar_lunar_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(calendar_lunar_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_transform_scale(calendar_lunar_label_, 160, 0);

    lv_obj_t* info_rule = CreateObject(info_panel, 14, 98, 190, 1);
    StyleRule(info_rule, kColorCyan, LV_OPA_50);

    lv_obj_t* good_panel = CreateObject(info_panel, 12, 112, 194, 116);
    StylePanel(good_panel, 0x041A20, LV_OPA_90);
    lv_obj_set_style_border_color(good_panel, lv_color_hex(kColorGreen), 0);
    lv_obj_t* good_badge = CreateObject(good_panel, 10, 10, 44, 32);
    StylePanel(good_badge, 0x0A3A31, LV_OPA_COVER);
    lv_obj_set_style_border_color(good_badge, lv_color_hex(kColorGreen), 0);
    lv_obj_t* good_badge_label =
        CreateLabel(good_badge, text_font_, lv_color_hex(kColorGreen), "宜");
    lv_obj_center(good_badge_label);
    lv_obj_set_style_transform_scale(good_badge_label, 190, 0);
    lv_obj_t* good_caption =
        CreateLabel(good_panel, text_font_, lv_color_hex(kColorMuted), "TODAY'S GOOD");
    lv_obj_set_pos(good_caption, 66, 16);
    lv_obj_set_style_transform_scale(good_caption, 140, 0);
    calendar_good_label_ =
        CreateLabel(good_panel, text_font_, lv_color_hex(kColorText), "规划  学习\n整理  运动");
    lv_obj_set_pos(calendar_good_label_, 14, 52);
    lv_obj_set_width(calendar_good_label_, 166);
    lv_obj_set_style_text_align(calendar_good_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(calendar_good_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_transform_scale(calendar_good_label_, 180, 0);

    lv_obj_t* avoid_panel = CreateObject(info_panel, 12, 242, 194, 136);
    StylePanel(avoid_panel, 0x201015, LV_OPA_90);
    lv_obj_set_style_border_color(avoid_panel, lv_color_hex(kColorFocus), 0);
    lv_obj_t* avoid_badge = CreateObject(avoid_panel, 10, 10, 44, 32);
    StylePanel(avoid_badge, 0x3A1720, LV_OPA_COVER);
    lv_obj_set_style_border_color(avoid_badge, lv_color_hex(kColorFocus), 0);
    lv_obj_t* avoid_badge_label =
        CreateLabel(avoid_badge, text_font_, lv_color_hex(kColorFocus), "忌");
    lv_obj_center(avoid_badge_label);
    lv_obj_set_style_transform_scale(avoid_badge_label, 190, 0);
    lv_obj_t* avoid_caption =
        CreateLabel(avoid_panel, text_font_, lv_color_hex(kColorMuted), "TODAY'S AVOID");
    lv_obj_set_pos(avoid_caption, 66, 16);
    lv_obj_set_style_transform_scale(avoid_caption, 140, 0);
    calendar_avoid_label_ =
        CreateLabel(avoid_panel, text_font_, lv_color_hex(kColorText), "熬夜  拖延\n久坐  分心");
    lv_obj_set_pos(calendar_avoid_label_, 14, 58);
    lv_obj_set_width(calendar_avoid_label_, 166);
    lv_obj_set_style_text_align(calendar_avoid_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(calendar_avoid_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_transform_scale(calendar_avoid_label_, 180, 0);

    lv_obj_add_flag(calendar_view_, LV_OBJ_FLAG_HIDDEN);
    ReturnCalendarToToday();
}

void StandbyDashboard::CreateMusicView() {
    music_view_ = CreateObject(root_, 0, 0, 800, 480);
    lv_obj_set_style_bg_color(music_view_, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_grad_color(music_view_, lv_color_hex(kColorBackgroundDeep), 0);
    lv_obj_set_style_bg_grad_dir(music_view_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(music_view_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(music_view_, 0, 0);
    lv_obj_set_style_pad_all(music_view_, 0, 0);

    for (int x = 20; x < 800; x += 52) {
        lv_obj_t* line = CreateObject(music_view_, x, 48, 1, 432);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }
    for (int y = 74; y < 480; y += 44) {
        lv_obj_t* line = CreateObject(music_view_, 0, y, 800, 1);
        StyleRule(line, kColorGrid, LV_OPA_20);
    }
    lv_obj_t* top_rule = CreateObject(music_view_, 0, 47, 800, 1);
    StyleRule(top_rule, kColorViolet, LV_OPA_80);

    lv_obj_t* music_icon =
        CreateLabel(music_view_, icon_font_, lv_color_hex(kColorViolet), FONT_AWESOME_MUSIC);
    lv_obj_set_pos(music_icon, 18, 8);
    lv_obj_t* title = CreateLabel(music_view_, text_font_, lv_color_hex(kColorText), "本地音乐");
    lv_obj_set_pos(title, 54, 6);
    lv_obj_t* subtitle =
        CreateLabel(music_view_, text_font_, lv_color_hex(kColorCyan), "LOCAL PLAYER");
    lv_obj_set_pos(subtitle, 250, 6);
    lv_obj_set_style_transform_scale(subtitle, 150, 0);

    lv_obj_t* close_button = CreateTechButton(music_view_, text_font_, "关闭", 706, 5, 76, 38,
                                              lv_color_hex(kColorAmber));
    lv_obj_add_event_cb(close_button, MusicCloseEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* playlist_panel = CreateObject(music_view_, 18, 62, 360, 394);
    StylePanel(playlist_panel, kColorPanel, LV_OPA_90);
    lv_obj_set_style_border_color(playlist_panel, lv_color_hex(kColorCyan), 0);

    lv_obj_t* playlist_title =
        CreateLabel(playlist_panel, text_font_, lv_color_hex(kColorText), "播放列表");
    lv_obj_set_pos(playlist_title, 16, 10);
    music_playlist_count_label_ =
        CreateLabel(playlist_panel, text_font_, lv_color_hex(kColorMuted), "曲库 --");
    lv_obj_set_pos(music_playlist_count_label_, 172, 14);
    lv_obj_set_width(music_playlist_count_label_, 122);
    lv_obj_set_style_text_align(music_playlist_count_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_transform_scale(music_playlist_count_label_, 160, 0);

    lv_obj_t* refresh_button =
        CreateTechButton(playlist_panel, icon_font_, FONT_AWESOME_ARROWS_ROTATE, 304, 8, 42, 38,
                         lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(refresh_button, MusicRefreshEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* playlist_rule = CreateObject(playlist_panel, 12, 54, 336, 1);
    StyleRule(playlist_rule, kColorCyan, LV_OPA_50);

    music_playlist_container_ = lv_obj_create(playlist_panel);
    lv_obj_set_pos(music_playlist_container_, 8, 62);
    lv_obj_set_size(music_playlist_container_, 344, 322);
    lv_obj_set_style_bg_opa(music_playlist_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_playlist_container_, 0, 0);
    lv_obj_set_style_radius(music_playlist_container_, 0, 0);
    lv_obj_set_style_pad_all(music_playlist_container_, 4, 0);
    lv_obj_set_style_pad_row(music_playlist_container_, 5, 0);
    lv_obj_set_flex_flow(music_playlist_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(music_playlist_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(music_playlist_container_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(music_playlist_container_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(music_playlist_container_, lv_color_hex(kColorCyan),
                              LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(music_playlist_container_, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(music_playlist_container_, 4, LV_PART_SCROLLBAR);

    music_empty_label_ =
        CreateLabel(playlist_panel, text_font_, lv_color_hex(kColorMuted), "正在读取本地曲库");
    lv_obj_set_pos(music_empty_label_, 20, 172);
    lv_obj_set_width(music_empty_label_, 320);
    lv_obj_set_style_text_align(music_empty_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(music_empty_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_transform_scale(music_empty_label_, 180, 0);

    lv_obj_t* player_panel = CreateObject(music_view_, 394, 62, 390, 394);
    StylePanel(player_panel, kColorPanelSecondary, LV_OPA_90);
    lv_obj_set_style_border_color(player_panel, lv_color_hex(kColorViolet), 0);

    lv_obj_t* playing_caption =
        CreateLabel(player_panel, text_font_, lv_color_hex(kColorMuted), "NOW PLAYING");
    lv_obj_set_pos(playing_caption, 18, 10);
    lv_obj_set_style_transform_scale(playing_caption, 150, 0);

    music_state_label_ = CreateLabel(player_panel, text_font_, lv_color_hex(kColorCyan), "待播放");
    lv_obj_set_pos(music_state_label_, 224, 10);
    lv_obj_set_width(music_state_label_, 146);
    lv_obj_set_style_text_align(music_state_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_transform_scale(music_state_label_, 170, 0);

    lv_obj_t* disc_outer = CreateObject(player_panel, 22, 62, 112, 112);
    lv_obj_set_style_bg_opa(disc_outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(disc_outer, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_border_width(disc_outer, 2, 0);
    lv_obj_set_style_border_opa(disc_outer, LV_OPA_70, 0);
    lv_obj_set_style_radius(disc_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_color(disc_outer, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_shadow_width(disc_outer, 16, 0);
    lv_obj_set_style_shadow_opa(disc_outer, LV_OPA_20, 0);

    lv_obj_t* disc_inner = CreateObject(player_panel, 42, 82, 72, 72);
    lv_obj_set_style_bg_color(disc_inner, lv_color_hex(kColorViolet), 0);
    lv_obj_set_style_bg_opa(disc_inner, LV_OPA_20, 0);
    lv_obj_set_style_border_color(disc_inner, lv_color_hex(kColorViolet), 0);
    lv_obj_set_style_border_width(disc_inner, 1, 0);
    lv_obj_set_style_radius(disc_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_t* disc_icon =
        CreateLabel(player_panel, icon_font_, lv_color_hex(kColorText), FONT_AWESOME_MUSIC);
    lv_obj_set_pos(disc_icon, 63, 102);

    lv_obj_t* orbit_node = CreateObject(player_panel, 119, 112, 8, 8);
    lv_obj_set_style_bg_color(orbit_node, lv_color_hex(kColorGreen), 0);
    lv_obj_set_style_bg_opa(orbit_node, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(orbit_node, 0, 0);
    lv_obj_set_style_radius(orbit_node, LV_RADIUS_CIRCLE, 0);

    music_track_name_label_ =
        CreateLabel(player_panel, text_font_, lv_color_hex(kColorText), "选择一首音乐");
    lv_obj_set_pos(music_track_name_label_, 150, 61);
    lv_obj_set_width(music_track_name_label_, 218);
    lv_label_set_long_mode(music_track_name_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(music_track_name_label_, 6000, 0);
    lv_obj_set_style_transform_scale(music_track_name_label_, 190, 0);

    music_track_meta_label_ =
        CreateLabel(player_panel, text_font_, lv_color_hex(kColorMuted), "SD 本地曲库");
    lv_obj_set_pos(music_track_meta_label_, 150, 104);
    lv_obj_set_width(music_track_meta_label_, 218);
    lv_label_set_long_mode(music_track_meta_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_transform_scale(music_track_meta_label_, 150, 0);

    lv_obj_t* spectrum_frame = CreateObject(player_panel, 150, 133, 212, 52);
    MakeTransparent(spectrum_frame);
    for (int i = 0; i < kMusicSpectrumBarCount; ++i) {
        lv_obj_t* bar = CreateObject(spectrum_frame, 5 + i * 23, 44, 7, 6);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kColorSegmentOff), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 3, 0);
        music_spectrum_bars_[i] = bar;
    }

    lv_obj_t* player_rule = CreateObject(player_panel, 18, 204, 354, 1);
    StyleRule(player_rule, kColorViolet, LV_OPA_50);

    music_progress_bar_ = lv_bar_create(player_panel);
    lv_obj_set_pos(music_progress_bar_, 24, 224);
    lv_obj_set_size(music_progress_bar_, 342, 10);
    lv_bar_set_range(music_progress_bar_, 0, 1000);
    lv_bar_set_value(music_progress_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(kColorSegmentOff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(music_progress_bar_, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(music_progress_bar_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(music_progress_bar_, lv_color_hex(kColorCyan), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(music_progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(music_progress_bar_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    music_elapsed_label_ =
        CreateLabel(player_panel, text_font_, lv_color_hex(kColorMuted), "00:00");
    lv_obj_set_pos(music_elapsed_label_, 24, 240);
    lv_obj_set_style_transform_scale(music_elapsed_label_, 150, 0);

    lv_obj_t* previous_button =
        CreateTechButton(player_panel, icon_font_, FONT_AWESOME_BACKWARD_STEP, 44, 282, 82, 70,
                         lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(previous_button, MusicPreviousEventCallback, LV_EVENT_CLICKED, this);

    music_play_pause_button_ =
        CreateTechButton(player_panel, icon_font_, FONT_AWESOME_PLAY, 139, 274, 112, 86,
                         lv_color_hex(kColorCyan), &music_play_pause_label_);
    lv_obj_add_event_cb(music_play_pause_button_, MusicPlayPauseEventCallback, LV_EVENT_CLICKED,
                        this);

    lv_obj_t* next_button = CreateTechButton(player_panel, icon_font_, FONT_AWESOME_FORWARD_STEP,
                                             264, 282, 82, 70, lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(next_button, MusicNextEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_add_flag(music_view_, LV_OBJ_FLAG_HIDDEN);
    UpdateMusicView();
}

void StandbyDashboard::CreateSettingsView() {
    auto* layer = lv_display_get_layer_top(lv_obj_get_display(root_));
    settings_view_ = CreateObject(layer, 0, 0, 800, 480);
    lv_obj_add_flag(settings_view_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(settings_view_, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_grad_color(settings_view_, lv_color_hex(kColorBackgroundDeep), 0);
    lv_obj_set_style_bg_grad_dir(settings_view_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(settings_view_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_view_, 0, 0);
    lv_obj_set_style_pad_all(settings_view_, 0, 0);
    lv_obj_set_style_radius(settings_view_, 0, 0);

    auto label = [this](lv_obj_t* parent, const char* text, int x, int y, uint32_t color) {
        auto* obj = CreateLabel(parent, text_font_, lv_color_hex(color), text);
        lv_obj_set_pos(obj, x, y);
        return obj;
    };
    auto* icon =
        CreateLabel(settings_view_, icon_font_, lv_color_hex(kColorBlue), FONT_AWESOME_GEAR);
    lv_obj_set_pos(icon, 18, 8);
    label(settings_view_, SettingsText("设置", "Setup"), 54, 6, kColorText);
    StyleRule(CreateObject(settings_view_, 0, 47, 800, 1), kColorBlue, LV_OPA_80);
    settings_factory_reset_button_ =
        CreateTechButton(settings_view_, text_font_, SettingsText("恢复出厂", "Factory reset"), 496,
                         5, 184, 38, lv_color_hex(kColorAmber));
    lv_obj_add_event_cb(
        settings_factory_reset_button_,
        [](lv_event_t* e) {
            static_cast<StandbyDashboard*>(lv_event_get_user_data(e))
                ->ShowFactoryResetConfirmation();
        },
        LV_EVENT_CLICKED, this);
    SetHidden(settings_factory_reset_button_, !DeviceControl::GetInstance().Enabled());
    auto* close = CreateTechButton(settings_view_, text_font_, SettingsText("关闭", "Back"), 698, 5,
                                   84, 38, lv_color_hex(kColorAmber));
    lv_obj_add_event_cb(
        close,
        [](lv_event_t* e) {
            static_cast<StandbyDashboard*>(lv_event_get_user_data(e))->CloseSettingsView();
        },
        LV_EVENT_CLICKED, this);

    auto* account = CreateObject(settings_view_, 18, 62, 374, 394);
    StylePanel(account, kColorPanel);
    lv_obj_set_style_border_color(account, lv_color_hex(kColorCyan), 0);
    label(account, SettingsText("AI 集市", "AI Market"), 18, 12, kColorCyan);
    StyleRule(CreateObject(account, 18, 54, 336, 1), kColorCyan, LV_OPA_50);
    settings_account_state_ = label(account, "", 18, 70, kColorMuted);
    lv_obj_set_width(settings_account_state_, 336);
    label(account, SettingsText("用户名", "User"), 18, 136, kColorMuted);
    settings_username_ = label(account, "", 135, 136, kColorText);
    label(account, SettingsText("积分", "Points"), 18, 190, kColorMuted);
    settings_balance_ = label(account, "", 135, 190, kColorCyan);
    for (auto* value : {settings_username_, settings_balance_}) {
        lv_obj_set_width(value, 218);
        lv_label_set_long_mode(value, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }

    settings_refresh_button_ = CreateTechButton(account, text_font_, "", 18, 250, 116, 46,
                                                lv_color_hex(kColorBlue), &settings_refresh_label_);
    lv_obj_add_event_cb(
        settings_refresh_button_,
        [](lv_event_t* e) {
            auto* self = static_cast<StandbyDashboard*>(lv_event_get_user_data(e));
            if (self->sign_out_confirmation_)
                self->sign_out_confirmation_ = 0;
            else
                DeviceControl::GetInstance().RefreshAccountDetails();
            self->UpdateSettingsView();
        },
        LV_EVENT_CLICKED, this);
    settings_consent_button_ =
        CreateTechButton(account, text_font_, SettingsText("扫码授权", "Authorize"), 146, 250, 208,
                         46, lv_color_hex(kColorCyan));
    lv_obj_add_event_cb(
        settings_consent_button_,
        [](lv_event_t* e) {
            auto* self = static_cast<StandbyDashboard*>(lv_event_get_user_data(e));
            self->CloseSettingsView();
            DeviceControl::GetInstance().RequestAccount();
        },
        LV_EVENT_CLICKED, this);
    settings_sign_out_button_ =
        CreateTechButton(account, text_font_, "", 18, 314, 336, 56, lv_color_hex(kColorAmber),
                         &settings_sign_out_label_);
    lv_obj_add_event_cb(
        settings_sign_out_button_,
        [](lv_event_t* e) {
            static_cast<StandbyDashboard*>(lv_event_get_user_data(e))->HandleSettingsSignOut();
        },
        LV_EVENT_CLICKED, this);

    auto* wifi = CreateObject(settings_view_, 408, 62, 374, 394);
    StylePanel(wifi, kColorPanel);
    label(wifi, "Wi-Fi", 18, 12, kColorCyan);
    StyleRule(CreateObject(wifi, 18, 54, 336, 1), kColorBlue, LV_OPA_50);
    settings_wifi_state_ = label(wifi, "", 18, 70, kColorText);
    settings_wifi_detail_ = label(wifi, "", 18, 121, kColorMuted);
    lv_obj_set_width(settings_wifi_detail_, 336);
    lv_obj_set_height(settings_wifi_detail_, 114);
    lv_label_set_long_mode(settings_wifi_detail_, LV_LABEL_LONG_DOT);
    settings_wifi_button_ =
        CreateTechButton(wifi, text_font_, "", 18, 250, 336, 46, lv_color_hex(kColorCyan),
                         &settings_wifi_button_label_);
    lv_obj_add_event_cb(
        settings_wifi_button_,
        [](lv_event_t* e) {
            const auto state = Board::GetInstance().GetWifiLinkView().state;
            if (!WifiSettingsAvailable() || state == WifiLinkState::Unsupported)
                return;
            lv_obj_add_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_DISABLED);
            Application::GetInstance().Schedule([state]() {
                auto& board = Board::GetInstance();
                if (WifiSettingsAvailable() &&
                    board.GetWifiLinkView().state != WifiLinkState::Unsupported)
                    board.SetWifiEnabled(state == WifiLinkState::Off);
            });
        },
        LV_EVENT_CLICKED, this);
    settings_wifi_setup_button_ =
        CreateTechButton(wifi, text_font_, SettingsText("更换网络", "Change network"), 18, 314, 336,
                         56, lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(
        settings_wifi_setup_button_,
        [](lv_event_t* e) {
            lv_obj_add_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_DISABLED);
            Application::GetInstance().Schedule([]() {
                if (WifiSettingsAvailable())
                    Board::GetInstance().ConfigureWifi();
            });
        },
        LV_EVENT_CLICKED, this);

    settings_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<StandbyDashboard*>(lv_timer_get_user_data(timer))->UpdateSettingsView();
        },
        250, this);
}

void StandbyDashboard::DestroySettingsView() {
    CloseFactoryResetConfirmation();
    if (settings_timer_ != nullptr) {
        lv_timer_delete(settings_timer_);
        settings_timer_ = nullptr;
    }
    if (settings_view_ != nullptr && lv_obj_is_valid(settings_view_))
        lv_obj_delete(settings_view_);
    settings_view_ = nullptr;
}

void StandbyDashboard::SetSettingsShortcutVisible(bool visible) {
    settings_shortcut_needed_ = visible;
    const bool settings_open =
        settings_view_ != nullptr && !lv_obj_has_flag(settings_view_, LV_OBJ_FLAG_HIDDEN);
    SetHidden(settings_shortcut_, settings_open || !visible);
    if (settings_open)
        lv_obj_move_foreground(settings_view_);
    else if (visible)
        lv_obj_move_foreground(settings_shortcut_);
}

void StandbyDashboard::ShowSettingsView() {
    if (settings_view_ == nullptr)
        CreateSettingsView();
    sign_out_confirmation_ = 0;
    CloseFactoryResetConfirmation();
    lv_obj_remove_flag(settings_view_, LV_OBJ_FLAG_HIDDEN);
    SetSettingsShortcutVisible(settings_shortcut_needed_);
    DeviceControl::GetInstance().ShowAccountDetails(true);
    UpdateSettingsView();
    lv_timer_resume(settings_timer_);
}

void StandbyDashboard::CloseSettingsView() {
    if (settings_view_ == nullptr || lv_obj_has_flag(settings_view_, LV_OBJ_FLAG_HIDDEN))
        return;
    lv_timer_pause(settings_timer_);
    lv_obj_add_flag(settings_view_, LV_OBJ_FLAG_HIDDEN);
    sign_out_confirmation_ = 0;
    CloseFactoryResetConfirmation();
    DeviceControl::GetInstance().ShowAccountDetails(false);
    SetSettingsShortcutVisible(settings_shortcut_needed_);
}

void StandbyDashboard::UpdateSettingsView() {
    const bool can_reset = Application::GetInstance().CanRestoreFactorySettings();
    SetButtonEnabled(settings_factory_reset_button_, can_reset);
    if (!can_reset)
        CloseFactoryResetConfirmation();
    const auto wifi = Board::GetInstance().GetWifiLinkView();
    auto account = DeviceControl::GetInstance().GetAccountView();
    if (wifi.state != WifiLinkState::Connected && wifi.state != WifiLinkState::Unsupported) {
        const auto sign_out = account.sign_out;
        account = {};
        account.sign_out = sign_out;
        account.status = DeviceAccountStatus::Offline;
    }
    if (!account.can_sign_out || account.permission_version != sign_out_confirmation_)
        sign_out_confirmation_ = 0;
    const bool confirm = sign_out_confirmation_ != 0;
    const bool pending = account.sign_out == DeviceSignOutStatus::Pending;
    const char* status = SettingsText("账号信息暂不可用", "Account unavailable");
    if (pending) {
        status = account.status == DeviceAccountStatus::Offline
                     ? SettingsText("联网后继续解绑", "Reconnect to unbind")
                     : SettingsText("正在退出并解绑", "Unbinding");
    } else if (confirm) {
        status = SettingsText("解绑后其他账号可扫码登录", "Other accounts can sign in after unbinding");
    } else if (account.can_sign_out && account.sign_out == DeviceSignOutStatus::Unsupported) {
        status = SettingsText("后台需升级以支持解绑", "Server update needed to unbind");
    } else if (account.can_sign_out && account.sign_out == DeviceSignOutStatus::Failed) {
        status = SettingsText("解绑未完成，请重试", "Unbinding failed; retry");
    } else {
        switch (account.status) {
            case DeviceAccountStatus::Offline:
                status = SettingsText("Wi-Fi 未连接", "Wi-Fi offline");
                break;
            case DeviceAccountStatus::NeedsLogin:
                status = SettingsText("请扫码登录", "Scan QR to sign in");
                break;
            case DeviceAccountStatus::SignedOut:
                status = SettingsText("已停用，仍绑定原账号", "Stopped; still bound");
                break;
            case DeviceAccountStatus::Synchronizing:
                status = SettingsText("正在同步登录状态", "Syncing login");
                break;
            case DeviceAccountStatus::Loading:
                status = SettingsText("正在读取账号信息", "Loading account");
                break;
            case DeviceAccountStatus::Ready:
                status = SettingsText("已登录", "Signed in");
                break;
            case DeviceAccountStatus::Unavailable:
                break;
        }
    }
    SetLabelText(settings_account_state_, status);
    const bool ready = account.status == DeviceAccountStatus::Ready;
    const char* missing = ready ? SettingsText("暂不可用", "Unavailable") : "-";
    SetLabelText(settings_username_, account.username.empty() ? missing : account.username.c_str());
    SetLabelText(settings_balance_, account.balance.empty() ? missing : account.balance.c_str());
    SetHidden(settings_refresh_button_, !account.can_sign_out);
    SetButtonEnabled(settings_refresh_button_, confirm || account.can_refresh);
    SetLabelText(settings_refresh_label_,
                 confirm ? SettingsText("取消", "Cancel") : SettingsText("刷新", "Refresh"));
    SetHidden(settings_consent_button_,
              confirm || !account.can_sign_out || !ready || !account.balance.empty());
    SetHidden(settings_sign_out_button_, !account.can_sign_out && !pending && !account.can_scan);
    SetButtonEnabled(settings_sign_out_button_, !pending);
    SetLabelText(settings_sign_out_label_, pending   ? SettingsText("解绑中", "Unbinding")
                                           : confirm ? SettingsText("确认退出并解绑", "Confirm unbinding")
                                           : account.can_scan
                                               ? SettingsText("扫码登录", "Scan QR")
                                               : SettingsText("退出并解除绑定", "Sign out and unbind"));

    const bool can_change = WifiSettingsAvailable() && wifi.state != WifiLinkState::Unsupported;
    const bool off = wifi.state == WifiLinkState::Off;
    const bool provisioning = wifi.state == WifiLinkState::Provisioning;
    const char* wifi_status = SettingsText("Wi-Fi 不可用", "Wi-Fi unavailable");
    std::string wifi_detail = wifi.ssid;
    switch (wifi.state) {
        case WifiLinkState::Connected:
            wifi_status = SettingsText("已连接", "Connected");
            break;
        case WifiLinkState::Connecting:
            wifi_status = SettingsText("正在连接", "Connecting");
            break;
        case WifiLinkState::Off:
            wifi_status = SettingsText("已断开", "Disconnected");
            wifi_detail =
                SettingsText("连接时会使用已保存的网络。", "Reconnect to a saved network.");
            break;
        case WifiLinkState::Provisioning:
            wifi_status = SettingsText("等待配网", "Wi-Fi setup");
            wifi_detail =
                wifi.setup_url.empty()
                    ? SettingsText("请按音箱的配网提示操作。", "Follow the setup prompts.")
                    : SettingsText("连接热点 ", "Join ") + wifi.ssid +
                          SettingsText("\n打开 ", "\nOpen ") + wifi.setup_url;
            break;
        case WifiLinkState::Unsupported:
            break;
    }
    SetLabelText(settings_wifi_state_, wifi_status);
    SetLabelText(settings_wifi_detail_, wifi_detail.c_str());
    SetLabelText(settings_wifi_button_label_, off ? SettingsText("连接 Wi-Fi", "Connect Wi-Fi")
                                                  : SettingsText("断开 Wi-Fi", "Disconnect Wi-Fi"));
    SetButtonEnabled(settings_wifi_button_, can_change);
    SetButtonEnabled(settings_wifi_setup_button_, can_change && !provisioning);
}

void StandbyDashboard::ShowFactoryResetConfirmation() {
    if (!Application::GetInstance().CanRestoreFactorySettings() || settings_factory_reset_dialog_)
        return;
    sign_out_confirmation_ = 0;
    settings_factory_reset_dialog_ = CreateObject(settings_view_, 0, 0, 800, 480);
    lv_obj_add_flag(settings_factory_reset_dialog_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(settings_factory_reset_dialog_, lv_color_hex(kColorBackground), 0);
    lv_obj_set_style_bg_opa(settings_factory_reset_dialog_, LV_OPA_90, 0);
    lv_obj_set_style_border_width(settings_factory_reset_dialog_, 0, 0);
    lv_obj_set_style_pad_all(settings_factory_reset_dialog_, 0, 0);
    auto* panel = CreateObject(settings_factory_reset_dialog_, 140, 100, 520, 280);
    StylePanel(panel, kColorPanel);
    auto* title = CreateLabel(panel, text_font_, lv_color_hex(kColorAmber),
                              SettingsText("恢复出厂设置？", "Restore factory settings?"));
    lv_obj_set_pos(title, 24, 24);
    auto* detail =
        CreateLabel(panel, text_font_, lv_color_hex(kColorText),
                    SettingsText("将清除 Wi-Fi 和本机设置。\n设备身份、账号归属和停用状态保留。",
                                 "Clear Wi-Fi and local settings.\nKeep device identity, account "
                                 "ownership and usage state."));
    lv_obj_set_pos(detail, 24, 80);
    lv_obj_set_width(detail, 472);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    auto* cancel = CreateTechButton(panel, text_font_, SettingsText("取消", "Cancel"), 24, 204, 224,
                                    50, lv_color_hex(kColorBlue));
    lv_obj_add_event_cb(
        cancel,
        [](lv_event_t* e) {
            static_cast<StandbyDashboard*>(lv_event_get_user_data(e))
                ->CloseFactoryResetConfirmation();
        },
        LV_EVENT_CLICKED, this);
    auto* confirm =
        CreateTechButton(panel, text_font_, SettingsText("恢复并重启", "Reset and reboot"), 272,
                         204, 224, 50, lv_color_hex(kColorAmber));
    lv_obj_add_event_cb(
        confirm,
        [](lv_event_t* e) {
            lv_obj_add_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_DISABLED);
            auto* self = static_cast<StandbyDashboard*>(lv_event_get_user_data(e));
            self->CloseSettingsView();
            Application::GetInstance().Schedule(
                []() { Application::GetInstance().RestoreFactorySettings(); });
        },
        LV_EVENT_CLICKED, this);
    lv_obj_move_foreground(settings_factory_reset_dialog_);
}

void StandbyDashboard::CloseFactoryResetConfirmation() {
    if (settings_factory_reset_dialog_ && lv_obj_is_valid(settings_factory_reset_dialog_))
        lv_obj_delete(settings_factory_reset_dialog_);
    settings_factory_reset_dialog_ = nullptr;
}

void StandbyDashboard::HandleSettingsSignOut() {
    const auto account = DeviceControl::GetInstance().GetAccountView();
    if (account.can_scan) {
        CloseSettingsView();
        return;
    }
    if (!account.can_sign_out)
        return;
    if (sign_out_confirmation_ != account.permission_version) {
        sign_out_confirmation_ = account.permission_version;
        UpdateSettingsView();
        return;
    }
    const auto expected = sign_out_confirmation_;
    sign_out_confirmation_ = 0;
    lv_obj_add_state(settings_sign_out_button_, LV_STATE_DISABLED);
    Application::GetInstance().Schedule(
        [expected]() { DeviceControl::GetInstance().SignOut(expected); });
}

void StandbyDashboard::CreateNotificationChip() {
    notification_chip_ = CreateObject(root_, 238, 4, 324, 40);
    lv_obj_set_style_bg_color(notification_chip_, lv_color_hex(kColorPanelSecondary), 0);
    lv_obj_set_style_bg_opa(notification_chip_, LV_OPA_90, 0);
    lv_obj_set_style_border_color(notification_chip_, lv_color_hex(kColorCyan), 0);
    lv_obj_set_style_border_width(notification_chip_, 1, 0);
    lv_obj_set_style_radius(notification_chip_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(notification_chip_, 0, 0);

    notification_label_ = CreateLabel(notification_chip_, text_font_, lv_color_hex(kColorText), "");
    lv_obj_set_width(notification_label_, 298);
    lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(notification_label_);
    lv_obj_add_flag(notification_chip_, LV_OBJ_FLAG_HIDDEN);
}

void StandbyDashboard::SetVisible(bool visible) {
    if (visible_ == visible || root_ == nullptr) {
        return;
    }
    visible_ = visible;
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(root_, -1);
        if (pomodoro_visible_ || calendar_visible_ || music_visible_) {
            StopPulseAnimation();
        } else {
            StartPulseAnimation();
        }
    } else {
        StopPulseAnimation();
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StandbyDashboard::Update(bool force) {
    if (!visible_ && !force) {
        return;
    }
    if (music_visible_) {
        UpdateMusicView();
    } else if (calendar_visible_) {
        UpdateCalendarClock(time(nullptr));
    } else if (pomodoro_visible_) {
        UpdatePomodoro();
    } else {
        UpdateClock(time(nullptr));
        UpdateEnvironment(force);
    }
}

void StandbyDashboard::ShowNotification(const char* notification, int duration_ms) {
    if (notification_chip_ == nullptr || notification_label_ == nullptr ||
        notification_timer_ == nullptr) {
        return;
    }
    lv_label_set_text(notification_label_, notification != nullptr ? notification : "");
    lv_obj_remove_flag(notification_chip_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(notification_chip_, -1);
    lv_timer_set_period(notification_timer_, std::max(duration_ms, 100));
    lv_timer_set_repeat_count(notification_timer_, 1);
    lv_timer_reset(notification_timer_);
    lv_timer_resume(notification_timer_);
}

void StandbyDashboard::ShowPomodoroView() {
    if (pomodoro_view_ == nullptr) {
        return;
    }
    if (calendar_view_ != nullptr) {
        calendar_visible_ = false;
        lv_obj_add_flag(calendar_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (music_view_ != nullptr) {
        music_visible_ = false;
        lv_obj_add_flag(music_view_, LV_OBJ_FLAG_HIDDEN);
    }
    pomodoro_visible_ = true;
    SetMainEntryButtonsEnabled(false);
    StopPulseAnimation();
    lv_obj_remove_flag(pomodoro_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(pomodoro_view_, -1);
    if (notification_chip_ != nullptr) {
        lv_obj_move_to_index(notification_chip_, -1);
    }
    UpdatePomodoro();
}

void StandbyDashboard::ClosePomodoroView() {
    if (pomodoro_view_ == nullptr) {
        return;
    }
    pomodoro_visible_ = false;
    lv_obj_add_flag(pomodoro_view_, LV_OBJ_FLAG_HIDDEN);
    SetMainEntryButtonsEnabled(!calendar_visible_ && !music_visible_);
    if (visible_) {
        StartPulseAnimation();
        UpdateClock(time(nullptr));
        UpdateEnvironment(true);
    }
}

bool StandbyDashboard::IsPomodoroActive() const {
    PomodoroState state = PomodoroTimer::GetInstance().GetStatus().state;
    return pomodoro_visible_ && state != PomodoroState::kIdle;
}

void StandbyDashboard::ShowCalendarView() {
    if (calendar_view_ == nullptr) {
        return;
    }
    if (pomodoro_view_ != nullptr) {
        pomodoro_visible_ = false;
        lv_obj_add_flag(pomodoro_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (music_view_ != nullptr) {
        music_visible_ = false;
        lv_obj_add_flag(music_view_, LV_OBJ_FLAG_HIDDEN);
    }
    calendar_visible_ = true;
    SetMainEntryButtonsEnabled(false);
    StopPulseAnimation();
    ReturnCalendarToToday();
    lv_obj_remove_flag(calendar_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(calendar_view_, -1);
    if (notification_chip_ != nullptr) {
        lv_obj_move_to_index(notification_chip_, -1);
    }
}

void StandbyDashboard::CloseCalendarView() {
    if (calendar_view_ == nullptr) {
        return;
    }
    calendar_visible_ = false;
    lv_obj_add_flag(calendar_view_, LV_OBJ_FLAG_HIDDEN);
    SetMainEntryButtonsEnabled(!pomodoro_visible_ && !music_visible_);
    if (visible_) {
        StartPulseAnimation();
        UpdateClock(time(nullptr));
        UpdateEnvironment(true);
    }
}

void StandbyDashboard::ShowMusicView() {
    if (music_view_ == nullptr) {
        return;
    }
    if (pomodoro_view_ != nullptr) {
        pomodoro_visible_ = false;
        lv_obj_add_flag(pomodoro_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (calendar_view_ != nullptr) {
        calendar_visible_ = false;
        lv_obj_add_flag(calendar_view_, LV_OBJ_FLAG_HIDDEN);
    }
    music_visible_ = true;
    SetMainEntryButtonsEnabled(false);
    StopPulseAnimation();
    lv_obj_remove_flag(music_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(music_view_, -1);
    RefreshMusicPlaylist();
    if (notification_chip_ != nullptr) {
        lv_obj_move_to_index(notification_chip_, -1);
    }
}

void StandbyDashboard::CloseMusicView() {
    if (music_view_ == nullptr) {
        return;
    }
    music_visible_ = false;
    lv_obj_add_flag(music_view_, LV_OBJ_FLAG_HIDDEN);
    SetMainEntryButtonsEnabled(!pomodoro_visible_ && !calendar_visible_);
    if (visible_) {
        StartPulseAnimation();
        UpdateClock(time(nullptr));
        UpdateEnvironment(true);
    }
}

void StandbyDashboard::CloseFeatureViews() {
    CloseSettingsView();
    pomodoro_visible_ = false;
    calendar_visible_ = false;
    music_visible_ = false;
    SetMainEntryButtonsEnabled(true);
    if (pomodoro_view_ != nullptr) {
        lv_obj_add_flag(pomodoro_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (calendar_view_ != nullptr) {
        lv_obj_add_flag(calendar_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (music_view_ != nullptr) {
        lv_obj_add_flag(music_view_, LV_OBJ_FLAG_HIDDEN);
    }
    if (visible_) {
        StartPulseAnimation();
        UpdateClock(time(nullptr));
        UpdateEnvironment(true);
    }
}

void StandbyDashboard::SetDigit(int index, int value) {
    static constexpr uint8_t digit_masks[] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    };
    uint8_t mask = value >= 0 && value <= 9 ? digit_masks[value] : 0;
    for (int segment = 0; segment < kSegmentCount; ++segment) {
        bool active = (mask & (1U << segment)) != 0;
        lv_obj_set_style_bg_color(digit_segments_[index][segment],
                                  lv_color_hex(active ? kColorCyan : kColorSegmentOff), 0);
        lv_obj_set_style_bg_opa(digit_segments_[index][segment], active ? LV_OPA_COVER : LV_OPA_40,
                                0);
    }
}

void StandbyDashboard::SetPomodoroDigit(int index, int value, lv_color_t color) {
    static constexpr uint8_t digit_masks[] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    };
    uint8_t mask = value >= 0 && value <= 9 ? digit_masks[value] : 0;
    for (int segment = 0; segment < kSegmentCount; ++segment) {
        bool active = (mask & (1U << segment)) != 0;
        lv_obj_set_style_bg_color(pomodoro_digit_segments_[index][segment],
                                  active ? color : lv_color_hex(kColorSegmentOff), 0);
        lv_obj_set_style_bg_opa(pomodoro_digit_segments_[index][segment],
                                active ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

void StandbyDashboard::UpdateClock(time_t now) {
    struct tm timeinfo = {};
    localtime_r(&now, &timeinfo);
    bool time_valid = timeinfo.tm_year >= 2025 - 1900;
    if (!time_valid) {
        for (int i = 0; i < kDigitCount; ++i) {
            SetDigit(i, -1);
        }
        lv_label_set_text(seconds_label_, "--");
        lv_label_set_text(date_label_, "等待网络校时");
        return;
    }

    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    SetDigit(0, hour / 10);
    SetDigit(1, hour % 10);
    SetDigit(2, minute / 10);
    SetDigit(3, minute % 10);

    char seconds[4];
    snprintf(seconds, sizeof(seconds), "%02d", timeinfo.tm_sec);
    lv_label_set_text(seconds_label_, seconds);

    char date[64];
    snprintf(date, sizeof(date), "%02d月%02d日  %s", timeinfo.tm_mon + 1, timeinfo.tm_mday,
             WeekdayText(timeinfo.tm_wday));
    lv_label_set_text(date_label_, date);
}

void StandbyDashboard::UpdateEnvironment(bool force) {
    int64_t now_us = esp_timer_get_time();
    if (!force && last_environment_update_us_ != 0 &&
        now_us - last_environment_update_us_ < kEnvironmentUpdateIntervalUs) {
        return;
    }
    last_environment_update_us_ = now_us;

    auto& environment = EnvironmentService::GetInstance();
    environment.RefreshAsync(false);
    EnvironmentSnapshot snapshot = environment.GetSnapshot();

    if (!snapshot.city.empty()) {
        lv_label_set_text(city_label_, snapshot.city.c_str());
    } else if (snapshot.refresh_in_progress) {
        lv_label_set_text(city_label_, "正在定位");
    } else {
        lv_label_set_text(city_label_, "本地天气");
    }

    const char* primary_icon = FONT_AWESOME_CLOUD;
    const char* accent_icon = "";
    lv_color_t primary_color = lv_color_hex(kColorText);
    lv_color_t accent_color = lv_color_hex(kColorSun);
    SelectWeatherIcons(snapshot.weather_text, primary_icon, accent_icon, primary_color,
                       accent_color);
    lv_label_set_text(weather_icon_primary_, primary_icon);
    lv_label_set_text(weather_icon_accent_, accent_icon);
    lv_obj_set_style_text_color(weather_icon_primary_, primary_color, 0);
    lv_obj_set_style_text_color(weather_icon_accent_, accent_color, 0);

    char buffer[96];
    if (snapshot.weather_available) {
        snprintf(buffer, sizeof(buffer), "%s°C",
                 snapshot.temperature_c.empty() ? "--" : snapshot.temperature_c.c_str());
        lv_label_set_text(temperature_label_, buffer);
        lv_label_set_text(weather_label_, snapshot.weather_text.c_str());
    } else {
        lv_label_set_text(temperature_label_, "--°C");
        lv_label_set_text(weather_label_,
                          snapshot.configured
                              ? (snapshot.refresh_in_progress ? "正在更新" : "天气不可用")
                              : "待配置天气");
    }

    int feels_like = 0;
    if (CalculateFeelsLikeC(snapshot.temperature_c, snapshot.humidity, feels_like)) {
        snprintf(buffer, sizeof(buffer), "%d°", feels_like);
        lv_label_set_text(feels_like_label_, buffer);
    } else {
        lv_label_set_text(feels_like_label_, "--°");
    }

    snprintf(buffer, sizeof(buffer), "%s%%",
             snapshot.humidity.empty() ? "--" : snapshot.humidity.c_str());
    lv_label_set_text(humidity_label_, buffer);

    std::string wind_title = WindTitle(snapshot.wind_direction);
    std::string wind_power = WindPower(snapshot.wind_power);
    lv_label_set_text(wind_title_label_, wind_title.c_str());
    lv_label_set_text(wind_power_label_, wind_power.c_str());

    std::string air_category = AirCategory(snapshot.aqi, snapshot.air_category);
    lv_label_set_text(air_category_label_, air_category.c_str());
    lv_obj_set_style_text_color(air_category_label_, AirCategoryColor(snapshot.aqi), 0);
    if (snapshot.aqi >= 0) {
        snprintf(buffer, sizeof(buffer), "%d", snapshot.aqi);
        lv_label_set_text(air_value_label_, buffer);
    } else {
        lv_label_set_text(air_value_label_, "--");
    }

    UpdateDeviceStatus();
}

void StandbyDashboard::UpdateDeviceStatus() {
    auto& board = Board::GetInstance();
    const char* network_icon = board.GetNetworkStateIcon();
    lv_label_set_text(network_label_,
                      network_icon != nullptr ? network_icon : FONT_AWESOME_WIFI_SLASH);

    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        battery_level = std::clamp(battery_level, 0, 100);
        lv_label_set_text(battery_icon_label_, BatteryIconForLevel(battery_level, charging));
        char percent[8];
        snprintf(percent, sizeof(percent), "%d%%", battery_level);
        lv_label_set_text(battery_percent_label_, percent);
        lv_obj_remove_flag(battery_group_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(battery_group_, LV_OBJ_FLAG_HIDDEN);
    }
    (void)discharging;
}

void StandbyDashboard::UpdatePomodoro() {
    if (pomodoro_view_ == nullptr || pomodoro_progress_arc_ == nullptr) {
        return;
    }

    PomodoroStatus status = PomodoroTimer::GetInstance().GetStatus();
    int remaining_seconds =
        status.state == PomodoroState::kIdle ? 30 * 60 : std::max(0, status.remaining_seconds);
    int hours = std::min(99, remaining_seconds / 3600);
    int minutes = (remaining_seconds / 60) % 60;
    int seconds = remaining_seconds % 60;
    const int digits[kPomodoroDigitCount] = {
        hours / 10, hours % 10, minutes / 10, minutes % 10, seconds / 10, seconds % 10,
    };

    const char* state_text = "选择专注时长";
    const char* pause_text = "待启动";
    lv_color_t state_color = lv_color_hex(kColorCyan);
    bool pause_enabled = false;
    char detail[128];
    switch (status.state) {
        case PomodoroState::kRunning:
            state_text = "专注进行中";
            pause_text = "暂停";
            state_color = lv_color_hex(kColorFocus);
            pause_enabled = true;
            snprintf(detail, sizeof(detail), "本轮 %d 分钟 · 已专注 %02d:%02d",
                     status.duration_minutes, status.elapsed_seconds / 60,
                     status.elapsed_seconds % 60);
            break;
        case PomodoroState::kPaused:
            state_text = "计时已暂停";
            pause_text = "继续";
            state_color = lv_color_hex(kColorAmber);
            pause_enabled = true;
            snprintf(detail, sizeof(detail), "剩余时间已保留 · 点击继续恢复");
            break;
        case PomodoroState::kFinished:
            state_text = "本轮已完成";
            pause_text = "已完成";
            state_color = lv_color_hex(kColorGreen);
            snprintf(detail, sizeof(detail), "专注完成 · 选择快捷时长开始下一轮");
            break;
        case PomodoroState::kIdle:
        default:
            snprintf(detail, sizeof(detail), "选择快捷时长，或通过语音自定义");
            break;
    }

    for (int i = 0; i < kPomodoroDigitCount; ++i) {
        SetPomodoroDigit(i, digits[i], state_color);
    }
    for (lv_obj_t* dot : pomodoro_colon_dots_) {
        lv_obj_set_style_bg_color(dot, state_color, 0);
    }

    lv_label_set_text(pomodoro_state_label_, state_text);
    lv_obj_set_style_text_color(pomodoro_state_label_, state_color, 0);
    lv_label_set_text(pomodoro_detail_label_, detail);
    lv_label_set_text(pomodoro_pause_label_, pause_text);

    int total_seconds = status.duration_minutes * 60;
    int progress = total_seconds > 0
                       ? std::clamp(status.remaining_seconds * 1000 / total_seconds, 0, 1000)
                       : 0;
    lv_arc_set_value(pomodoro_progress_arc_, progress);
    lv_obj_set_style_arc_color(pomodoro_progress_arc_, state_color, LV_PART_INDICATOR);

    if (pause_enabled) {
        lv_obj_add_flag(pomodoro_pause_button_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(pomodoro_pause_button_, state_color, 0);
        lv_obj_set_style_text_color(pomodoro_pause_label_, lv_color_hex(kColorText), 0);
        lv_obj_set_style_bg_opa(pomodoro_pause_button_, LV_OPA_80, 0);
    } else {
        lv_obj_remove_flag(pomodoro_pause_button_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(pomodoro_pause_button_, lv_color_hex(kColorSegmentOff), 0);
        lv_obj_set_style_text_color(pomodoro_pause_label_, lv_color_hex(kColorMuted), 0);
        lv_obj_set_style_bg_opa(pomodoro_pause_button_, LV_OPA_40, 0);
    }
}

void StandbyDashboard::UpdateCalendarClock(time_t now) {
    if (calendar_clock_label_ == nullptr) {
        return;
    }
    struct tm timeinfo = {};
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < 2020 - 1900) {
        lv_label_set_text(calendar_clock_label_, "--:--");
        return;
    }

    char time_text[8];
    snprintf(time_text, sizeof(time_text), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    lv_label_set_text(calendar_clock_label_, time_text);

    if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0 && timeinfo.tm_sec == 0) {
        UpdateCalendarView();
    }
}

void StandbyDashboard::UpdateCalendarView() {
    if (calendar_view_ == nullptr || calendar_month_label_ == nullptr) {
        return;
    }

    static constexpr const char* kMonthNames[] = {
        "一月", "二月", "三月", "四月", "五月",   "六月",
        "七月", "八月", "九月", "十月", "十一月", "十二月",
    };
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%04d / %02d  %s", calendar_year_, calendar_month_,
             kMonthNames[calendar_month_ - 1]);
    lv_label_set_text(calendar_month_label_, buffer);

    time_t now = time(nullptr);
    struct tm today = {};
    localtime_r(&now, &today);
    bool today_valid = today.tm_year >= 2020 - 1900;
    int today_year = today.tm_year + 1900;
    int today_month = today.tm_mon + 1;
    int today_day = today.tm_mday;
    bool showing_current_month =
        today_valid && calendar_year_ == today_year && calendar_month_ == today_month;
    if (calendar_today_label_ != nullptr) {
        if (showing_current_month) {
            snprintf(buffer, sizeof(buffer), "今日 %02d", today_day);
            lv_label_set_text(calendar_today_label_, buffer);
        } else {
            lv_label_set_text(calendar_today_label_, "返回今日");
        }
    }

    int day_count = DaysInMonth(calendar_year_, calendar_month_);
    calendar_selected_day_ = std::clamp(calendar_selected_day_, 1, day_count);
    int first_weekday = FirstWeekday(calendar_year_, calendar_month_);
    for (int index = 0; index < kCalendarCellCount; ++index) {
        lv_obj_t* button = calendar_day_buttons_[index];
        lv_obj_t* label = calendar_day_labels_[index];
        int day = index - first_weekday + 1;
        if (day < 1 || day > day_count) {
            lv_obj_set_user_data(button, nullptr);
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(day)));
        snprintf(buffer, sizeof(buffer), "%02d", day);
        lv_label_set_text(label, buffer);

        int weekday = (first_weekday + day - 1) % 7;
        lv_color_t text_color =
            lv_color_hex(weekday == 0 ? kColorFocus : (weekday == 6 ? kColorBlue : kColorText));
        bool is_today = showing_current_month && day == today_day;
        bool is_selected = day == calendar_selected_day_;
        if (is_today) {
            text_color = lv_color_hex(kColorGreen);
        }

        lv_obj_set_style_text_color(label, text_color, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_outline_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_shadow_opa(button, LV_OPA_TRANSP, 0);
        if (is_today) {
            lv_obj_set_style_border_color(button, lv_color_hex(kColorGreen), 0);
            lv_obj_set_style_border_opa(button, LV_OPA_70, 0);
            lv_obj_set_style_border_width(button, 1, 0);
        }
        if (is_selected) {
            lv_obj_set_style_text_color(label, lv_color_hex(kColorText), 0);
            lv_obj_set_style_bg_color(button, lv_color_hex(kColorCyan), 0);
            lv_obj_set_style_bg_opa(button, LV_OPA_20, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(kColorCyan), 0);
            lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_outline_color(button, lv_color_hex(kColorCyan), 0);
            lv_obj_set_style_outline_opa(button, LV_OPA_40, 0);
            lv_obj_set_style_outline_width(button, 1, 0);
            lv_obj_set_style_outline_pad(button, 2, 0);
            lv_obj_set_style_shadow_color(button, lv_color_hex(kColorCyan), 0);
            lv_obj_set_style_shadow_width(button, 12, 0);
            lv_obj_set_style_shadow_opa(button, LV_OPA_30, 0);
        }
    }

    int selected_weekday = (first_weekday + calendar_selected_day_ - 1) % 7;
    snprintf(buffer, sizeof(buffer), "%04d.%02d.%02d", calendar_year_, calendar_month_,
             calendar_selected_day_);
    lv_label_set_text(calendar_selected_date_label_, buffer);
    lv_label_set_text(calendar_selected_weekday_label_, WeekdayText(selected_weekday));
    std::string lunar_text = LunarDateText(calendar_year_, calendar_month_, calendar_selected_day_);
    lv_label_set_text(calendar_lunar_label_, lunar_text.c_str());

    // These are deterministic local lifestyle suggestions, not official almanac data.
    static constexpr const char* kGoodItems[] = {
        "规划", "学习", "整理", "运动", "阅读", "沟通",
        "复盘", "早睡", "散步", "创作", "收纳", "专注",
    };
    static constexpr const char* kAvoidItems[] = {
        "熬夜", "拖延", "久坐", "分心", "急躁", "透支", "争执", "贪食",
    };
    uint32_t seed = static_cast<uint32_t>(calendar_year_ * 10000 + calendar_month_ * 100 +
                                          calendar_selected_day_);
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    int good_start = seed % 12;
    snprintf(buffer, sizeof(buffer), "%s  %s\n%s  %s", kGoodItems[good_start],
             kGoodItems[(good_start + 5) % 12], kGoodItems[(good_start + 7) % 12],
             kGoodItems[(good_start + 11) % 12]);
    lv_label_set_text(calendar_good_label_, buffer);

    int avoid_start = (seed >> 8) % 8;
    snprintf(buffer, sizeof(buffer), "%s  %s\n%s  %s", kAvoidItems[avoid_start],
             kAvoidItems[(avoid_start + 3) % 8], kAvoidItems[(avoid_start + 5) % 8],
             kAvoidItems[(avoid_start + 7) % 8]);
    lv_label_set_text(calendar_avoid_label_, buffer);
}

void StandbyDashboard::ChangeCalendarMonth(int delta) {
    int new_year = calendar_year_;
    int new_month = calendar_month_ + delta;
    while (new_month < 1) {
        new_month += 12;
        --new_year;
    }
    while (new_month > 12) {
        new_month -= 12;
        ++new_year;
    }
    if (new_year < 1900 || new_year > 2099) {
        return;
    }
    calendar_year_ = new_year;
    calendar_month_ = new_month;
    calendar_selected_day_ =
        std::min(calendar_selected_day_, DaysInMonth(calendar_year_, calendar_month_));
    UpdateCalendarView();
}

void StandbyDashboard::SelectCalendarDate(int day) {
    if (day < 1 || day > DaysInMonth(calendar_year_, calendar_month_)) {
        return;
    }
    calendar_selected_day_ = day;
    UpdateCalendarView();
}

void StandbyDashboard::ReturnCalendarToToday() {
    time_t now = time(nullptr);
    struct tm today = {};
    localtime_r(&now, &today);
    int year = today.tm_year + 1900;
    if (year >= 1900 && year <= 2099) {
        calendar_year_ = year;
        calendar_month_ = today.tm_mon + 1;
        calendar_selected_day_ = today.tm_mday;
    }
    UpdateCalendarClock(now);
    UpdateCalendarView();
}

void StandbyDashboard::RefreshMusicPlaylist() {
    if (music_playlist_container_ == nullptr || music_playlist_count_label_ == nullptr) {
        return;
    }

    if (music_refresh_in_progress_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    music_refresh_ready_.store(false, std::memory_order_release);
    lv_label_set_text(music_playlist_count_label_, "曲库读取中");
    if (music_playlist_.empty()) {
        lv_label_set_text(music_empty_label_, "正在读取本地曲库");
        lv_obj_remove_flag(music_empty_label_, LV_OBJ_FLAG_HIDDEN);
    }

    BaseType_t result =
        xTaskCreate(MusicRefreshTask, "music_scan", kMusicRefreshTaskStackBytes, this, 3, nullptr);
    if (result != pdPASS) {
        music_refresh_in_progress_.store(false, std::memory_order_release);
        lv_label_set_text(music_playlist_count_label_, "曲库读取失败");
        if (music_playlist_.empty()) {
            lv_label_set_text(music_empty_label_, "无法启动曲库读取任务");
        }
    }
}

void StandbyDashboard::MusicRefreshTask(void* arg) {
    auto* dashboard = static_cast<StandbyDashboard*>(arg);
    if (dashboard == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    auto playlist = MusicPlayer::GetInstance().GetPlaylist();
    bool mounted = SdCardService::GetInstance().IsMounted();

    if (!dashboard->music_refresh_shutdown_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(dashboard->music_refresh_mutex_);
        dashboard->pending_music_playlist_ = std::move(playlist);
        dashboard->pending_music_sd_mounted_ = mounted;
        dashboard->music_refresh_ready_.store(true, std::memory_order_release);
    }
    dashboard->music_refresh_in_progress_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

void StandbyDashboard::ApplyPendingMusicPlaylist() {
    if (!music_refresh_ready_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    std::vector<MediaFile> playlist;
    bool mounted = false;
    {
        std::lock_guard<std::mutex> lock(music_refresh_mutex_);
        playlist = std::move(pending_music_playlist_);
        mounted = pending_music_sd_mounted_;
    }
    RenderMusicPlaylist(std::move(playlist), mounted);
}

void StandbyDashboard::RenderMusicPlaylist(std::vector<MediaFile> playlist, bool sd_mounted) {
    lv_obj_clean(music_playlist_container_);
    music_playlist_rows_.clear();
    music_playlist_name_labels_.clear();
    music_playlist_ = std::move(playlist);
    music_last_index_ = -2;
    music_last_state_ = -1;

    char count_text[32];
    snprintf(count_text, sizeof(count_text), "曲库 %u",
             static_cast<unsigned>(music_playlist_.size()));
    lv_label_set_text(music_playlist_count_label_, count_text);

    if (music_playlist_.empty()) {
        if (sd_mounted) {
            lv_label_set_text(music_empty_label_, "未找到 MP3 / WAV\n请将音乐放入 /sdcard/music");
        } else {
            lv_label_set_text(music_empty_label_, "未检测到 microSD 卡\n插入后点击刷新按钮");
        }
        UpdateMusicView();
        return;
    }
    lv_obj_add_flag(music_empty_label_, LV_OBJ_FLAG_HIDDEN);

    const size_t visible_count = std::min(music_playlist_.size(), kMaxMusicPlaylistRows);
    for (size_t index = 0; index < visible_count; ++index) {
        const auto& track = music_playlist_[index];
        lv_obj_t* row = lv_button_create(music_playlist_container_);
        lv_obj_set_size(row, 326, 54);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(kColorPanelSecondary), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_60, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(kColorGrid), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_80, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(kColorCyan), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        lv_obj_add_event_cb(row, MusicTrackEventCallback, LV_EVENT_CLICKED, this);

        char index_text[16];
        snprintf(index_text, sizeof(index_text), "%02u", static_cast<unsigned>(index + 1));
        lv_obj_t* number = CreateLabel(row, text_font_, lv_color_hex(kColorMuted), index_text);
        lv_obj_set_pos(number, 10, 9);
        lv_obj_set_width(number, 34);
        lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_transform_scale(number, 150, 0);

        lv_obj_t* name =
            CreateLabel(row, text_font_, lv_color_hex(kColorText), track.display_name.c_str());
        lv_obj_set_pos(name, 48, 3);
        lv_obj_set_width(name, 218);
        lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_anim_duration(name, 6000, 0);
        lv_obj_set_style_transform_scale(name, 170, 0);

        std::string type = track.type;
        std::transform(type.begin(), type.end(), type.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        lv_obj_t* type_label = CreateLabel(row, text_font_, lv_color_hex(kColorCyan), type.c_str());
        lv_obj_set_pos(type_label, 270, 10);
        lv_obj_set_width(type_label, 46);
        lv_obj_set_style_text_align(type_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_transform_scale(type_label, 135, 0);

        music_playlist_rows_.push_back(row);
        music_playlist_name_labels_.push_back(name);
    }
    UpdateMusicView();
}

void StandbyDashboard::UpdateMusicView() {
    if (music_view_ == nullptr || music_state_label_ == nullptr || music_progress_bar_ == nullptr) {
        return;
    }

    ApplyPendingMusicPlaylist();

    MusicPlayerStatus status = MusicPlayer::GetInstance().GetStatus();
    const char* state_text = "待播放";
    lv_color_t state_color = lv_color_hex(kColorCyan);
    switch (status.state) {
        case MusicPlayerState::kPlaying:
            state_text = "正在播放";
            state_color = lv_color_hex(kColorGreen);
            break;
        case MusicPlayerState::kPaused:
            state_text = "已暂停";
            state_color = lv_color_hex(kColorAmber);
            break;
        case MusicPlayerState::kError:
            state_text = "播放错误";
            state_color = lv_color_hex(kColorFocus);
            break;
        case MusicPlayerState::kIdle:
        default:
            break;
    }
    lv_label_set_text(music_state_label_, state_text);
    lv_obj_set_style_text_color(music_state_label_, state_color, 0);

    if (!status.track_name.empty()) {
        lv_label_set_text(music_track_name_label_, status.track_name.c_str());
    } else {
        lv_label_set_text(music_track_name_label_, "选择一首音乐");
    }

    char meta[96];
    if (status.state == MusicPlayerState::kError) {
        snprintf(meta, sizeof(meta), "%s",
                 SdCardService::GetInstance().IsMounted() ? "请检查文件格式或重新选择"
                                                          : "microSD 卡不可用");
    } else if (!status.track_name.empty()) {
        const std::string sample_rate_text =
            status.sample_rate > 0 ? std::to_string(status.sample_rate) + "Hz" : "读取中";
        snprintf(meta, sizeof(meta), "%s · %s · %s",
                 status.track_type.empty() ? "AUDIO" : status.track_type.c_str(),
                 sample_rate_text.c_str(),
                 status.channels > 1 ? "双声道" : (status.channels == 1 ? "单声道" : "分析中"));
    } else {
        snprintf(meta, sizeof(meta), "SD 本地曲库 · %u 首",
                 static_cast<unsigned>(music_playlist_.size()));
    }
    lv_label_set_text(music_track_meta_label_, meta);

    int progress = std::clamp(status.progress_per_mille, 0, 1000);
    lv_bar_set_value(music_progress_bar_, progress, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(music_progress_bar_, state_color, LV_PART_INDICATOR);

    char elapsed[16];
    FormatDuration(status.elapsed_seconds, elapsed, sizeof(elapsed));
    lv_label_set_text(music_elapsed_label_, elapsed);

    bool playing = status.state == MusicPlayerState::kPlaying;
    lv_label_set_text(music_play_pause_label_, playing ? FONT_AWESOME_PAUSE : FONT_AWESOME_PLAY);
    lv_obj_set_style_border_color(music_play_pause_button_, state_color, 0);
    lv_obj_set_style_shadow_color(music_play_pause_button_, state_color, 0);
    lv_obj_set_style_shadow_width(music_play_pause_button_, playing ? 14 : 0, 0);
    lv_obj_set_style_shadow_opa(music_play_pause_button_, playing ? LV_OPA_30 : LV_OPA_TRANSP, 0);

    for (int i = 0; i < kMusicSpectrumBarCount; ++i) {
        int height = 6;
        lv_color_t bar_color = lv_color_hex(kColorSegmentOff);
        if (status.state == MusicPlayerState::kPlaying) {
            height = 10 + ((status.elapsed_seconds * 3 + i * 7 + i * i) % 6) * 7;
            bar_color = lv_color_hex(i == 4 ? kColorViolet : kColorCyan);
        } else if (status.state == MusicPlayerState::kPaused) {
            height = 12 + (i % 3) * 4;
            bar_color = lv_color_hex(kColorAmber);
        }
        lv_obj_set_pos(music_spectrum_bars_[i], 5 + i * 23, 50 - height);
        lv_obj_set_height(music_spectrum_bars_[i], height);
        lv_obj_set_style_bg_color(music_spectrum_bars_[i], bar_color, 0);
        lv_obj_set_style_bg_opa(music_spectrum_bars_[i],
                                status.state == MusicPlayerState::kIdle ? LV_OPA_50 : LV_OPA_COVER,
                                0);
    }

    int current_index = status.current_index;
    int current_state = static_cast<int>(status.state);
    if (current_index != music_last_index_ || current_state != music_last_state_) {
        if (music_last_index_ >= 0 &&
            music_last_index_ < static_cast<int>(music_playlist_rows_.size())) {
            StyleMusicPlaylistRow(music_last_index_, false, false);
        }
        if (current_index >= 0 && current_index < static_cast<int>(music_playlist_rows_.size())) {
            StyleMusicPlaylistRow(current_index, true, playing);
            if (current_index != music_last_index_) {
                lv_obj_scroll_to_view(music_playlist_rows_[current_index], LV_ANIM_ON);
            }
        }
        music_last_index_ = current_index;
        music_last_state_ = current_state;
    }
}

void StandbyDashboard::StyleMusicPlaylistRow(int index, bool selected, bool playing) {
    if (index < 0 || index >= static_cast<int>(music_playlist_rows_.size())) {
        return;
    }
    lv_obj_t* row = music_playlist_rows_[index];
    lv_obj_t* name = music_playlist_name_labels_[index];
    lv_label_set_long_mode(name, selected ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
    if (selected) {
        lv_color_t accent = lv_color_hex(playing ? kColorGreen : kColorCyan);
        lv_obj_set_style_bg_color(row, accent, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, 0);
        lv_obj_set_style_border_color(row, accent, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_shadow_color(row, accent, 0);
        lv_obj_set_style_shadow_width(row, 8, 0);
        lv_obj_set_style_shadow_opa(row, LV_OPA_20, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(kColorText), 0);
    } else {
        lv_obj_set_style_bg_color(row, lv_color_hex(kColorPanelSecondary), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_60, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(kColorGrid), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_80, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(kColorText), 0);
    }
}

void StandbyDashboard::HandleMusicTrackSelect(int index) {
    if (index < 0 || index >= static_cast<int>(music_playlist_.size())) {
        return;
    }
    if (MusicPlayer::GetInstance().Play(index) != ESP_OK) {
        ShowNotification("无法播放所选音乐", 3000);
    }
    UpdateMusicView();
}

void StandbyDashboard::HandleMusicPrevious() {
    if (music_refresh_in_progress_.load(std::memory_order_acquire)) {
        ShowNotification("正在读取本地曲库", 2000);
        return;
    }
    if (MusicPlayer::GetInstance().Previous() != ESP_OK) {
        ShowNotification("没有可播放的上一首", 3000);
    }
    UpdateMusicView();
}

void StandbyDashboard::HandleMusicPlayPause() {
    if (music_refresh_in_progress_.load(std::memory_order_acquire)) {
        ShowNotification("正在读取本地曲库", 2000);
        return;
    }
    if (music_playlist_.empty()) {
        RefreshMusicPlaylist();
        ShowNotification("正在读取本地曲库", 2000);
        return;
    }
    if (MusicPlayer::GetInstance().TogglePlayPause() != ESP_OK) {
        ShowNotification("音乐播放操作失败", 3000);
    }
    UpdateMusicView();
}

void StandbyDashboard::HandleMusicNext() {
    if (music_refresh_in_progress_.load(std::memory_order_acquire)) {
        ShowNotification("正在读取本地曲库", 2000);
        return;
    }
    if (MusicPlayer::GetInstance().Next() != ESP_OK) {
        ShowNotification("没有可播放的下一首", 3000);
    }
    UpdateMusicView();
}

void StandbyDashboard::HandlePomodoroPreset(int minutes) {
    if (PomodoroTimer::GetInstance().Start(minutes) == ESP_OK) {
        ShowPomodoroView();
    }
}

void StandbyDashboard::HandlePomodoroPauseResume() {
    auto& timer = PomodoroTimer::GetInstance();
    PomodoroStatus status = timer.GetStatus();
    if (status.state == PomodoroState::kRunning) {
        timer.Pause();
    } else if (status.state == PomodoroState::kPaused) {
        timer.Resume();
    }
    UpdatePomodoro();
}

void StandbyDashboard::HandlePomodoroClose() {
    PomodoroTimer::GetInstance().Cancel();
    ClosePomodoroView();
}

void StandbyDashboard::StartPulseAnimation() {
    if (pulse_ring_ == nullptr) {
        return;
    }
    lv_anim_delete(pulse_ring_, nullptr);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, pulse_ring_);
    lv_anim_set_values(&animation, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_duration(&animation, 1400);
    lv_anim_set_reverse_duration(&animation, 1400);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&animation, [](void* object, int32_t value) {
        lv_obj_set_style_border_opa(static_cast<lv_obj_t*>(object), static_cast<lv_opa_t>(value),
                                    0);
    });
    lv_anim_start(&animation);
}

void StandbyDashboard::StopPulseAnimation() {
    if (pulse_ring_ != nullptr) {
        lv_anim_delete(pulse_ring_, nullptr);
        lv_obj_set_style_border_opa(pulse_ring_, LV_OPA_70, 0);
    }
}

const char* StandbyDashboard::WeekdayText(int weekday) {
    static const char* weekdays[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六",
    };
    return weekday >= 0 && weekday < 7 ? weekdays[weekday] : "";
}

void StandbyDashboard::FormatDuration(int seconds, char* output, size_t output_size) {
    if (output == nullptr || output_size == 0) {
        return;
    }
    seconds = std::max(0, seconds);
    int hours = seconds / 3600;
    int minutes = (seconds / 60) % 60;
    int remaining_seconds = seconds % 60;
    if (hours > 0) {
        snprintf(output, output_size, "%d:%02d:%02d", hours, minutes, remaining_seconds);
    } else {
        snprintf(output, output_size, "%02d:%02d", minutes, remaining_seconds);
    }
}

int StandbyDashboard::DaysInMonth(int year, int month) {
    static constexpr int kMonthDays[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && IsGregorianLeapYear(year)) {
        return 29;
    }
    return kMonthDays[month - 1];
}

int StandbyDashboard::FirstWeekday(int year, int month) {
    int64_t day_number = GregorianDayNumber(year, month, 1);
    return static_cast<int>((day_number + 1) % 7);
}

std::string StandbyDashboard::LunarDateText(int year, int month, int day) {
    constexpr int kFirstLunarYear = 1900;
    constexpr int kLastLunarYear =
        kFirstLunarYear + static_cast<int>(sizeof(kLunarYearInfo) / sizeof(kLunarYearInfo[0])) - 1;
    if (year < kFirstLunarYear || year > kLastLunarYear || month < 1 || month > 12 || day < 1 ||
        day > DaysInMonth(year, month)) {
        return "农历信息不可用";
    }

    int64_t offset = GregorianDayNumber(year, month, day) - GregorianDayNumber(1900, 1, 31);
    if (offset < 0) {
        return "农历信息不可用";
    }

    int lunar_year = kFirstLunarYear;
    while (lunar_year <= kLastLunarYear) {
        int year_days = LunarYearDays(lunar_year);
        if (offset < year_days) {
            break;
        }
        offset -= year_days;
        ++lunar_year;
    }
    if (lunar_year > kLastLunarYear) {
        return "农历信息不可用";
    }

    int leap_month = LunarLeapMonth(lunar_year);
    int lunar_month = 1;
    bool is_leap_month = false;
    while (lunar_month <= 12) {
        int month_days =
            is_leap_month ? LunarLeapDays(lunar_year) : LunarMonthDays(lunar_year, lunar_month);
        if (offset < month_days) {
            break;
        }
        offset -= month_days;
        if (leap_month == lunar_month && !is_leap_month) {
            is_leap_month = true;
        } else {
            is_leap_month = false;
            ++lunar_month;
        }
    }

    static constexpr const char* kLunarMonthNames[] = {
        "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊",
    };
    static constexpr const char* kLunarDayNames[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
    };
    int lunar_day = static_cast<int>(offset) + 1;
    char text[48];
    snprintf(text, sizeof(text), "农历%s%s月%s", is_leap_month ? "闰" : "",
             kLunarMonthNames[lunar_month - 1], kLunarDayNames[lunar_day - 1]);
    return text;
}

void StandbyDashboard::SelectWeatherIcons(const std::string& weather, const char*& primary,
                                          const char*& accent, lv_color_t& primary_color,
                                          lv_color_t& accent_color) {
    primary = FONT_AWESOME_CLOUD;
    accent = "";
    primary_color = lv_color_hex(kColorText);
    accent_color = lv_color_hex(kColorSun);

    if (weather.find("雷") != std::string::npos) {
        primary = FONT_AWESOME_CLOUD_BOLT;
        primary_color = lv_color_hex(kColorViolet);
    } else if (weather.find("雪") != std::string::npos) {
        primary = FONT_AWESOME_SNOWFLAKE;
        primary_color = lv_color_hex(kColorCyan);
    } else if (weather.find("雨") != std::string::npos) {
        primary = FONT_AWESOME_CLOUD_RAIN;
        primary_color = lv_color_hex(kColorCyan);
    } else if (weather.find("雾") != std::string::npos || weather.find("霾") != std::string::npos) {
        primary = FONT_AWESOME_SMOG;
        primary_color = lv_color_hex(kColorMuted);
    } else if (weather.find("晴") != std::string::npos && weather.find("云") == std::string::npos) {
        primary = FONT_AWESOME_SUN;
        primary_color = lv_color_hex(kColorSun);
    } else if (weather.find("云") != std::string::npos) {
        primary = FONT_AWESOME_CLOUD;
        accent = FONT_AWESOME_SUN;
    } else if (weather.find("阴") != std::string::npos) {
        primary = FONT_AWESOME_CLOUDS;
    }
}

bool StandbyDashboard::CalculateFeelsLikeC(const std::string& temperature,
                                           const std::string& humidity, int& feels_like) {
    if (temperature.empty() || humidity.empty()) {
        return false;
    }
    char* end = nullptr;
    double temperature_c = std::strtod(temperature.c_str(), &end);
    if (end == temperature.c_str()) {
        return false;
    }
    double relative_humidity = std::strtod(humidity.c_str(), &end);
    if (end == humidity.c_str()) {
        return false;
    }

    if (temperature_c < 26.0 || relative_humidity < 40.0) {
        feels_like = static_cast<int>(std::lround(temperature_c));
        return true;
    }

    double temperature_f = temperature_c * 9.0 / 5.0 + 32.0;
    double heat_index_f =
        -42.379 + 2.04901523 * temperature_f + 10.14333127 * relative_humidity -
        0.22475541 * temperature_f * relative_humidity -
        0.00683783 * temperature_f * temperature_f -
        0.05481717 * relative_humidity * relative_humidity +
        0.00122874 * temperature_f * temperature_f * relative_humidity +
        0.00085282 * temperature_f * relative_humidity * relative_humidity -
        0.00000199 * temperature_f * temperature_f * relative_humidity * relative_humidity;
    feels_like = static_cast<int>(std::lround((heat_index_f - 32.0) * 5.0 / 9.0));
    return true;
}

std::string StandbyDashboard::WindTitle(const std::string& direction) {
    if (direction.empty()) {
        return "风向";
    }
    if (direction.find("风") != std::string::npos) {
        return direction;
    }
    return direction + "风";
}

std::string StandbyDashboard::WindPower(const std::string& power) {
    if (power.empty()) {
        return "--";
    }
    if (power.find("级") != std::string::npos) {
        return power;
    }
    return power + "级";
}

std::string StandbyDashboard::AirCategory(int aqi, const std::string& category) {
    if (!category.empty()) {
        return category;
    }
    if (aqi < 0) {
        return "--";
    }
    if (aqi <= 50) {
        return "优";
    }
    if (aqi <= 100) {
        return "良";
    }
    if (aqi <= 150) {
        return "轻度";
    }
    if (aqi <= 200) {
        return "中度";
    }
    return "重度";
}

lv_color_t StandbyDashboard::AirCategoryColor(int aqi) {
    if (aqi < 0) {
        return lv_color_hex(kColorMuted);
    }
    if (aqi <= 50) {
        return lv_color_hex(0x5CFF73);
    }
    if (aqi <= 100) {
        return lv_color_hex(0xE9ED4A);
    }
    if (aqi <= 150) {
        return lv_color_hex(0xFFB020);
    }
    return lv_color_hex(0xFF5A67);
}

void StandbyDashboard::NotificationTimerCallback(lv_timer_t* timer) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_timer_get_user_data(timer));
    if (dashboard != nullptr && dashboard->notification_chip_ != nullptr) {
        lv_obj_add_flag(dashboard->notification_chip_, LV_OBJ_FLAG_HIDDEN);
    }
}

void StandbyDashboard::PomodoroEntryEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->ShowPomodoroView();
    }
}

void StandbyDashboard::PomodoroPresetEventCallback(lv_event_t* event) {
    auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    auto* dashboard = static_cast<StandbyDashboard*>(lv_obj_get_user_data(button));
    int minutes = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if (dashboard != nullptr) {
        dashboard->HandlePomodoroPreset(minutes);
    }
}

void StandbyDashboard::PomodoroPauseEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->HandlePomodoroPauseResume();
    }
}

void StandbyDashboard::PomodoroCloseEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->HandlePomodoroClose();
    }
}

void StandbyDashboard::CalendarEntryEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->ShowCalendarView();
    }
}

void StandbyDashboard::CalendarPreviousMonthEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->ChangeCalendarMonth(-1);
    }
}

void StandbyDashboard::CalendarNextMonthEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->ChangeCalendarMonth(1);
    }
}

void StandbyDashboard::CalendarTodayEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->ReturnCalendarToToday();
    }
}

void StandbyDashboard::CalendarDayEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    int day = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(button)));
    if (dashboard != nullptr && day > 0) {
        dashboard->SelectCalendarDate(day);
    }
}

void StandbyDashboard::CalendarCloseEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->CloseCalendarView();
    }
}

void StandbyDashboard::MusicEntryEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->ShowMusicView();
    }
}

void StandbyDashboard::MusicRefreshEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->RefreshMusicPlaylist();
    }
}

void StandbyDashboard::MusicTrackEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    auto* row = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row)));
    if (dashboard != nullptr && index >= 0) {
        dashboard->HandleMusicTrackSelect(index);
    }
}

void StandbyDashboard::MusicPreviousEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->HandleMusicPrevious();
    }
}

void StandbyDashboard::MusicPlayPauseEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->HandleMusicPlayPause();
    }
}

void StandbyDashboard::MusicNextEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->HandleMusicNext();
    }
}

void StandbyDashboard::MusicCloseEventCallback(lv_event_t* event) {
    auto* dashboard = static_cast<StandbyDashboard*>(lv_event_get_user_data(event));
    if (dashboard != nullptr) {
        dashboard->CloseMusicView();
    }
}
