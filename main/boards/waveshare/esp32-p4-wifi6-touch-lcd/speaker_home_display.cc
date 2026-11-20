#include "speaker_home_display.h"
#include "speaker_standby_weather.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "assets/lang_config.h"
#include "hncig_logo_i2.h"
#include "lvgl_font.h"
#include "lvgl_image.h"
#include "speaker_account/speaker_account.h"

#include <cstring>
#include <ctime>
#include <string>

using SpeakerHomeWidgets::UpdateLabel;

SpeakerHomeDisplay::~SpeakerHomeDisplay() {
    DisplayLockGuard lock(this);
    if (lock && refresh_timer_) {
        lv_timer_delete(refresh_timer_);
        lv_obj_delete(home_);
        SetSpeakerAccountLauncherVisible(true);
    }
}

void SpeakerHomeDisplay::SetupUI() {
    if (IsSetupUICalled()) {
        return;
    }
    MipiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    using namespace SpeakerHomeWidgets;
    home_ = Panel(lv_screen_active());
    lv_obj_remove_flag(home_, LV_OBJ_FLAG_CLICKABLE);
    if (auto* theme = GetTheme(); theme && theme->GetTextFont()) {
        lv_obj_set_style_text_font(home_, theme->GetTextFont()->font(), LV_PART_MAIN);
    }

    Rule(home_, 399, 76, 2, 350, kCyan, LV_OPA_60);
    Rule(home_, 397, 235, 6, 34, kCyan, LV_OPA_80);
    Rule(home_, 0, 454, 180, 2, kBlue, LV_OPA_70);
    Rule(home_, 620, 454, 180, 2, kBlue, LV_OPA_70);

    lv_obj_set_pos(Label(home_, "筑音", kText), 18, 7);
    auto* clock_card = Card(home_, 18, 72, 364, 160);
    lv_obj_set_style_border_color(clock_card, lv_color_hex(kCyan), LV_PART_MAIN);
    lv_obj_set_style_border_opa(clock_card, LV_OPA_50, LV_PART_MAIN);
    lv_obj_center(clock_.Create(clock_card, kCyan));
    date_ = Label(home_, "正在校时", kText);
    lv_obj_set_pos(date_, 18, 240);
    lv_obj_set_width(date_, 364);
    lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    auto* logo_frame = Card(home_, 126, 284, 148, 148);
    lv_obj_set_style_radius(logo_frame, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(logo_frame, lv_color_hex(kCyan), LV_PART_MAIN);
    auto* logo = lv_image_create(home_);
    lv_image_set_src(logo, &hncig_logo_i2);
    lv_obj_align_to(logo, logo_frame, LV_ALIGN_CENTER, 0, 0);

    auto* weather_card = Card(home_, 420, 72, 364, 232);
    lv_obj_set_pos(Label(weather_card, "天气", kCyan), 18, 14);
    weather_city_ = Label(weather_card, "", kText);
    lv_obj_set_pos(weather_city_, 104, 14);
    lv_obj_set_width(weather_city_, 236);
    lv_label_set_long_mode(weather_city_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(weather_city_, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    Rule(weather_card, 18, 56, 326, 1, kCyan, LV_OPA_40);
    weather_condition_ = Label(weather_card, "正在获取天气", kText);
    lv_obj_set_width(weather_condition_, 326);
    lv_obj_align(weather_condition_, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_align(weather_condition_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    weather_details_ = Label(weather_card, "", kMuted);
    lv_obj_set_width(weather_details_, 326);
    lv_obj_align(weather_details_, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_text_align(weather_details_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    SpeakerStandbyWeather::Get().Start();

    auto* account_card = Card(home_, 420, 319, 364, 109);
    lv_obj_set_pos(Label(account_card, "账号", kCyan), 18, 14);
    Rule(account_card, 18, 45, 326, 1, kCyan, LV_OPA_40);
    account_ = Label(account_card, "", kMuted);
    lv_obj_set_width(account_, 312);
    lv_obj_align(account_, LV_ALIGN_CENTER, 0, 25);
    lv_label_set_long_mode(account_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(account_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

#if LV_USE_CALENDAR
    auto* calendar_button = Button(home_, "日历", [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->calendar_.Show();
    }, this);
    lv_obj_set_size(calendar_button, 84, 40);
    lv_obj_set_pos(calendar_button, 420, 4);
#endif

    auto* pomodoro_button = Button(home_, "番茄钟", [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->pomodoro_.Show();
    }, this);
    lv_obj_set_size(pomodoro_button, 110, 40);
    lv_obj_set_pos(pomodoro_button, 304, 4);
    auto* music_button = Button(home_, "音乐", [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->music_.Show();
    }, this);
    lv_obj_set_size(music_button, 84, 40);
    lv_obj_set_pos(music_button, 104, 4);
    auto* recording_button = Button(home_, "录音", [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->recording_.Show();
    }, this);
    lv_obj_set_size(recording_button, 84, 40);
    lv_obj_set_pos(recording_button, 200, 4);

    // Draw shared status controls above the independently managed pages.
#if LV_USE_CALENDAR
    calendar_.Create(home_);
#endif
    pomodoro_.Create(home_);
    music_.Create(home_);
    recording_.Create(home_);
    auto* account_button = Button(home_, "账号", [](lv_event_t*) {
        ToggleSpeakerAccountDetails();
    }, nullptr);
    lv_obj_set_size(account_button, 84, 40);
    lv_obj_set_pos(account_button, 570, 4);

    auto* icons = lv_obj_create(home_);
    lv_obj_remove_style_all(icons);
    lv_obj_remove_flag(icons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(icons, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(icons, 122, 46);
    lv_obj_align(icons, LV_ALIGN_TOP_RIGHT, -16, 0);
    lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(icons, 8, LV_PART_MAIN);
    lv_obj_set_style_text_font(icons, lv_obj_get_style_text_font(network_label_, LV_PART_MAIN),
                               LV_PART_MAIN);
    home_network_ = Label(icons, "", kText);
    home_mute_ = Label(icons, "", kText);
    home_battery_ = Label(icons, "", kCyan);

    home_notice_ = Label(home_, "", kText);
    lv_obj_set_width(home_notice_, 520);
    lv_obj_align(home_notice_, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_text_align(home_notice_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(home_notice_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_bg_color(home_notice_, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(home_notice_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(home_notice_, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(home_notice_, 8, LV_PART_MAIN);
    lv_obj_add_flag(home_notice_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(low_battery_popup_);

    refresh_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* display = static_cast<SpeakerHomeDisplay*>(lv_timer_get_user_data(timer));
        display->pomodoro_.Update();
        display->RefreshHome();
    }, 250, this);
}

void SpeakerHomeDisplay::SetTheme(Theme* theme) {
    MipiLcdDisplay::SetTheme(theme);
    DisplayLockGuard lock(this);
    if (lock && home_ && theme && theme->GetTextFont()) {
        lv_obj_set_style_text_font(home_, theme->GetTextFont()->font(), LV_PART_MAIN);
    }
}

void SpeakerHomeDisplay::SetStatus(const char* status) {
    const char* visible_status = status;
    {
        DisplayLockGuard lock(this);
        if (lock && home_ && status) {
            standby_ = std::strcmp(status, Lang::Strings::STANDBY) == 0;
            const bool speaking = std::strcmp(status, Lang::Strings::SPEAKING) == 0
                && Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking;
            if (speaking && awaiting_reply_) {
                // The server can enter the speaking state before sending a reply sentence.
                visible_status = "正在处理";
            } else if (!speaking) {
                awaiting_reply_ = false;
            }
            RefreshHome();
        }
    }
    MipiLcdDisplay::SetStatus(visible_status);
}

void SpeakerHomeDisplay::SetIdleClock(const char* clock) {
    // The status-bar clock is a display refresh. Standby follows application status.
    MipiLcdDisplay::SetStatus(clock);
}

void SpeakerHomeDisplay::SetChatMessage(const char* role, const char* content) {
    MipiLcdDisplay::SetChatMessage(role, content);
    if (!role) {
        return;
    }
    if (!content || !*content) {
        DisplayLockGuard lock(this);
        if (lock && home_) {
            message_.clear();
            RefreshHome();
        }
        return;
    }
    const auto state = Application::GetInstance().GetDeviceState();
    const bool recognized = (state == kDeviceStateListening || state == kDeviceStateSpeaking)
        && std::strcmp(role, "user") == 0;
    const bool reply = state == kDeviceStateSpeaking && std::strcmp(role, "assistant") == 0;
    {
        DisplayLockGuard lock(this);
        if (!lock || !home_) {
            return;
        }
        message_role_ = role;
        message_ = content;
        if (recognized || reply) {
            awaiting_reply_ = recognized;
        }
        RefreshHome();
    }

    if (recognized) {
        MipiLcdDisplay::SetStatus("正在处理");
    } else if (reply) {
        MipiLcdDisplay::SetStatus(Lang::Strings::SPEAKING);
    }
}

void SpeakerHomeDisplay::RemoveChatMessage(const char* role, const char* content) {
    MipiLcdDisplay::RemoveChatMessage(role, content);
    DisplayLockGuard lock(this);
    if (lock && home_ && role && content && message_role_ == role && message_ == content) {
        message_.clear();
        RefreshHome();
    }
}

void SpeakerHomeDisplay::ClearChatMessages() {
    MipiLcdDisplay::ClearChatMessages();
    DisplayLockGuard lock(this);
    if (lock && home_) {
        message_.clear();
        awaiting_reply_ = false;
        RefreshHome();
    }
}

void SpeakerHomeDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    const bool visible = image != nullptr;
    MipiLcdDisplay::SetPreviewImage(std::move(image));
    DisplayLockGuard lock(this);
    if (lock && home_) {
        has_preview_ = visible;
        RefreshHome();
    }
}

void SpeakerHomeDisplay::RefreshHome() {
    const auto state = Application::GetInstance().GetDeviceState();
    const bool show = standby_ && state == kDeviceStateIdle && message_.empty() && !has_preview_;
    lv_obj_set_flag(home_, LV_OBJ_FLAG_HIDDEN, !show);
    SetSpeakerAccountLauncherVisible(!show);
    if (!show) {
#if LV_USE_CALENDAR
        calendar_.Hide();
#endif
        pomodoro_.Hide();
        music_.Hide();
        recording_.Hide();
        return;
    }
    UpdateLabel(home_network_, lv_label_get_text(network_label_));
    UpdateLabel(home_mute_, lv_label_get_text(mute_label_));
    UpdateLabel(home_battery_, lv_label_get_text(battery_label_));
    const bool notice_visible = !lv_obj_has_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    if (notice_visible) {
        UpdateLabel(home_notice_, lv_label_get_text(notification_label_));
    }
    lv_obj_set_flag(home_notice_, LV_OBJ_FLAG_HIDDEN, !notice_visible);
    const auto now = std::time(nullptr);
    std::tm local{};
    if (now >= 1704067200 && localtime_r(&now, &local)) {
        char value[32];
        std::strftime(value, sizeof(value), "%H:%M", &local);
        clock_.SetTime(value);
        std::strftime(value, sizeof(value), "%Y 年 %m 月 %d 日", &local);
        UpdateLabel(date_, value);
    }
    const auto view = SpeakerAccount::Get().GetView();
    const std::string account = view.page == SpeakerAccount::Page::Ready
        ? (view.customer.empty() ? "已登录" : view.customer)
        : "";
    UpdateLabel(account_, account.c_str());
    const auto weather = SpeakerStandbyWeather::Get().GetSnapshot();
    std::string summary;
    std::string detail;
    switch (weather.state) {
        case SpeakerStandbyWeather::State::Loading: summary = "正在获取天气"; break;
        case SpeakerStandbyWeather::State::Offline: summary = "网络已断开"; break;
        case SpeakerStandbyWeather::State::Unavailable: summary = "暂无天气数据"; break;
        case SpeakerStandbyWeather::State::Ready:
            summary = weather.condition;
            if (!weather.temperature.empty()) summary += "   " + weather.temperature + " ℃";
            if (!weather.humidity.empty()) detail = "湿度 " + weather.humidity + "%";
            if (!weather.wind.empty()) {
                if (!detail.empty()) detail += "    ";
                detail += "风 " + weather.wind;
            }
            break;
    }
    UpdateLabel(weather_city_, weather.city.c_str());
    UpdateLabel(weather_condition_, summary.c_str());
    UpdateLabel(weather_details_, detail.c_str());
    music_.Update();
    recording_.Update();
#if LV_USE_CALENDAR
    calendar_.Update();
#endif
}

#endif
