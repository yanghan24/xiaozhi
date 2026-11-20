#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <mutex>
#include <string>
#include <vector>
#include <driver/sdspi_host.h>
#include <sdmmc_cmd.h>
#include <sd_pwr_ctrl.h>

class SpeakerSdStorage {
public:
    static constexpr const char* Root() { return "/sdcard"; }
    static constexpr const char* Recordings() { return "/sdcard/recordings"; }

    static SpeakerSdStorage& Get();
    bool Mount();
    std::vector<std::string> ListAudio() const;

private:
    std::mutex mutex_;
    sdmmc_card_t* card_ = nullptr;
    sd_pwr_ctrl_handle_t power_ = nullptr;
    bool bus_ready_ = false;
};

#endif
