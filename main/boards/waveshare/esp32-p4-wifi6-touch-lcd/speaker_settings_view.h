#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"
#include <cstdint>

class SpeakerSettingsView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void UpdateSoundAndDisplay();
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
    struct LevelSlider {
        lv_obj_t* control = nullptr;
        lv_obj_t* value_label = nullptr;
        int displayed_value = -1;
        bool brightness = false;
        bool editing = false;
        bool changed = false;
    };

    void CreateSoundAndDisplay(lv_obj_t* parent, SpeakerNavigation& navigation);
    void CreateLevelSlider(lv_obj_t* panel, LevelSlider& slider, const char* title,
                           const char* icon, int y, bool brightness);
    static void OnLevelSlider(lv_event_t* event);
    void CreateScreensaver(lv_obj_t* parent, SpeakerNavigation& navigation);
    void CreateAbout(lv_obj_t* parent, SpeakerNavigation& navigation);
    void SetScreensaverMinutes(int minutes);
    void UpdateTimeoutButtons();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* timeout_hint_ = nullptr;
    int screensaver_minutes_ = 1;
    std::array<RouteAction, 4> routes_{};
    LevelSlider volume_;
    LevelSlider brightness_;
    std::array<TimeoutAction, 4> timeouts_{};
    std::array<lv_obj_t*, 4> timeout_buttons_{};
};

#endif
