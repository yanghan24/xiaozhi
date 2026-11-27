#ifndef SPEAKER_BOOT_BRAND_I2_H
#define SPEAKER_BOOT_BRAND_I2_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPEAKER_BOOT_BRAND_WIDTH = 640,
    SPEAKER_BOOT_BRAND_HEIGHT = 102,
    SPEAKER_BOOT_EMBLEM_X = 0,
    SPEAKER_BOOT_EMBLEM_Y = 0,
    SPEAKER_BOOT_WORDMARK_X = 141,
    SPEAKER_BOOT_WORDMARK_Y = 12,
};

typedef struct {
    lv_image_header_t header;
    const uint8_t* packed;
    size_t packed_size;
} speaker_boot_brand_asset_t;

extern const speaker_boot_brand_asset_t speaker_boot_emblem_i2;
extern const speaker_boot_brand_asset_t speaker_boot_wordmark_i2;
extern const speaker_boot_brand_asset_t speaker_boot_title_i2;

bool speaker_boot_brand_unpack(const speaker_boot_brand_asset_t* asset, uint8_t* output, size_t size);

#ifdef __cplusplus
}
#endif

#endif  // SPEAKER_BOOT_BRAND_I2_H
