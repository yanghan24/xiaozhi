#include "audio_recorder.h"

#include "application.h"
#include "audio_service.h"
#include "board.h"
#include "storage/sd_card_service.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cerrno>
#include <cstring>
#include <ctime>

#define TAG "AudioRecorder"

namespace {
constexpr const char* kRecordingsDirectory = SDCARD_MOUNT_POINT "/recordings";
constexpr uint32_t kMaxRecordingSeconds = 60 * 60;
constexpr uint64_t kMinFreeBytesToStart = 1024 * 1024;
constexpr uint64_t kMinFreeBytesDuringRecording = 512 * 1024;
constexpr int64_t kSpaceCheckIntervalUs = 1000000;

void WriteLe16(FILE* file, uint16_t value) {
    uint8_t bytes[2] = {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

void WriteLe32(FILE* file, uint32_t value) {
    uint8_t bytes[4] = {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
        static_cast<uint8_t>((value >> 16) & 0xff),
        static_cast<uint8_t>((value >> 24) & 0xff),
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}
} // namespace

AudioRecorder& AudioRecorder::GetInstance() {
    static AudioRecorder instance;
    return instance;
}

esp_err_t AudioRecorder::Start() {
    auto& control = DeviceControl::GetInstance();
    const auto permission_generation = control.GetGeneration();
    if (!control.IsCurrentGeneration(permission_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t storage_ret = Board::GetInstance().EnsureStorageReady();

    auto& storage = SdCardService::GetInstance();
    if (!storage.IsMounted()) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = std::string("SD mount failed: ") + esp_err_to_name(storage_ret);
        state_ = AudioRecorderState::kError;
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = storage.EnsureRecordingsDirectory();
    if (ret != ESP_OK) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = std::string("Prepare /sdcard/recordings failed: ") +
            esp_err_to_name(ret);
        if (ret == ESP_ERR_INVALID_ARG) {
            last_error_ += " (enable FatFS LFN)";
        }
        state_ = AudioRecorderState::kError;
        return ret;
    }

    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput,
            [](AudioFocusOwner owner, uint8_t resources) {
                (void)owner;
                if (resources & kAudioFocusResourceInput) {
                    AudioRecorder::GetInstance().PauseFromFocusLoss();
                }
            })) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Audio input is busy.";
        state_ = AudioRecorderState::kError;
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!control.IsCurrentGeneration(permission_generation)) {
        audio.ReleaseAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput);
        return ESP_ERR_INVALID_STATE;
    }
    if (state_ == AudioRecorderState::kRecording) {
        return ESP_OK;
    }
    if (file_ != nullptr) {
        FinalizeFileLocked();
    }

    last_free_bytes_ = storage.GetFreeBytes();
    if (last_free_bytes_ < kMinFreeBytesToStart) {
        last_error_ = "Not enough free space to start recording.";
        state_ = AudioRecorderState::kError;
        audio.ReleaseAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput);
        return ESP_ERR_NO_MEM;
    }

    file_name_ = BuildFileName();
    file_path_ = BuildFilePath(file_name_);
    file_ = fopen(file_path_.c_str(), "wb+");
    if (file_ == nullptr) {
        last_error_ = std::string("Failed to create recording: errno=") + std::to_string(errno);
        state_ = AudioRecorderState::kError;
        audio.ReleaseAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput);
        return ESP_FAIL;
    }

    WriteWavHeader(file_, 0);
    bytes_written_ = 0;
    last_error_.clear();
    last_space_check_us_ = esp_timer_get_time();
    state_ = AudioRecorderState::kRecording;
    permission_generation_ = permission_generation;
    const auto recording_generation = ++recording_generation_;

    bool observer_ok = audio.SetInputPcmObserver(
        kAudioFocusOwnerRecorder,
        [this, recording_generation, permission_generation](const int16_t* data, size_t samples,
                                                            int sample_rate, int channels) {
            OnPcmData(data, samples, sample_rate, channels, recording_generation,
                      permission_generation);
        });
    if (!observer_ok) {
        CloseWithErrorLocked("Failed to register recorder PCM observer.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Recording started: %s", file_path_.c_str());
    return ESP_OK;
}

