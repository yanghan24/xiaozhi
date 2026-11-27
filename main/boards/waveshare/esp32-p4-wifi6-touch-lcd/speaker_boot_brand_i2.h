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

extern const lv_image_dsc_t speaker_boot_emblem_i2;
extern const lv_image_dsc_t speaker_boot_wordmark_i2;

#ifdef __cplusplus
}
#endif

#endif  // SPEAKER_BOOT_BRAND_I2_H
