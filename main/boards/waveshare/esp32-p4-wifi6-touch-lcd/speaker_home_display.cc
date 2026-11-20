#include "speaker_home_display.h"

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

namespace {

void UpdateLabel(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

}  // namespace

SpeakerHomeDisplay::~SpeakerHomeDisplay() {
    DisplayLockGuard lock(this);
    if (lock && refresh_timer_) {
        lv_timer_delete(refresh_timer_);
        lv_obj_delete(home_);
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
    constexpr int status_bar_height = 64;
    using namespace SpeakerHomeWidgets;
    home_ = Panel(lv_screen_active());
    lv_obj_set_size(home_, width(), height() - status_bar_height);
    lv_obj_align(home_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(home_, LV_OBJ_FLAG_CLICKABLE);
    if (auto* theme = GetTheme(); theme && theme->GetTextFont()) {
        lv_obj_set_style_text_font(home_, theme->GetTextFont()->font(), LV_PART_MAIN);
    }

    auto* clock_card = Card(home_, 18, 62, 364, 322);
    lv_obj_set_style_border_color(clock_card, lv_color_hex(kCyan), LV_PART_MAIN);
    lv_obj_set_style_border_opa(clock_card, LV_OPA_50, LV_PART_MAIN);
    lv_obj_align(clock_.Create(clock_card, kCyan), LV_ALIGN_CENTER, 0, -25);
    date_ = Label(clock_card, "正在校时", kText);
    lv_obj_align(date_, LV_ALIGN_CENTER, 0, 75);

    auto* account_card = Card(home_, 418, 62, 364, 322);
    auto* logo = lv_image_create(account_card);
    lv_image_set_src(logo, &hncig_logo_i2);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_align(Label(account_card, "筑音", kText), LV_ALIGN_TOP_MID, 0, 180);
    account_ = Label(account_card, "", kMuted);
    lv_obj_set_width(account_, 312);
    lv_obj_align(account_, LV_ALIGN_TOP_MID, 0, 237);
    lv_label_set_long_mode(account_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(account_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

#if LV_USE_CALENDAR
    calendar_.Create(home_);
    auto* calendar_button = Button(home_, "日历", [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->calendar_.Show();
    }, this);
    lv_obj_set_size(calendar_button, 86, 44);
    lv_obj_set_pos(calendar_button, 418, 6);
#endif

    pomodoro_.Create(home_);
    auto* pomodoro_button = Button(home_, "番茄钟", [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->pomodoro_.Show();
    }, this);
    lv_obj_set_size(pomodoro_button, 110, 44);
    lv_obj_set_pos(pomodoro_button, 298, 6);

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
        lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
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
    if (!standby_ || state != kDeviceStateIdle || !message_.empty() || has_preview_) {
#if LV_USE_CALENDAR
        calendar_.Hide();
#endif
        pomodoro_.Hide();
        lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(home_, LV_OBJ_FLAG_HIDDEN);
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
        ? (view.customer.empty() ? "账号已登录" : "账号  " + view.customer)
        : "";
    UpdateLabel(account_, account.c_str());
#if LV_USE_CALENDAR
    calendar_.Update();
#endif
}

#endif