esp_err_t AudioRecorder::Pause() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != AudioRecorderState::kRecording) {
            return ESP_OK;
        }
        state_ = AudioRecorderState::kPaused;
    }
    Application::GetInstance().GetAudioService().ReleaseAudioFocus(
        kAudioFocusOwnerRecorder, kAudioFocusResourceInput);
    ESP_LOGI(TAG, "Recording paused");
    return ESP_OK;
}

esp_err_t AudioRecorder::Resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!DeviceControl::GetInstance().IsCurrentGeneration(permission_generation_)) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput,
            [](AudioFocusOwner owner, uint8_t resources) {
                (void)owner;
                if (resources & kAudioFocusResourceInput) {
                    AudioRecorder::GetInstance().PauseFromFocusLoss();
                }
            })) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "Audio input is busy.";
        return ESP_ERR_INVALID_STATE;
    }

    bool can_resume = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        can_resume = state_ == AudioRecorderState::kPaused && file_ != nullptr;
        if (can_resume) {
            state_ = AudioRecorderState::kRecording;
            last_space_check_us_ = esp_timer_get_time();
            ESP_LOGI(TAG, "Recording resumed");
        }
    }
    if (can_resume) {
        return ESP_OK;
    }

    audio.ReleaseAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput);
    return Start();
}

esp_err_t AudioRecorder::TogglePauseResume() {
    auto status = GetStatus();
    if (status.state == AudioRecorderState::kRecording) {
        return Pause();
    }
    if (status.state == AudioRecorderState::kPaused) {
        return Resume();
    }
    return Start();
}

esp_err_t AudioRecorder::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++recording_generation_;
        if (file_ != nullptr) {
            FinalizeFileLocked();
        }
        if (state_ != AudioRecorderState::kError) {
            state_ = AudioRecorderState::kIdle;
        }
        ClearObserverAndFocus();
    }
    ESP_LOGI(TAG, "Recording stopped");
    return ESP_OK;
}

AudioRecorderStatus AudioRecorder::GetStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    AudioRecorderStatus status;
    status.state = state_;
    status.file_name = file_name_;
    status.file_path = file_path_;
    status.last_error = last_error_;
    status.bytes_written = bytes_written_;
    status.elapsed_seconds = bytes_written_ /
        (kRecorderPcmSampleRate * kRecorderPcmChannels *
            (kRecorderPcmBitsPerSample / 8));
    status.free_bytes = last_free_bytes_;
    return status;
}

std::string AudioRecorder::GetStatusText() {
    auto status = GetStatus();
    std::string text = StateName(status.state);
    if (!status.file_name.empty()) {
        text += ": " + status.file_name;
    }
    text += " " + std::to_string(status.elapsed_seconds) + "s";
    if (!status.last_error.empty() && status.state == AudioRecorderState::kError) {
        text += " (" + status.last_error + ")";
    }
    return text;
}

void AudioRecorder::OnPcmData(const int16_t* data, size_t samples, int sample_rate, int channels,
                              uint64_t recording_generation, uint64_t permission_generation) {
    bool should_clear = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (recording_generation != recording_generation_ ||
            !DeviceControl::GetInstance().IsCurrentGeneration(permission_generation)) {
            return;
        }
        if (state_ != AudioRecorderState::kRecording || file_ == nullptr || data == nullptr ||
            samples == 0) {
            return;
        }
        if (sample_rate != kRecorderPcmSampleRate || channels != kRecorderPcmChannels) {
            CloseWithErrorLocked("Recorder PCM format mismatch.");
            should_clear = true;
        } else if (!CheckFreeSpaceLocked(false)) {
            CloseWithErrorLocked("Low SD card free space; recording stopped.");
            should_clear = true;
        } else if (bytes_written_ >= kMaxRecordingSeconds * kRecorderPcmSampleRate *
                                         kRecorderPcmChannels * (kRecorderPcmBitsPerSample / 8)) {
            CloseWithErrorLocked("Maximum recording duration reached.");
            should_clear = true;
        } else {
            size_t written = fwrite(data, sizeof(int16_t), samples, file_);
            if (written != samples) {
                CloseWithErrorLocked("Failed to write recording data.");
                should_clear = true;
            } else {
                bytes_written_ += static_cast<uint32_t>(written * sizeof(int16_t));
            }
        }
        if (should_clear) {
            // Serialize cleanup with Start so an old callback cannot remove
            // the observer or focus installed by a later recording.
            ClearObserverAndFocus();
        }
    }
}

