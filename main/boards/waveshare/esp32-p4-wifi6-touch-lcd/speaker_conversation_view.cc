#include "speaker_conversation_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "hncig_logo_i2.h"
#include "speaker_home_widgets.h"

using namespace SpeakerHomeWidgets;

namespace {
void AnimateLogo(void* object, int32_t scale) {
    auto* logo = static_cast<lv_obj_t*>(object);
    lv_obj_set_style_transform_scale(logo, scale, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(logo, (scale - 256) * 2, LV_PART_MAIN);
}
}  // namespace

void SpeakerConversationView::Create(lv_obj_t* parent) {
    root_ = Panel(parent);
    auto* top = Card(root_, 0, 0, 800, 52);
    lv_obj_set_style_radius(top, 0, LV_PART_MAIN);
    lv_obj_set_pos(Label(top, "筑音", kText), 18, 9);
    status_ = Label(top, "", kCyan);
    lv_obj_set_width(status_, 350);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(status_, LV_ALIGN_CENTER, 0, 0);
    auto* exit = Button(top, "结束对话", [](lv_event_t*) {
        Application::GetInstance().EndSpeakerConversation();
    }, nullptr);
    lv_obj_set_size(exit, 150, 40);
    lv_obj_set_pos(exit, 626, 5);

    for (int size : {244, 202, 144}) {
        auto* ring = Card(root_, (800 - size) / 2, 218 - size / 2, size, size);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_color(ring, lv_color_hex(size == 202 ? 0x6C51C8 : kCyan), LV_PART_MAIN);
        lv_obj_set_style_border_opa(ring, LV_OPA_60, LV_PART_MAIN);
    }
    logo_ = lv_image_create(root_);
    lv_image_set_src(logo_, &hncig_logo_i2);
    lv_obj_align(logo_, LV_ALIGN_CENTER, 0, -22);
    auto* brand = Label(root_, "中湘元筑未来", kCyan);
    lv_obj_align(brand, LV_ALIGN_CENTER, 0, 129);
    auto* sub_bar = Card(root_, 0, 400, 800, 80);
    lv_obj_set_style_radius(sub_bar, 0, LV_PART_MAIN);
    subtitle_ = Label(sub_bar, "", kText);
    lv_obj_set_width(subtitle_, 746);
    lv_obj_set_style_text_align(subtitle_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(subtitle_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_center(subtitle_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerConversationView::Destroy() {
    if (!root_) return;
    SetAnimating(false);
    lv_obj_delete(root_);
    root_ = nullptr;
}

void SpeakerConversationView::Update(bool visible, const char* status, const char* subtitle,
                                     const lv_font_t* font, bool speaking) {
    if (!root_) return;
    const bool was_visible = !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (was_visible != visible) {
        lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !visible);
    }
    SetAnimating(visible && speaking);
    if (!visible) return;
    if (font && font_ != font) {
        lv_obj_set_style_text_font(root_, font, LV_PART_MAIN);
        font_ = font;
    }
    UpdateLabel(status_, status);
    UpdateLabel(subtitle_, subtitle);
}

void SpeakerConversationView::SetAnimating(bool animate) {
    if (animate == animating_) return;
    animating_ = animate;
    if (!animate) {
        lv_anim_delete(logo_, AnimateLogo);
        AnimateLogo(logo_, 256);
        return;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, logo_);
    lv_anim_set_exec_cb(&animation, AnimateLogo);
    lv_anim_set_values(&animation, 248, 267);
    lv_anim_set_duration(&animation, 1250);
    lv_anim_set_reverse_duration(&animation, 1250);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

#endif
