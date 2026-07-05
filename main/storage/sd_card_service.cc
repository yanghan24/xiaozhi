#include "sd_card_service.h"

#include <esp_log.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utility>

#define TAG "SdCardService"

namespace {
constexpr const char* kMusicDirectory = SDCARD_MOUNT_POINT "/music";
constexpr const char* kRecordingsDirectory = SDCARD_MOUNT_POINT "/recordings";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string GetExtension(const std::string& file_name) {
    auto dot = file_name.find_last_of('.');
    if (dot == std::string::npos) {
        return "";
    }
    return file_name.substr(dot);
}

bool IsSupportedMediaExtension(const std::string& extension) {
    auto lower = ToLower(extension);
    return lower == ".mp3" || lower == ".wav";
}

std::string GetMediaType(const std::string& extension) {
    auto lower = ToLower(extension);
    if (lower == ".mp3") {
        return "mp3";
    }
    if (lower == ".wav") {
        return "wav";
    }
    return "";
}

esp_err_t EnsureDirectory(const char* path) {
    struct stat st = {};
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "%s exists but is not a directory", path);
        return ESP_ERR_INVALID_STATE;
    }

    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Failed to create %s: errno=%d (%s)",
        path, errno, strerror(errno));
    return ESP_FAIL;
}
} // namespace

SdCardService& SdCardService::GetInstance() {
    static SdCardService instance;
    return instance;
}

bool SdCardService::IsMounted() const {
    struct stat st = {};
    return stat(SDCARD_MOUNT_POINT, &st) == 0 && S_ISDIR(st.st_mode);
}

esp_err_t SdCardService::EnsureMediaDirectories() {
    if (!IsMounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = EnsureDirectory(kMusicDirectory);
    if (ret != ESP_OK) {
        return ret;
    }
    return EnsureDirectory(kRecordingsDirectory);
}

std::vector<MediaFile> SdCardService::ScanMediaFiles(size_t max_items) {
    std::vector<MediaFile> files;
    if (max_items == 0 || !IsMounted()) {
        return files;
    }

    EnsureMediaDirectories();
    ScanDirectory(kMusicDirectory, files, max_items);
    ScanDirectory(kRecordingsDirectory, files, max_items);

    std::sort(files.begin(), files.end(), [](const MediaFile& a, const MediaFile& b) {
        if (a.modified_time != b.modified_time) {
            return a.modified_time > b.modified_time;
        }
        return a.path < b.path;
    });

    if (files.size() > max_items) {
        files.resize(max_items);
    }
    return files;
}

uint64_t SdCardService::GetFreeBytes() const {
    struct statvfs fs = {};
    if (!IsMounted() || statvfs(SDCARD_MOUNT_POINT, &fs) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(fs.f_bavail) * static_cast<uint64_t>(fs.f_frsize);
}

void SdCardService::ScanDirectory(const std::string& directory,
        std::vector<MediaFile>& files, size_t max_items) const {
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        ESP_LOGW(TAG, "Failed to open %s: errno=%d (%s)",
            directory.c_str(), errno, strerror(errno));
        return;
    }

    size_t initial_size = files.size();
    while (files.size() - initial_size < max_items) {
        dirent* entry = readdir(dir);
        if (entry == nullptr) {
            break;
        }

        std::string file_name(entry->d_name);
        if (file_name == "." || file_name == "..") {
            continue;
        }

        auto extension = GetExtension(file_name);
        if (!IsSupportedMediaExtension(extension)) {
            continue;
        }

        std::string path = directory + "/" + file_name;
        struct stat st = {};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        MediaFile file;
        file.path = path;
        file.display_name = file_name;
        file.extension = extension;
        file.type = GetMediaType(extension);
        file.size_bytes = static_cast<uint64_t>(st.st_size);
        file.modified_time = st.st_mtime;
        files.push_back(std::move(file));
    }

    closedir(dir);
}
