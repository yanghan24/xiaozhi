#pragma once

#include "display/lcd_display.h"
#include <string>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_calendar_view.h"
#include "speaker_home_widgets.h"
#include "speaker_pomodoro_view.h"
#include "speaker_music_view.h"

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
    void SetIdleClock(const char* clock) override;
    void RefreshHome();

    lv_obj_t* home_ = nullptr;
    SpeakerHomeWidgets::DigitalClock clock_;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* account_ = nullptr;
    lv_obj_t* weather_city_ = nullptr;
    lv_obj_t* weather_condition_ = nullptr;
    lv_obj_t* weather_details_ = nullptr;
    lv_obj_t* home_network_ = nullptr;
    lv_obj_t* home_mute_ = nullptr;
    lv_obj_t* home_battery_ = nullptr;
    lv_obj_t* home_notice_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    bool standby_ = false;
    bool has_preview_ = false;
    bool awaiting_reply_ = false;
    std::string message_role_;
    std::string message_;
#if LV_USE_CALENDAR
    SpeakerCalendarView calendar_;
#endif
    SpeakerPomodoroView pomodoro_;
    SpeakerMusicView music_;
};

#endif
