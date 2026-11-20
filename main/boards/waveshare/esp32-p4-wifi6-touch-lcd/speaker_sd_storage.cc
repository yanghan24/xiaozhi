#include "speaker_sd_storage.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_vfs_fat.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#include <sys/stat.h>

namespace {
constexpr auto kClock = GPIO_NUM_43;
constexpr auto kMosi = GPIO_NUM_44;
constexpr auto kMiso = GPIO_NUM_39;
constexpr auto kSelect = GPIO_NUM_42;
constexpr size_t kMaxFiles = 100;

void Scan(const std::string& directory, unsigned remaining_depth, std::vector<std::string>& files) {
    auto* entries = opendir(directory.c_str());
    if (!entries) return;
    while (files.size() < kMaxFiles) {
        auto* entry = readdir(entries);
        if (!entry) break;
        if (entry->d_name[0] == '.') continue;
        const auto path = directory + "/" + entry->d_name;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode) && remaining_depth) {
            Scan(path, remaining_depth - 1, files);
        } else if (S_ISREG(info.st_mode)) {
            if (path.size() < 4) continue;
            auto extension = path.substr(path.size() - 4);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (extension == ".mp3" || extension == ".wav") files.push_back(path);
        }
    }
    closedir(entries);
}
}  // namespace

SpeakerSdStorage& SpeakerSdStorage::Get() {
    static SpeakerSdStorage storage;
    return storage;
}

bool SpeakerSdStorage::Mount() {
    std::lock_guard lock(mutex_);
    if (card_) return true;

    if (!power_) {
        sd_pwr_ctrl_ldo_config_t config{.ldo_chan_id = 4};
        if (sd_pwr_ctrl_new_on_chip_ldo(&config, &power_) != ESP_OK) return false;
    }

    // The C6 Wi-Fi link owns SDMMC on this board. The card uses its SPI wiring.
    if (!bus_ready_) {
        for (const auto pin : {kMiso, GPIO_NUM_40, GPIO_NUM_41, kSelect, kClock, kMosi}) {
            gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
        }
        spi_bus_config_t bus{};
        bus.mosi_io_num = kMosi;
        bus.miso_io_num = kMiso;
        bus.sclk_io_num = kClock;
        bus.quadwp_io_num = GPIO_NUM_NC;
        bus.quadhd_io_num = GPIO_NUM_NC;
        bus.max_transfer_sz = 16 * 1024;
        if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
        bus_ready_ = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    host.pwr_ctrl_handle = power_;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = kSelect;
    esp_vfs_fat_sdmmc_mount_config_t config{};
    config.max_files = 6;
    config.disk_status_check_enable = true;
    if (esp_vfs_fat_sdspi_mount(Root(), &host, &slot, &config, &card_) != ESP_OK) {
        card_ = nullptr;
        return false;
    }
    mkdir(Recordings(), 0775);
    return true;
}

std::vector<std::string> SpeakerSdStorage::ListAudio() const {
    std::vector<std::string> files;
    Scan(Root(), 2, files);
    std::sort(files.begin(), files.end());
    return files;
}

#endif
