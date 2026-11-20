#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"
#include "speaker_wave.h"

class SpeakerLauncherView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void Show();
    void Hide();
    void Update(const char* time, const char* wifi);

private:
    struct Entry {
        const char* title;
        const char* glyph;
        uint32_t tint;
        SpeakerNavigation::Page page;
    };

    struct LaunchAction {
        SpeakerNavigation* navigation = nullptr;
        SpeakerNavigation::Page page = SpeakerNavigation::Page::Home;
    };

    std::array<LaunchAction, 5> actions_{};
    lv_obj_t* root_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* wifi_ = nullptr;
    SpeakerWave wave_;
};

#endif
