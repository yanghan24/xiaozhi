#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <esp_err.h>

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

enum class AudioRecorderState {
    kIdle,
    kRecording,
    kPaused,
    kError,
};

struct AudioRecorderStatus {
    AudioRecorderState state = AudioRecorderState::kIdle;
    std::string file_name;
    std::string file_path;
    std::string last_error;
    uint32_t elapsed_seconds = 0;
    uint32_t bytes_written = 0;
    uint64_t free_bytes = 0;
};

class AudioRecorder {
public:
    static AudioRecorder& GetInstance();

    esp_err_t Start();
    esp_err_t Pause();
    esp_err_t Resume();
    esp_err_t TogglePauseResume();
    esp_err_t Stop();

    AudioRecorderStatus GetStatus();
    std::string GetStatusText();

private:
    AudioRecorder() = default;
    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    mutable std::mutex mutex_;
    AudioRecorderState state_ = AudioRecorderState::kIdle;
    FILE* file_ = nullptr;
    std::string file_name_;
    std::string file_path_;
    std::string last_error_;
    uint32_t bytes_written_ = 0;
    uint64_t last_free_bytes_ = 0;
    int64_t last_space_check_us_ = 0;
    uint64_t recording_generation_ = 0;
    uint64_t permission_generation_ = 0;

    void OnPcmData(const int16_t* data, size_t samples, int sample_rate, int channels,
                   uint64_t recording_generation, uint64_t permission_generation);
    void StopFromFocusLoss();
    void FinalizeFileLocked();
    void CloseWithErrorLocked(const std::string& error);
    void ClearObserverAndFocus();
    bool CheckFreeSpaceLocked(bool force);
    std::string BuildFileName() const;
    std::string BuildFilePath(const std::string& file_name) const;

    static void WriteWavHeader(FILE* file, uint32_t data_bytes);
    static const char* StateName(AudioRecorderState state);
};

#endif // AUDIO_RECORDER_H
