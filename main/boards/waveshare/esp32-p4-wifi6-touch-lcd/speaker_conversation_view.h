#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <lvgl.h>

class SpeakerConversationView {
public:
    void Create(lv_obj_t* parent);
    void Destroy();
    void Update(bool visible, const char* status, const char* subtitle, const lv_font_t* font, bool speaking);

private:
    void SetAnimating(bool animate);
    lv_obj_t* root_ = nullptr;
    lv_obj_t* logo_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    const lv_font_t* font_ = nullptr;
    bool animating_ = false;
};

#endif
