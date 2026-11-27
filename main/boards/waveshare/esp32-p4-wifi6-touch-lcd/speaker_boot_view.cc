#include "speaker_boot_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_boot_brand_i2.h"
#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

namespace {
constexpr int kBrandStartY = -43;
constexpr int kBrandRise = 12;
}

void SpeakerBootView::Create(lv_obj_t* parent, SpeakerBackground& background) {
    root_ = Panel(parent);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    background.Load(root_, SpeakerBackground::Artwork::Boot);
    artwork_ = Surface(root_, 0, 0, kWidth, kHeight);
    brand_ = Surface(artwork_, 0, 0, SPEAKER_BOOT_BRAND_WIDTH, SPEAKER_BOOT_BRAND_HEIGHT);
    lv_obj_align(brand_, LV_ALIGN_CENTER, 0, kBrandStartY);
    auto* emblem = lv_image_create(brand_);
    lv_image_set_src(emblem, &speaker_boot_emblem_i2);
    lv_obj_set_pos(emblem, SPEAKER_BOOT_EMBLEM_X, SPEAKER_BOOT_EMBLEM_Y);
    auto* wordmark = lv_image_create(brand_);
    lv_image_set_src(wordmark, &speaker_boot_wordmark_i2);
    lv_obj_set_pos(wordmark, SPEAKER_BOOT_WORDMARK_X, SPEAKER_BOOT_WORDMARK_Y);
    auto* title = lv_image_create(artwork_);
    lv_image_set_src(title, &speaker_boot_title_i2);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 72);
    lv_obj_set_style_opa(artwork_, LV_OPA_TRANSP, LV_PART_MAIN);
    started_ = lv_tick_get();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

bool SpeakerBootView::Visible() const {
    return root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerBootView::Update(bool interrupted, SpeakerBackground& background) {
    if (!Visible()) {
        return;
    }
    const uint32_t elapsed = lv_tick_elaps(started_);
    if (interrupted || elapsed >= 1500) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        background.Detach(root_);
        return;
    }
    const uint32_t reveal = elapsed < 760 ? elapsed : 760;
    const auto opacity = static_cast<lv_opa_t>(reveal * LV_OPA_COVER / 760);
    lv_obj_set_style_opa(artwork_, opacity, LV_PART_MAIN);
    lv_obj_set_y(brand_, kBrandStartY - reveal * kBrandRise / 760);
}

void SpeakerBootView::SetSystemBrandVisible(lv_obj_t* parent, bool visible) {
    if (!artwork_ || Visible()) {
        return;
    }
    if (visible && lv_obj_get_parent(artwork_) != parent) {
        // Put the artwork on the system background, beneath the status and message bars.
        lv_obj_set_parent(artwork_, parent);
        lv_obj_set_pos(artwork_, 0, 0);
        lv_obj_set_y(brand_, kBrandStartY - kBrandRise);
        lv_obj_set_style_opa(artwork_, LV_OPA_COVER, LV_PART_MAIN);
    }
    lv_obj_set_flag(artwork_, LV_OBJ_FLAG_HIDDEN, !visible);
}

#endif
