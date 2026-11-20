#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_music.h"
#include <lvgl.h>

class SpeakerMusicView {
public:
    void Create(lv_obj_t* parent);
    void Show();
    void Hide();
    void Update();

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* song_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* toggle_label_ = nullptr;
    std::shared_ptr<const SpeakerMusic::Tracks> displayed_tracks_;
};

#endif
