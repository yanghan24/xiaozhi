#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_home_widgets.h"

class SpeakerRecordingView {
public:
    void Create(lv_obj_t* parent);
    void Show();
    void Hide();
    void Update();

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* back_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* file_ = nullptr;
    lv_obj_t* start_ = nullptr;
    lv_obj_t* stop_ = nullptr;
    SpeakerHomeWidgets::DigitalClock elapsed_;
};

#endif
