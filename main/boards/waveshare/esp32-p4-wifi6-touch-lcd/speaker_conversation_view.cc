#include "speaker_conversation_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

void SpeakerConversationView::Create(lv_obj_t* parent, void (*on_exit)(void*), void* context) {
    root_ = Panel(parent);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x070A15), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_NONE, LV_PART_MAIN);

    message_ = Label(root_, "");
    lv_obj_set_width(message_, 378);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(message_, 6, LV_PART_MAIN);
    lv_obj_set_style_max_height(message_, 104, LV_PART_MAIN);
    Scale(message_, 470);
    lv_obj_align(message_, LV_ALIGN_CENTER, 0, -46);

    wave_.Create(root_, 346, 134, SpeakerWave::Shape::Ribbon);
    exit_.Create(root_, on_exit, context);
    exit_.SetVisible(true);
}

void SpeakerConversationView::Update(bool visible, SpeakerWave::Motion motion, const char* status,
                                     const char* subtitle, const lv_font_t* font) {
    lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !visible);
    wave_.SetMotion(visible ? motion : SpeakerWave::Motion::Still);
    if (!visible) {
        return;
    }
    if (font && lv_obj_get_style_text_font(message_, LV_PART_MAIN) != font) {
        lv_obj_set_style_text_font(message_, font, LV_PART_MAIN);
    }
    UpdateLabel(message_, *subtitle ? subtitle : status);
}

void SpeakerConversationView::Stop() {
    wave_.Stop();
}

#endif
