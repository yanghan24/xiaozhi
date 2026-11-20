#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_wave.h"

class SpeakerScreensaverView {
public:
    void Create(lv_obj_t* parent, lv_event_cb_t on_exit, void* context);
    void SetVisible(bool visible);
    bool Visible() const;
    void Update(const char* time, const char* date_weather, const char* wifi);

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* date_weather_ = nullptr;
    lv_obj_t* wifi_ = nullptr;
    SpeakerWave wave_;
};

#endif
