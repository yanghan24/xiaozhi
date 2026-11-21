#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_navigation.h"
#include "speaker_wave.h"

#include <string>

class SpeakerConversationView {
public:
    void Create(lv_obj_t* parent, void (*on_exit)(void*), void* context);
    void Stop();
    void OnNewSubtitle() { new_subtitle_ = true; }
    bool IsAnimating() const { return wave_.IsAnimating(); }
    void Update(bool visible, SpeakerWave::Motion motion, const char* status,
                const char* subtitle, const lv_font_t* font, bool spoken_subtitle);

private:
    void StopSubtitleScroll();
    void StepSubtitleScroll();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* subtitle_window_ = nullptr;
    lv_obj_t* scrolling_subtitle_ = nullptr;
    lv_timer_t* subtitle_timer_ = nullptr;
    const lv_font_t* message_font_ = nullptr;
    std::string displayed_text_;
    uint32_t scroll_start_ = 0;
    uint32_t scroll_duration_ = 0;
    int32_t scroll_distance_ = 0;
    bool spoken_subtitle_ = false;
    bool new_subtitle_ = false;
    SpeakerWave wave_;
    SpeakerEdgeGesture exit_;
};

#endif
