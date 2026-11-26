#include "speaker_background.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"
#include <cstring>
#include <driver/jpeg_decode.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <src/misc/cache/lv_cache.h>

namespace {
constexpr char kTag[] = "SpeakerBackground";
constexpr uint32_t kStride = SpeakerUi::kWidth * 2;
constexpr uint32_t kImageBytes = kStride * SpeakerUi::kHeight;
static_assert(SpeakerUi::kWidth % 16 == 0 && SpeakerUi::kHeight % 16 == 0);

extern const uint8_t home_start[] asm("_binary_speaker_home_jpg_start");
extern const uint8_t home_end[] asm("_binary_speaker_home_jpg_end");
extern const uint8_t boot_start[] asm("_binary_speaker_boot_jpg_start");
extern const uint8_t boot_end[] asm("_binary_speaker_boot_jpg_end");

struct EmbeddedImage {
    const uint8_t* start;
    const uint8_t* end;
};

constexpr EmbeddedImage Asset(SpeakerBackground::Artwork artwork) {
    using Artwork = SpeakerBackground::Artwork;
    switch (artwork) {
        case Artwork::Home: return {home_start, home_end};
        case Artwork::Boot: return {boot_start, boot_end};
    }
    return {};
}

std::unique_ptr<LvglAllocatedImage> Decode(EmbeddedImage asset) {
    if (!asset.start) {
        return nullptr;
    }
    const auto length = static_cast<uint32_t>(asset.end - asset.start);
    jpeg_decode_picture_info_t header{};
    if (jpeg_decoder_get_info(asset.start, length, &header) != ESP_OK ||
        header.width != SpeakerUi::kWidth || header.height != SpeakerUi::kHeight ||
        header.sample_method != JPEG_DOWN_SAMPLING_YUV420) {
        return nullptr;
    }

    const jpeg_decode_memory_alloc_cfg_t input_config{.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    const jpeg_decode_memory_alloc_cfg_t output_config{.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    size_t input_capacity = 0;
    size_t output_capacity = 0;
    using Buffer = std::unique_ptr<uint8_t, decltype(&heap_caps_free)>;
    Buffer input(static_cast<uint8_t*>(jpeg_alloc_decoder_mem(length, &input_config, &input_capacity)), heap_caps_free);
    Buffer pixels(static_cast<uint8_t*>(jpeg_alloc_decoder_mem(kImageBytes, &output_config, &output_capacity)), heap_caps_free);
    if (!input || !pixels || input_capacity < length || output_capacity < kImageBytes) {
        return nullptr;
    }
    std::memcpy(input.get(), asset.start, length);

    const jpeg_decode_engine_cfg_t engine_config{.timeout_ms = 250};
    const jpeg_decode_cfg_t decode_config{
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    jpeg_decoder_handle_t engine = nullptr;
    if (jpeg_new_decoder_engine(&engine_config, &engine) != ESP_OK) {
        return nullptr;
    }
    uint32_t decoded_size = 0;
    const auto result = jpeg_decoder_process(engine, &decode_config, input.get(), length, pixels.get(),
                                             output_capacity, &decoded_size);
    jpeg_del_decoder_engine(engine);
    if (result != ESP_OK || decoded_size != kImageBytes) {
        return nullptr;
    }
    return std::make_unique<LvglAllocatedImage>(pixels.release(), decoded_size, SpeakerUi::kWidth,
                                                 SpeakerUi::kHeight, kStride, LV_COLOR_FORMAT_RGB565);
}
}

void SpeakerBackground::Load(lv_obj_t* panel, Artwork artwork) {
    if (!image_) {
        image_ = Decode(Asset(artwork));
        if (!image_) {
            ESP_LOGW(kTag, "Could not load background %d", static_cast<int>(artwork));
            return;
        }
    }
    lv_obj_set_style_bg_image_src(panel, image_->image_dsc(), LV_PART_MAIN);
}

void SpeakerBackground::Detach(lv_obj_t* panel) const {
    if (image_ && lv_obj_get_style_bg_image_src(panel, LV_PART_MAIN) == image_->image_dsc()) {
        lv_obj_set_style_bg_image_src(panel, nullptr, LV_PART_MAIN);
    }
}

void SpeakerBackground::Release(lv_obj_t* panel) {
    if (image_) {
        Detach(panel);
        lv_image_cache_drop(image_->image_dsc());
        image_.reset();
    }
}

#endif
