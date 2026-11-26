#pragma once
#include <sdkconfig.h>

#include "display/lcd_display.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_account_page.h"
#include "speaker_boot_view.h"
#include "speaker_calculator_view.h"
#include "speaker_calendar_view.h"
#include "speaker_conversation_view.h"
#include "speaker_launcher_view.h"
#include "speaker_music_view.h"
#include "speaker_pomodoro_view.h"
#include "speaker_recording_view.h"
#include "speaker_screensaver_view.h"
#include "speaker_settings_view.h"
#include "speaker_wifi_view.h"

#include <ctime>
#include <string>

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
    void SetVoicePlaybackActive(bool active) override;
    void SetHideSubtitle(bool hide) override;
    void OnTouch() { pomodoro_.StopAlarm(); }

private:
    void SetIdleClock(const char* clock) override;
    void ApplySystemStyle();
    void RefreshUi();
    void RefreshClocks();
    void ExitScreensaver();

    lv_obj_t* scene_ = nullptr;
    lv_obj_t* desktop_ = nullptr;
    lv_obj_t* notice_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    bool standby_ = false;
    bool desktop_allowed_ = false;
    bool system_background_visible_ = false;
    bool has_preview_ = false;
    bool awaiting_reply_ = false;
    bool voice_playback_ = false;
    std::string message_role_;
    std::string message_;
    std::string recognized_;
    std::time_t last_clock_second_ = -1;

    SpeakerNavigation navigation_;
    SpeakerBackground system_background_;
    SpeakerBootView boot_;
    SpeakerLauncherView launcher_;
    SpeakerScreensaverView screensaver_;
    SpeakerSettingsView settings_;
#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI
    SpeakerWifiView wifi_;
#endif
    SpeakerAccountPage account_;
#if LV_USE_CALENDAR
    SpeakerCalendarView calendar_;
#endif
    SpeakerPomodoroView pomodoro_;
    SpeakerCalculatorView calculator_;
    SpeakerMusicView music_;
    SpeakerRecordingView recording_;
    SpeakerConversationView conversation_;
};

#endif
