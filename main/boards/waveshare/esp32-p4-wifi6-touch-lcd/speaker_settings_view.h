#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"
#include <cstdint>

class SpeakerSettingsView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    uint32_t ScreensaverDelayMs() const;

private:
    struct RouteAction {
        SpeakerNavigation* navigation = nullptr;
        SpeakerNavigation::Page page = SpeakerNavigation::Page::Home;
    };
    struct TimeoutAction {
        SpeakerSettingsView* view = nullptr;
        int minutes = 0;
    };

    void CreateScreensaver(lv_obj_t* parent, SpeakerNavigation& navigation);
    void CreateAbout(lv_obj_t* parent, SpeakerNavigation& navigation);
    void SetScreensaverMinutes(int minutes);
    void UpdateTimeoutButtons();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* timeout_hint_ = nullptr;
    int screensaver_minutes_ = 1;
    std::array<RouteAction, 3> routes_{};
    std::array<TimeoutAction, 4> timeouts_{};
    std::array<lv_obj_t*, 4> timeout_buttons_{};
};

#endif
