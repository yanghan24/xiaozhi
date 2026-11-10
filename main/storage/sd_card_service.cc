#include "sd_card_service.h"

#include "sdkconfig.h"

#include <esp_log.h>
#include <esp_vfs_fat.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#define TAG "SdCardService"

#if (defined(CONFIG_BOARD_TYPE_LICHUANG_DEV_S3) ||                         \
     defined(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3)) && \
    defined(CONFIG_FATFS_LFN_NONE)
#error \
    "SD media paths require FatFS long filename support. Enable CONFIG_FATFS_LFN_HEAP and CONFIG_FATFS_MAX_LFN=255."
#endif

#if (defined(CONFIG_BOARD_TYPE_LICHUANG_DEV_S3) ||                         \
     defined(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3)) && \
    !defined(CONFIG_FATFS_LFN_HEAP) && !defined(CONFIG_FATFS_LFN_STACK)
#error \
    "SD media paths require FatFS long filename support. Enable CONFIG_FATFS_LFN_HEAP or CONFIG_FATFS_LFN_STACK."
#endif

#if (defined(CONFIG_BOARD_TYPE_LICHUANG_DEV_S3) ||                         \
     defined(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3)) && \
    defined(CONFIG_FATFS_MAX_LFN) && CONFIG_FATFS_MAX_LFN < 32
#error "CONFIG_FATFS_MAX_LFN is too small for SD media file names."
#endif

#if (defined(CONFIG_BOARD_TYPE_LICHUANG_DEV_S3) ||                         \
     defined(CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3)) && \
    !defined(CONFIG_FATFS_API_ENCODING_UTF_8)
#error "SD media scanning requires CONFIG_FATFS_API_ENCODING_UTF_8 for non-ASCII music file names."
#endif

