#include "speaker_home_display.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_image.h"
#include "speaker_account/display_diagnostics.h"
#include "speaker_account/speaker_clock.h"
#include "speaker_account/speaker_ui_widgets.h"
#include "speaker_memos.h"
#include "speaker_standby_weather.h"

#include <cstring>
#include <ctime>

using namespace SpeakerUi;

SpeakerHomeDisplay::~SpeakerHomeDisplay() {
    DisplayLockGuard lock(this);
    if (!lock || !scene_) {
        return;
    }
    if (touch_) {
        lv_indev_remove_event_cb_with_user_data(touch_, OnTouchPressed, this);
    }
    lv_timer_delete(refresh_timer_);
    conversation_.Stop();
    account_.Destroy();
    screensaver_.Hide();
    lv_obj_delete(scene_);
    system_background_.Release(container_);
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

    scene_ = Surface(lv_screen_active(), 0, 0, kWidth, kHeight);
    if (auto* theme = GetTheme(); theme && theme->GetTextFont()) {
        lv_obj_set_style_text_font(scene_, theme->GetTextFont()->font(), LV_PART_MAIN);
    }
    desktop_ = Surface(scene_, 0, 0, kWidth, kHeight);
    lv_obj_add_flag(desktop_, LV_OBJ_FLAG_HIDDEN);
    auto* system = Surface(scene_, 0, 0, kWidth, kHeight);
    launcher_.Create(desktop_, navigation_);
    settings_.Create(desktop_, navigation_);
    account_.Create(desktop_, system, SpeakerNavigation::OnBack, &navigation_);
    music_.Create(desktop_, navigation_);
    recording_.Create(desktop_, navigation_);
    pomodoro_.Create(desktop_, navigation_);
    calculator_.Create(desktop_, navigation_);
#if LV_USE_CALENDAR
    calendar_.Create(desktop_, navigation_);
#endif
    navigation_.Create(desktop_);

    using Page = SpeakerNavigation::Page;
    navigation_.Register(Page::Home, [this] { launcher_.Show(); }, [this] { launcher_.Hide(); });
    navigation_.Register(Page::Account, [this] { account_.Show(); }, [this] { account_.Hide(); });
    navigation_.Register(Page::Music, [this] { music_.Show(); }, [this] { music_.Hide(); });
    navigation_.Register(Page::Recording, [this] { recording_.Show(); }, [this] { recording_.Hide(); },
                         [this] { return recording_.CanLeave(); });
    navigation_.Register(Page::Pomodoro, [this] { pomodoro_.Show(); }, [this] { pomodoro_.Hide(); });
    navigation_.Register(
        Page::Calculator, [this] { calculator_.Show(); }, [this] { calculator_.Hide(); });
#if LV_USE_CALENDAR
    navigation_.Register(Page::Calendar, [this] { calendar_.Show(); }, [this] { calendar_.Hide(); },
                         [this] { return calendar_.CanLeave(); });
#endif
    navigation_.Home();

    screensaver_.Create(scene_, [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->ExitScreensaver();
    }, this);
    conversation_.Create(scene_, [](void* context) {
        static_cast<SpeakerHomeDisplay*>(context)->navigation_.Home();
        Application::GetInstance().EndSpeakerConversation();
    }, this);
    boot_.Create(scene_, system_background_);
#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI
    wifi_.Create(scene_, navigation_);
#endif
    ApplySystemStyle();

    notice_ = Label(scene_, "", kText);
    lv_obj_set_width(notice_, 690);
    lv_obj_set_style_text_align(notice_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(notice_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_bg_color(notice_, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(notice_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(notice_, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(notice_, 8, LV_PART_MAIN);
    lv_obj_align(notice_, LV_ALIGN_BOTTOM_MID, 0, -33);
    lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(low_battery_popup_);
    SpeakerStandbyWeather::Get().Start();
    refresh_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* display = static_cast<SpeakerHomeDisplay*>(lv_timer_get_user_data(timer));
        display->pomodoro_.Update();
        SpeakerMemos::Get().Tick();
        display->RefreshUi();
    }, 100, this);

    for (auto* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_display(indev) == lv_obj_get_display(scene_)) {
            touch_ = indev;
            lv_indev_add_event_cb(touch_, OnTouchPressed, LV_EVENT_PRESSED, this);
            break;
        }
    }
}

void SpeakerHomeDisplay::OnTouchPressed(lv_event_t* event) {
    auto* display = static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event));
    display->pomodoro_.RequestStopAlarm();
}

void SpeakerHomeDisplay::SetTheme(Theme* theme) {
    MipiLcdDisplay::SetTheme(theme);
    DisplayLockGuard lock(this);
    if (lock && scene_ && theme && theme->GetTextFont()) {
        lv_obj_set_style_text_font(scene_, theme->GetTextFont()->font(), LV_PART_MAIN);
        ApplySystemStyle();
        RefreshUi();
    }
}