void AudioRecorder::PauseFromFocusLoss() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == AudioRecorderState::kRecording) {
        state_ = AudioRecorderState::kPaused;
        ESP_LOGI(TAG, "Recording paused by audio focus loss");
    }
}

void AudioRecorder::FinalizeFileLocked() {
    if (file_ == nullptr) {
        return;
    }
    WriteWavHeader(file_, bytes_written_);
    fflush(file_);
    fclose(file_);
    file_ = nullptr;
}

void AudioRecorder::CloseWithErrorLocked(const std::string& error) {
    last_error_ = error;
    state_ = AudioRecorderState::kError;
    FinalizeFileLocked();
    ESP_LOGW(TAG, "%s", error.c_str());
}

void AudioRecorder::ClearObserverAndFocus() {
    auto& audio = Application::GetInstance().GetAudioService();
    audio.ClearInputPcmObserver(kAudioFocusOwnerRecorder);
    audio.ReleaseAudioFocus(kAudioFocusOwnerRecorder, kAudioFocusResourceInput);
}

bool AudioRecorder::CheckFreeSpaceLocked(bool force) {
    int64_t now_us = esp_timer_get_time();
    if (!force && now_us - last_space_check_us_ < kSpaceCheckIntervalUs) {
        return last_free_bytes_ >= kMinFreeBytesDuringRecording;
    }
    last_space_check_us_ = now_us;
    last_free_bytes_ = SdCardService::GetInstance().GetFreeBytes();
    return last_free_bytes_ >= kMinFreeBytesDuringRecording;
}

std::string AudioRecorder::BuildFileName() const {
    time_t now = time(nullptr);
    struct tm timeinfo = {};
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < 2025 - 1900) {
        int64_t uptime_seconds = esp_timer_get_time() / 1000000;
        timeinfo = {};
        timeinfo.tm_year = 1970 - 1900;
        timeinfo.tm_mday = 1;
        timeinfo.tm_hour = (uptime_seconds / 3600) % 24;
        timeinfo.tm_min = (uptime_seconds / 60) % 60;
        timeinfo.tm_sec = uptime_seconds % 60;
    }

    char name[32];
    strftime(name, sizeof(name), "REC_%Y%m%d_%H%M%S.wav", &timeinfo);
    return name;
}

std::string AudioRecorder::BuildFilePath(const std::string& file_name) const {
    return std::string(kRecordingsDirectory) + "/" + file_name;
}

void AudioRecorder::WriteWavHeader(FILE* file, uint32_t data_bytes) {
    if (file == nullptr) {
        return;
    }

    uint32_t byte_rate = kRecorderPcmSampleRate * kRecorderPcmChannels *
        (kRecorderPcmBitsPerSample / 8);
    uint16_t block_align = kRecorderPcmChannels * (kRecorderPcmBitsPerSample / 8);

    fseek(file, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, file);
    WriteLe32(file, 36 + data_bytes);
    fwrite("WAVE", 1, 4, file);
    fwrite("fmt ", 1, 4, file);
    WriteLe32(file, 16);
    WriteLe16(file, 1);
    WriteLe16(file, kRecorderPcmChannels);
    WriteLe32(file, kRecorderPcmSampleRate);
    WriteLe32(file, byte_rate);
    WriteLe16(file, block_align);
    WriteLe16(file, kRecorderPcmBitsPerSample);
    fwrite("data", 1, 4, file);
    WriteLe32(file, data_bytes);
    fseek(file, 0, SEEK_END);
}

const char* AudioRecorder::StateName(AudioRecorderState state) {
    switch (state) {
        case AudioRecorderState::kRecording:
            return "Recording";
        case AudioRecorderState::kPaused:
            return "Paused";
        case AudioRecorderState::kError:
            return "Error";
        case AudioRecorderState::kIdle:
        default:
            return "Idle";
    }
}
