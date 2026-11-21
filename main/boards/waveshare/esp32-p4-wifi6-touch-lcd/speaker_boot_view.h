#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_background.h"

class SpeakerBootView {
public:
    void Create(lv_obj_t* parent, SpeakerBackground& background);
    bool Visible() const;
    void Update(bool interrupted);

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* logo_ = nullptr;
    lv_obj_t* title_ = nullptr;
    uint32_t started_ = 0;
};

#endif
