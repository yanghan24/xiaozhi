#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"
#include "speaker_wave.h"

class SpeakerConversationView {
public:
    void Create(lv_obj_t* parent, void (*on_exit)(void*), void* context);
    void Stop();
    bool IsAnimating() const { return wave_.IsAnimating(); }
    void Update(bool visible, SpeakerWave::Motion motion, const char* status,
                const char* subtitle, const lv_font_t* font);

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* message_ = nullptr;
    SpeakerWave wave_;
    SpeakerEdgeGesture exit_;
};

#endif