void SpeakerHomeDisplay::ApplySystemStyle() {
    // Keep the original system display objects and their visibility rules.
    lv_obj_set_style_bg_color(container_, lv_color_hex(kDeep), LV_PART_MAIN);
    if (system_background_visible_) {
        system_background_.Load(container_, SpeakerBackground::Artwork::Boot);
    }

    for (auto* bar : {top_bar_, bottom_bar_}) {
        if (bar) {
            lv_obj_set_style_bg_color(bar, lv_color_hex(kDeep), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bar, LV_OPA_50, LV_PART_MAIN);
        }
    }
    for (auto* label : {status_label_, notification_label_, chat_message_label_, network_label_,
                        mute_label_, battery_label_, emoji_label_}) {
        if (label) {
            lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
        }
    }
    lv_obj_set_style_image_recolor(emoji_image_, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(emoji_image_, LV_OPA_COVER, LV_PART_MAIN);
}

void SpeakerHomeDisplay::SetStatus(const char* status) {
    MipiLcdDisplay::SetStatus(status);
    DisplayLockGuard lock(this);
    if (lock && scene_ && status) {
        standby_ = std::strcmp(status, Lang::Strings::STANDBY) == 0;
        if (standby_ || std::strcmp(status, Lang::Strings::LISTENING) == 0 ||
            std::strcmp(status, Lang::Strings::CONNECTING) == 0) {
            awaiting_reply_ = false;
            recognized_.clear();
        }
        RefreshUi();
    }
}

void SpeakerHomeDisplay::SetIdleClock(const char* clock) {
    {
        DisplayLockGuard lock(this);
        if (!lock) {
            return;
        }
        if (notification_label_ && !lv_obj_has_flag(notification_label_, LV_OBJ_FLAG_HIDDEN)) {
            lv_label_set_text(status_label_, clock);
            return;
        }
    }
    MipiLcdDisplay::SetStatus(clock);
}

void SpeakerHomeDisplay::SetChatMessage(const char* role, const char* content) {
    MipiLcdDisplay::SetChatMessage(role, content);
    if (!role) {
        return;
    }
    const auto state = Application::GetInstance().GetDeviceState();
    DisplayLockGuard lock(this);
    if (!lock || !scene_) {
        return;
    }
    message_role_ = role;
    message_ = content ? content : "";
    if (message_role_ == "assistant") {
        conversation_.OnNewSubtitle();
    }
    if (!message_.empty() && (state == kDeviceStateListening || state == kDeviceStateSpeaking) &&
        message_role_ == "user") {
        awaiting_reply_ = true;
        recognized_ = message_;
    }
    RefreshUi();
}

void SpeakerHomeDisplay::RemoveChatMessage(const char* role, const char* content) {
    MipiLcdDisplay::RemoveChatMessage(role, content);
    DisplayLockGuard lock(this);
    if (lock && scene_ && role && content && message_role_ == role && message_ == content) {
        message_.clear();
        RefreshUi();
    }
}

void SpeakerHomeDisplay::ClearChatMessages() {
    MipiLcdDisplay::ClearChatMessages();
    DisplayLockGuard lock(this);
    if (lock && scene_ && !voice_playback_) {
        message_.clear();
        recognized_.clear();
        awaiting_reply_ = false;
        RefreshUi();
    }
}

void SpeakerHomeDisplay::SetVoicePlaybackActive(bool active) {
    DisplayLockGuard lock(this);
    if (!lock || !scene_ || voice_playback_ == active) {
        return;
    }
    voice_playback_ = active;
    if (!active) {
        awaiting_reply_ = false;
        recognized_.clear();
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            message_.clear();
        }
    }
    RefreshUi();
}

void SpeakerHomeDisplay::SetHideSubtitle(bool hide) {
    MipiLcdDisplay::SetHideSubtitle(hide);
    DisplayLockGuard lock(this);
    if (lock && scene_) {
        RefreshUi();
    }
}

void SpeakerHomeDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    const bool visible = image != nullptr;
    MipiLcdDisplay::SetPreviewImage(std::move(image));
    DisplayLockGuard lock(this);
    if (lock && scene_) {
        has_preview_ = visible;
        RefreshUi();
    }
}

void SpeakerHomeDisplay::ExitScreensaver() {
    lv_display_trigger_activity(lv_obj_get_display(scene_));
    screensaver_.Hide();
    RefreshUi();
}

