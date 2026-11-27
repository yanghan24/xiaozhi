#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"
#include "speaker_screensaver_view.h"
#include <cstdint>

class SpeakerSettingsView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void UpdateSoundAndDisplay();
    uint32_t ScreensaverDelayMs() const;
    SpeakerScreensaverView::Style GetScreensaverStyle() const { return screensaver_style_; }

private:
    using ScreensaverStyle = SpeakerScreensaverView::Style;
    struct RouteAction {
        SpeakerNavigation* navigation = nullptr;
        SpeakerNavigation::Page page = SpeakerNavigation::Page::Home;
    };
    struct TimeoutAction {
        SpeakerSettingsView* view = nullptr;
        int minutes = 0;
    };
    struct StyleAction {
        SpeakerSettingsView* view = nullptr;
        ScreensaverStyle style = ScreensaverStyle::Dashboard;
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
    void SetScreensaverStyle(ScreensaverStyle style);
    void UpdateTimeoutButtons();
    void UpdateStyleButtons();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* timeout_hint_ = nullptr;
    lv_obj_t* style_hint_ = nullptr;
    int screensaver_minutes_ = 1;
    ScreensaverStyle screensaver_style_ = ScreensaverStyle::Dashboard;
#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI
    std::array<RouteAction, 5> routes_{};
#else
    std::array<RouteAction, 4> routes_{};
#endif
    LevelSlider volume_;
    LevelSlider brightness_;
    std::array<TimeoutAction, 4> timeouts_{};
    std::array<lv_obj_t*, 4> timeout_buttons_{};
    std::array<StyleAction, 2> styles_{};
    std::array<lv_obj_t*, 2> style_buttons_{};
};

#endif
