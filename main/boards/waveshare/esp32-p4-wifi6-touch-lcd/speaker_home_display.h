#pragma once

#include "display/lcd_display.h"
#include <string>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_calendar_view.h"

class SpeakerHomeDisplay : public MipiLcdDisplay {
public:
    using MipiLcdDisplay::MipiLcdDisplay;
    ~SpeakerHomeDisplay() override;
    void SetupUI() override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void RemoveChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetTheme(Theme* theme) override;

private:
    void RefreshHome();

    lv_obj_t* home_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* account_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    bool standby_ = false;
    bool has_preview_ = false;
    std::string message_role_;
    std::string message_;
#if LV_USE_CALENDAR
    SpeakerCalendarView calendar_;
#endif
};

#endif