void SpeakerHomeDisplay::RefreshUi() {
    const auto state = Application::GetInstance().GetDeviceState();
    const bool voice = !has_preview_ && (voice_playback_ || state == kDeviceStateConnecting ||
        state == kDeviceStateListening || state == kDeviceStateSpeaking);
    const bool system_page = state == kDeviceStateWifiConfiguring || state == kDeviceStateUpgrading ||
        state == kDeviceStateFatalError || state == kDeviceStateAudioTesting || has_preview_;
    boot_.Update(voice || has_preview_ || state == kDeviceStateUpgrading || state == kDeviceStateFatalError,
                 system_background_);
    const bool boot_visible = boot_.Visible();
    account_.SetPromptsAllowed(!system_page && !boot_visible && !voice);
    const bool account_prompt = account_.PromptVisible();

    const bool show_desktop = standby_ && state == kDeviceStateIdle && !voice &&
        !boot_visible && !account_prompt && !has_preview_ && message_.empty();
    const bool notice = !lv_obj_has_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    if (show_desktop && !desktop_allowed_) {
        lv_display_trigger_activity(lv_obj_get_display(scene_));
    }
    desktop_allowed_ = show_desktop;
    if (!show_desktop) {
        screensaver_.Hide();
    } else if (!screensaver_.Visible() && navigation_.CanLeave() &&
               lv_display_get_inactive_time(lv_obj_get_display(scene_)) >= settings_.ScreensaverDelayMs()) {
        screensaver_.Show(settings_.GetScreensaverStyle());
        last_clock_second_ = -1;
    }
    lv_obj_set_flag(desktop_, LV_OBJ_FLAG_HIDDEN, !show_desktop || screensaver_.Visible());

    bool show_wifi = false;
#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI
    const bool configuring = state == kDeviceStateWifiConfiguring;
    show_wifi = !boot_visible && !voice && !account_prompt && !has_preview_ &&
        (configuring || (show_desktop && !screensaver_.Visible() &&
                         navigation_.Current() == SpeakerNavigation::Page::Wifi));
    wifi_.Update(show_wifi, configuring);
#endif
    const bool show_system_background = !boot_visible && !show_desktop && !voice && !account_prompt && !show_wifi;
    if (show_system_background && !system_background_visible_) {
        system_background_.Load(container_, SpeakerBackground::Artwork::Boot);
    } else if (!show_system_background && !boot_visible) {
        system_background_.Release(container_);
    }
    system_background_visible_ = show_system_background;

    const bool waiting = state == kDeviceStateSpeaking || awaiting_reply_;
    const auto motion = voice_playback_ ? SpeakerWave::Motion::Speaking :
        waiting || state == kDeviceStateConnecting ? SpeakerWave::Motion::Waiting : SpeakerWave::Motion::Listening;
    const char* status = notice ? lv_label_get_text(notification_label_) : voice_playback_ ? Lang::Strings::SPEAKING :
        state == kDeviceStateConnecting ? Lang::Strings::CONNECTING :
        waiting ? "正在处理" : Lang::Strings::LISTENING;
    const bool spoken_subtitle = voice_playback_ && message_role_ == "assistant";
    const char* subtitle = "";
    if (!hide_subtitle_ && !notice) {
        if (spoken_subtitle) {
            subtitle = message_.c_str();
        } else if (awaiting_reply_) {
            subtitle = recognized_.c_str();
        }
    }
    conversation_.Update(voice, motion, status, subtitle,
                         lv_obj_get_style_text_font(status_label_, LV_PART_MAIN), spoken_subtitle);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    using SpeakerDisplayDiagnostics::Mode;
    const auto display_mode = !voice ? Mode::Idle : voice_playback_ ? Mode::Speaking :
        state == kDeviceStateConnecting ? Mode::Connecting : waiting ? Mode::Waiting : Mode::Listening;
    SpeakerDisplayDiagnostics::SetContext(display_mode, conversation_.IsAnimating());
#endif

    if (show_desktop && notice) {
        UpdateLabel(notice_, lv_label_get_text(notification_label_));
    }
    lv_obj_set_flag(notice_, LV_OBJ_FLAG_HIDDEN, !show_desktop || !notice || show_wifi);
    if (show_desktop) {
        RefreshClocks();
        if (!screensaver_.Visible()) {
            if (navigation_.Current() == SpeakerNavigation::Page::SoundAndDisplay) {
                settings_.UpdateSoundAndDisplay();
            }
            music_.Update();
            recording_.Update();
#if LV_USE_CALENDAR
            calendar_.Update();
#endif
        }
    }
}

void SpeakerHomeDisplay::RefreshClocks() {
    const auto now = SpeakerClock::Now();
    if (now == last_clock_second_) {
        return;
    }
    last_clock_second_ = now;
    char time[12] = "--:--";
    char date[40] = "正在校时";
    std::tm local{};
    const auto* local_time = now ? localtime_r(&now, &local) : nullptr;
    if (local_time) {
        constexpr const char* days[] = {"日", "一", "二", "三", "四", "五", "六"};
        std::strftime(time, sizeof(time), "%H:%M", &local);
        std::snprintf(date, sizeof(date), "%d月%d日 周%s", local.tm_mon + 1, local.tm_mday, days[local.tm_wday]);
    }
    const auto weather = SpeakerStandbyWeather::Get().GetSnapshot();
    std::string weather_line;
    if (weather.state == SpeakerStandbyWeather::State::Ready) {
        weather_line = weather.condition;
        if (!weather.temperature.empty()) {
            weather_line += " " + weather.temperature + "°C";
        }
    } else if (weather.state == SpeakerStandbyWeather::State::Unavailable) {
        weather_line = "暂无天气";
    }
    const char* wifi = lv_label_get_text(network_label_);
    wifi = *wifi ? wifi : MATERIAL_SYMBOLS_WIFI_OFF;
    launcher_.Update(time, date, weather_line.c_str(), wifi);
    screensaver_.Update(local_time, weather, wifi);
}

#endif
