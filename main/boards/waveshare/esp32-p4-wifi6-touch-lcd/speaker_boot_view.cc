#include "speaker_boot_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_boot_brand_i2.h"
#include "speaker_account/speaker_ui_widgets.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <src/misc/cache/lv_cache.h>

using namespace SpeakerUi;

namespace {
constexpr int kBrandStartY = -43;
constexpr int kBrandRise = 12;
constexpr const char* kTag = "SpeakerBootView";
constexpr const speaker_boot_brand_asset_t* kAssets[] = {
    &speaker_boot_emblem_i2, &speaker_boot_wordmark_i2, &speaker_boot_title_i2,
};

size_t ImageSize(const lv_image_header_t& header) {
    constexpr size_t kPaletteSize = 4 * sizeof(lv_color32_t);
    return kPaletteSize + static_cast<size_t>(header.stride) * header.h;
}

size_t AlignedSize(size_t size) {
    return (size + 3U) & ~size_t{3};
}
}

void SpeakerBootView::Create(lv_obj_t* parent, SpeakerBackground& background) {
    root_ = Panel(parent);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    background.Load(root_, SpeakerBackground::Artwork::Boot);
    artwork_ = Surface(root_, 0, 0, kWidth, kHeight);
    brand_ = Surface(artwork_, 0, 0, SPEAKER_BOOT_BRAND_WIDTH, SPEAKER_BOOT_BRAND_HEIGHT);
    lv_obj_align(brand_, LV_ALIGN_CENTER, 0, kBrandStartY);
    image_objects_[0] = lv_image_create(brand_);
    lv_obj_set_pos(image_objects_[0], SPEAKER_BOOT_EMBLEM_X, SPEAKER_BOOT_EMBLEM_Y);
    image_objects_[1] = lv_image_create(brand_);
    lv_obj_set_pos(image_objects_[1], SPEAKER_BOOT_WORDMARK_X, SPEAKER_BOOT_WORDMARK_Y);
    image_objects_[2] = lv_image_create(artwork_);
    LoadArtwork();
    lv_obj_align(image_objects_[2], LV_ALIGN_CENTER, 0, 72);
    lv_obj_set_style_opa(artwork_, LV_OPA_TRANSP, LV_PART_MAIN);
    started_ = lv_tick_get();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerBootView::Destroy() {
    if (root_) {
        ReleaseArtwork();
        lv_obj_delete(artwork_);
        lv_obj_delete(root_);
        artwork_ = nullptr;
        root_ = nullptr;
    }
}

void SpeakerBootView::LoadArtwork() {
    if (pixels_) {
        return;
    }
    size_t total = 0;
    for (const auto* asset : kAssets) {
        total += AlignedSize(ImageSize(asset->header));
    }
    pixels_.reset(static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (!pixels_) {
        ESP_LOGW(kTag, "Could not allocate boot artwork");
        return;
    }

    auto* next = pixels_.get();
    for (size_t index = 0; index < images_.size(); ++index) {
        const auto& asset = *kAssets[index];
        const size_t size = ImageSize(asset.header);
        if (!speaker_boot_brand_unpack(&asset, next, size)) {
            ESP_LOGW(kTag, "Could not unpack boot artwork");
            pixels_.reset();
            return;
        }
        auto& image = images_[index];
        image.header = asset.header;
        image.data_size = size;
        image.data = next;
        next += AlignedSize(size);
    }
    for (size_t index = 0; index < images_.size(); ++index) {
        lv_image_set_src(image_objects_[index], &images_[index]);
    }
}

void SpeakerBootView::ReleaseArtwork() {
    if (!pixels_) {
        return;
    }
    for (size_t index = 0; index < images_.size(); ++index) {
        lv_image_set_src(image_objects_[index], nullptr);
        lv_image_cache_drop(&images_[index]);
        images_[index].data = nullptr;
    }
    pixels_.reset();
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
    if (visible && lv_obj_has_flag(artwork_, LV_OBJ_FLAG_HIDDEN)) {
        LoadArtwork();
    }
    lv_obj_set_flag(artwork_, LV_OBJ_FLAG_HIDDEN, !visible);
    if (!visible) {
        ReleaseArtwork();
    }
}

#endif
