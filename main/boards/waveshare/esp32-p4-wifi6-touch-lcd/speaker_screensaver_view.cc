#include "speaker_screensaver_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

void SpeakerScreensaverView::Create(lv_obj_t* parent, lv_event_cb_t on_exit, void* context) {
    root_ = Panel(parent);
    lv_obj_add_flag(root_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(root_, on_exit, LV_EVENT_CLICKED, context);
    wave_.Create(root_, 350, 130, SpeakerWave::Shape::Sweep);
    wifi_ = Wifi(root_);
    clock_ = Digits(root_, "--:--", 316, 900);
    lv_obj_align(clock_, LV_ALIGN_CENTER, 0, -60);
    date_weather_ = Label(root_, "正在校时", kMuted);
    lv_obj_set_width(date_weather_, 720);
    lv_obj_set_style_text_align(date_weather_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(date_weather_, LV_LABEL_LONG_DOT);
    lv_obj_align(date_weather_, LV_ALIGN_CENTER, 0, 68);
}

void SpeakerScreensaverView::SetVisible(bool visible) {
    lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !visible);
}

bool SpeakerScreensaverView::Visible() const {
    return !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerScreensaverView::Update(const char* time, const char* date_weather, const char* wifi) {
    UpdateLabel(clock_, time);
    UpdateLabel(date_weather_, date_weather);
    UpdateLabel(wifi_, wifi);
}

#endif
