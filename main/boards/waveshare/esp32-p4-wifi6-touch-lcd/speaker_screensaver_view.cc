#include "speaker_screensaver_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"

#if LV_USE_FONT_COMPRESSED
LV_FONT_DECLARE(speaker_clock_font);
#endif

using namespace SpeakerUi;

void SpeakerScreensaverView::Create(lv_obj_t* parent, lv_event_cb_t on_exit, void* context) {
    root_ = Panel(parent);
    lv_obj_add_flag(root_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(root_, on_exit, LV_EVENT_CLICKED, context);
    wifi_ = Wifi(root_);
#if LV_USE_FONT_COMPRESSED
    clock_ = Label(root_, "--:--");
    lv_obj_set_style_text_font(clock_, &speaker_clock_font, LV_PART_MAIN);
    lv_obj_set_width(clock_, 720);
    lv_obj_set_style_text_align(clock_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
#else
    clock_ = Digits(root_, "--:--", 316, 900);
#endif
    lv_obj_align(clock_, LV_ALIGN_CENTER, 0, -60);
    date_weather_ = Label(root_, "正在校时", kMuted);
    lv_obj_set_width(date_weather_, 720);
    lv_obj_set_style_text_align(date_weather_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(date_weather_, LV_LABEL_LONG_DOT);
    lv_obj_align(date_weather_, LV_ALIGN_CENTER, 0, 68);
}

void SpeakerScreensaverView::SetVisible(bool visible) {
    if (visible) {
        background_.Load(root_, SpeakerBackground::Artwork::Screensaver);
    }
    lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !visible);
    if (!visible) {
        background_.Release(root_);
    }
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
