#include "speaker_boot_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "hncig_logo_i2.h"
#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

void SpeakerBootView::Create(lv_obj_t* parent, SpeakerBackground& background) {
    root_ = Panel(parent);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    background.Load(root_, SpeakerBackground::Artwork::Boot);
    logo_ = lv_image_create(root_);
    lv_image_set_src(logo_, &hncig_logo_i2);
    lv_obj_align(logo_, LV_ALIGN_CENTER, 0, -43);
    title_ = Label(root_, "筑音");
    Scale(title_, 340);
    lv_obj_align(title_, LV_ALIGN_CENTER, 0, 72);
    lv_obj_set_style_opa(logo_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(title_, LV_OPA_TRANSP, LV_PART_MAIN);
    started_ = lv_tick_get();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

bool SpeakerBootView::Visible() const {
    return root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerBootView::Update(bool interrupted) {
    if (!Visible()) {
        return;
    }
    const uint32_t elapsed = lv_tick_elaps(started_);
    if (interrupted || elapsed >= 1500) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const uint32_t reveal = elapsed < 760 ? elapsed : 760;
    const auto opacity = static_cast<lv_opa_t>(reveal * LV_OPA_COVER / 760);
    lv_obj_set_style_opa(logo_, opacity, LV_PART_MAIN);
    lv_obj_set_style_opa(title_, opacity, LV_PART_MAIN);
    lv_obj_set_y(logo_, -43 - reveal * 12 / 760);
}

#endif