namespace {
constexpr const char* kMusicDirectory = SDCARD_MOUNT_POINT "/music";
constexpr const char* kRecordingsDirectory = SDCARD_MOUNT_POINT "/recordings";
constexpr int kMaxMediaScanDepth = 3;

std::string ToLower(std::string value);

void AddUniquePath(std::vector<std::string>& paths, const std::string& path) {
    if (path.empty()) {
        return;
    }

    // FAT path lookup is case-insensitive. Treat /music, /Music and /MUSIC as
    // the same directory, otherwise the same files are scanned repeatedly.
    const std::string normalized = ToLower(path);
    auto duplicate = std::find_if(
        paths.begin(), paths.end(),
        [&normalized](const std::string& existing) { return ToLower(existing) == normalized; });
    if (duplicate != paths.end()) {
        return;
    }
    paths.push_back(path);
}

bool IsDirectoryPath(const std::string& path) {
    struct stat st = {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

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

std::vector<std::string> FindDirectoriesCaseInsensitive(const std::string& parent,
        const std::string& directory_name) {
    std::vector<std::string> resolved;
    DIR* dir = opendir(parent.c_str());
    if (dir == nullptr) {
        return resolved;
    }

    std::string target = ToLower(directory_name);
    while (true) {
        dirent* entry = readdir(dir);
        if (entry == nullptr) {
            break;
        }
        std::string name(entry->d_name);
        if (name == "." || name == ".." || ToLower(name) != target) {
            continue;
        }

        std::string candidate = parent + "/" + name;
        if (IsDirectoryPath(candidate)) {
            resolved.push_back(candidate);
        }
    }

    closedir(dir);
    return resolved;
}

std::vector<std::string> ResolveMediaDirectoryPaths(const char* preferred_path) {
    std::string preferred(preferred_path);
    std::vector<std::string> resolved;
    if (IsDirectoryPath(preferred)) {
        AddUniquePath(resolved, preferred);
    }

    auto slash = preferred.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return resolved.empty() ? std::vector<std::string>{preferred} : resolved;
    }

    std::string parent = preferred.substr(0, slash);
    std::string name = preferred.substr(slash + 1);
    auto matches = FindDirectoriesCaseInsensitive(parent, name);
    for (const auto& match : matches) {
        AddUniquePath(resolved, match);
    }

    auto lower_name = ToLower(name);
    if (lower_name == "music") {
        const char* variants[] = {"music", "Music", "MUSIC"};
        for (const auto* variant : variants) {
            std::string candidate = parent + "/" + variant;
            if (IsDirectoryPath(candidate)) {
                AddUniquePath(resolved, candidate);
            }
        }
    } else if (lower_name == "recordings") {
        const char* variants[] = {"recordings", "Recordings", "RECORDINGS"};
        for (const auto* variant : variants) {
            std::string candidate = parent + "/" + variant;
            if (IsDirectoryPath(candidate)) {
                AddUniquePath(resolved, candidate);
            }
        }
    }

    return resolved.empty() ? std::vector<std::string>{preferred} : resolved;
}

esp_err_t EspErrorFromErrno(int error) {
    switch (error) {
        case ENOENT:
            return ESP_ERR_NOT_FOUND;
        case ENOMEM:
        case ENOSPC:
            return ESP_ERR_NO_MEM;
        case EINVAL:
            return ESP_ERR_INVALID_ARG;
        case ENOTDIR:
        case EACCES:
        case EROFS:
            return ESP_ERR_INVALID_STATE;
        default:
            return ESP_FAIL;
    }
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
    int stat_errno = errno;
    if (stat_errno != ENOENT) {
        ESP_LOGW(TAG, "Failed to stat %s: errno=%d (%s)",
            path, stat_errno, strerror(stat_errno));
        return EspErrorFromErrno(stat_errno);
    }

    auto resolved = ResolveMediaDirectoryPaths(path);
    if (!resolved.empty() && resolved.front() != path && IsDirectoryPath(resolved.front())) {
        ESP_LOGI(TAG, "Using existing SD media directory: %s", resolved.front().c_str());
        return ESP_OK;
    }

    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        ESP_LOGI(TAG, "Created SD media directory: %s", path);
        return ESP_OK;
    }

    int mkdir_errno = errno;
    ESP_LOGW(TAG, "Failed to create %s: errno=%d (%s)",
        path, mkdir_errno, strerror(mkdir_errno));
    return EspErrorFromErrno(mkdir_errno);
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

esp_err_t SdCardService::EnsureMusicDirectory() {
    if (!IsMounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    return EnsureDirectory(kMusicDirectory);
}

esp_err_t SdCardService::EnsureRecordingsDirectory() {
    if (!IsMounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    return EnsureDirectory(kRecordingsDirectory);
}

esp_err_t SdCardService::EnsureMediaDirectories() {
    esp_err_t ret = EnsureMusicDirectory();
    if (ret != ESP_OK) {
        return ret;
    }
    return EnsureRecordingsDirectory();
}

std::vector<MediaFile> SdCardService::ScanMediaFiles(size_t max_items) {
    std::vector<MediaFile> files;
    if (max_items == 0 || !IsMounted()) {
        return files;
    }

    EnsureMediaDirectories();

    auto scan_resolved_directory = [this, &files, max_items](
            const char* label, const std::string& directory) {
        size_t before = files.size();
        ESP_LOGI(TAG, "Scanning SD %s directory: %s", label, directory.c_str());
        ScanDirectory(directory, files, max_items, 0);
        ESP_LOGI(TAG, "Found %u media file(s) in %s directory: %s",
            static_cast<unsigned>(files.size() - before), label, directory.c_str());
    };

    for (const auto& directory : ResolveMediaDirectoryPaths(kMusicDirectory)) {
        scan_resolved_directory("music", directory);
    }
    for (const auto& directory : ResolveMediaDirectoryPaths(kRecordingsDirectory)) {
        scan_resolved_directory("recordings", directory);
    }

    std::sort(files.begin(), files.end(), [](const MediaFile& a, const MediaFile& b) {
        if (a.modified_time != b.modified_time) {
            return a.modified_time > b.modified_time;
        }
        return a.path < b.path;
    });

    // Keep the public playlist bounded even when both media directories are full.
    if (files.size() > max_items) {
        files.resize(max_items);
    }
    return files;
}

uint64_t SdCardService::GetFreeBytes() const {
    if (!IsMounted()) {
        return 0;
    }

    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t ret = esp_vfs_fat_info(SDCARD_MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to query free space for %s: %s",
            SDCARD_MOUNT_POINT, esp_err_to_name(ret));
        return 0;
    }
    return free_bytes;
}

void SdCardService::ScanDirectory(const std::string& directory,
        std::vector<MediaFile>& files, size_t max_items, int depth) const {
    if (files.size() >= max_items) {
        return;
    }

    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        ESP_LOGW(TAG, "Failed to open %s: errno=%d (%s)",
            directory.c_str(), errno, strerror(errno));
        return;
    }

    while (files.size() < max_items) {
        dirent* entry = readdir(dir);
        if (entry == nullptr) {
            break;
        }

        std::string file_name(entry->d_name);
        if (file_name == "." || file_name == "..") {
            continue;
        }

        std::string path = directory + "/" + file_name;
        struct stat st = {};
        if (stat(path.c_str(), &st) != 0) {
            ESP_LOGD(TAG, "Skipping %s because stat failed: errno=%d (%s)",
                path.c_str(), errno, strerror(errno));
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (depth < kMaxMediaScanDepth) {
                ESP_LOGD(TAG, "Descending into SD media subdirectory: %s",
                    path.c_str());
                ScanDirectory(path, files, max_items, depth + 1);
            }
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }

        auto extension = GetExtension(file_name);
        if (!IsSupportedMediaExtension(extension)) {
            ESP_LOGD(TAG, "Skipping unsupported SD media file: %s", path.c_str());
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
