#include "speaker_home_display.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "assets/lang_config.h"
#include "hncig_logo_i2.h"
#include "lvgl_font.h"
#include "lvgl_image.h"
#include "speaker_account/speaker_account.h"
#include "speaker_home_widgets.h"

#include <cstring>
#include <ctime>
#include <string>

namespace {

lv_obj_t* AddLabel(lv_obj_t* parent, const char* text, uint32_t color, lv_align_t align, int x,
                   int y) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_align(label, align, x, y);
    return label;
}

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
    home_ = lv_obj_create(lv_screen_active());
    lv_obj_set_size(home_, width(), height() - 104);
    lv_obj_align(home_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(home_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(home_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(home_, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(home_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(home_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(home_, lv_color_hex(0x050A14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(home_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(home_, lv_color_hex(0x071525), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(home_, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
    if (auto* theme = GetTheme(); theme && theme->GetTextFont()) {
        lv_obj_set_style_text_font(home_, theme->GetTextFont()->font(), LV_PART_MAIN);
    }

    auto* logo = lv_image_create(home_);
    lv_image_set_src(logo, &hncig_logo_i2);
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 36, -30);
    AddLabel(home_, "筑音", 0xF4F8FF, LV_ALIGN_LEFT_MID, 72, 58);

    clock_ = AddLabel(home_, "--:--", 0x27E5FF, LV_ALIGN_CENTER, 55, -45);
#if LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(clock_, &lv_font_montserrat_48, LV_PART_MAIN);
#endif
    date_ = AddLabel(home_, "正在校时", 0xA8B6CB, LV_ALIGN_CENTER, 55, 12);
    account_ = AddLabel(home_, "", 0xF4F8FF, LV_ALIGN_CENTER, 55, 72);
    lv_obj_set_width(account_, width() - 280);
    lv_label_set_long_mode(account_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(account_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

#if LV_USE_CALENDAR
    calendar_.Create(home_);
    auto* calendar_button = SpeakerHomeWidgets::Button(home_, "日历", [](lv_event_t* event) {
        static_cast<SpeakerHomeDisplay*>(lv_event_get_user_data(event))->calendar_.Show();
    }, this);
    lv_obj_align(calendar_button, LV_ALIGN_BOTTOM_MID, -75, -18);
#endif

    refresh_timer_ = lv_timer_create([](lv_timer_t* timer) {
        static_cast<SpeakerHomeDisplay*>(lv_timer_get_user_data(timer))->RefreshHome();
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
    MipiLcdDisplay::SetStatus(status);
    DisplayLockGuard lock(this);
    if (lock && home_) {
        standby_ = status && std::strcmp(status, Lang::Strings::STANDBY) == 0;
        RefreshHome();
    }
}

void SpeakerHomeDisplay::SetChatMessage(const char* role, const char* content) {
    MipiLcdDisplay::SetChatMessage(role, content);
    if (!role || !content || !*content) {
        return;
    }
    {
        DisplayLockGuard lock(this);
        if (!lock || !home_) {
            return;
        }
        message_role_ = role;
        message_ = content;
        lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
    }

    const auto state = Application::GetInstance().GetDeviceState();
    if ((state == kDeviceStateListening || state == kDeviceStateSpeaking)
        && std::strcmp(role, "user") == 0) {
        MipiLcdDisplay::SetStatus("正在处理");
    } else if (state == kDeviceStateSpeaking && std::strcmp(role, "assistant") == 0) {
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
        lv_obj_add_flag(home_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(home_, LV_OBJ_FLAG_HIDDEN);
    const auto now = std::time(nullptr);
    std::tm local{};
    if (now >= 1704067200 && localtime_r(&now, &local)) {
        char value[32];
        std::strftime(value, sizeof(value), "%H:%M", &local);
        UpdateLabel(clock_, value);
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
