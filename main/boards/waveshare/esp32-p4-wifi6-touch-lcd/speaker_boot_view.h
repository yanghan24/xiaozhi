#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <array>
#include <cstdlib>
#include <memory>

#include "speaker_background.h"

class SpeakerBootView {
public:
    void Create(lv_obj_t* parent, SpeakerBackground& background);
    void Destroy();
    bool Visible() const;
    void Update(bool interrupted, SpeakerBackground& background);
    void SetSystemBrandVisible(lv_obj_t* parent, bool visible);

private:
    void LoadArtwork();
    void ReleaseArtwork();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* artwork_ = nullptr;
    lv_obj_t* brand_ = nullptr;
    std::array<lv_obj_t*, 3> image_objects_{};
    std::array<lv_image_dsc_t, 3> images_{};
    std::unique_ptr<uint8_t, decltype(&std::free)> pixels_{nullptr, &std::free};
    uint32_t started_ = 0;
};

#endif
