#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <memory>
#include "lvgl_image.h"

class SpeakerBackground {
public:
    enum class Artwork { Home, Screensaver, Boot };

    void Load(lv_obj_t* panel, Artwork artwork);
    void Detach(lv_obj_t* panel) const;
    void Release(lv_obj_t* panel);

private:
    std::unique_ptr<LvglAllocatedImage> image_;
};

#endif
