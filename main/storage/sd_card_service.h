#ifndef SD_CARD_SERVICE_H
#define SD_CARD_SERVICE_H

#include <esp_err.h>

#include <ctime>
#include <cstdint>
#include <string>
#include <vector>

#ifndef SDCARD_MOUNT_POINT
#define SDCARD_MOUNT_POINT "/sdcard"
#endif

struct MediaFile {
    std::string path;
    std::string display_name;
    std::string extension;
    std::string type;
    uint64_t size_bytes = 0;
    time_t modified_time = 0;
};

class SdCardService {
public:
    static SdCardService& GetInstance();

    bool IsMounted() const;
    esp_err_t EnsureMediaDirectories();
    std::vector<MediaFile> ScanMediaFiles(size_t max_items = 200);
    uint64_t GetFreeBytes() const;

private:
    SdCardService() = default;
    SdCardService(const SdCardService&) = delete;
    SdCardService& operator=(const SdCardService&) = delete;

    void ScanDirectory(const std::string& directory, std::vector<MediaFile>& files, size_t max_items) const;
};

#endif // SD_CARD_SERVICE_H
