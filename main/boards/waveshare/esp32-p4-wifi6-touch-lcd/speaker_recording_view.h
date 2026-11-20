#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"

class SpeakerRecordingView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void Show();
    void Hide();
    void Update();
    bool CanLeave() const;

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* back_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* file_ = nullptr;
    lv_obj_t* start_ = nullptr;
    lv_obj_t* stop_ = nullptr;
    lv_obj_t* elapsed_ = nullptr;
    std::array<lv_obj_t*, 22> bars_{};
};

#endif
