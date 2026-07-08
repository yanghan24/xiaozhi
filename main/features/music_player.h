#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include "storage/sd_card_service.h"

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

enum class MusicPlayerState {
    kIdle,
    kPlaying,
    kPaused,
    kError,
};

struct MusicPlayerStatus {
    MusicPlayerState state = MusicPlayerState::kIdle;
    int current_index = -1;
    size_t playlist_size = 0;
    std::string track_name;
    std::string track_path;
    std::string track_type;
    std::string last_error;
    int sample_rate = 0;
    int channels = 0;
    int elapsed_seconds = 0;
    int duration_seconds = 0;
};

class MusicPlayer {
public:
    static MusicPlayer& GetInstance();

    esp_err_t Play(int index = -1);
    esp_err_t Pause();
    esp_err_t Resume();
    esp_err_t TogglePlayPause();
    esp_err_t Stop();
    esp_err_t Next();
    esp_err_t Previous();

    MusicPlayerStatus GetStatus();
    std::vector<MediaFile> GetPlaylist();
    std::string GetStatusText();

private:
    MusicPlayer() = default;
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    struct WavInfo {
        uint32_t data_offset = 0;
        uint32_t data_size = 0;
        uint32_t sample_rate = 0;
        uint16_t channels = 0;
        uint16_t bits_per_sample = 0;
        uint16_t audio_format = 0;
        uint32_t byte_rate = 0;
    };

    enum class PlaybackLoopResult {
        kContinue,
        kStopped,
        kError,
    };

    mutable std::mutex mutex_;
    std::vector<MediaFile> playlist_;
    MusicPlayerState state_ = MusicPlayerState::kIdle;
    int current_index_ = -1;
    std::string last_error_;
    TaskHandle_t playback_task_handle_ = nullptr;
    uint32_t playback_generation_ = 0;
    uint32_t resume_offset_ = 0;
    int elapsed_seconds_ = 0;
    int duration_seconds_ = 0;
    int sample_rate_ = 0;
    int channels_ = 0;

    std::vector<MediaFile> BuildPlayablePlaylistLocked();
    esp_err_t StartPlaybackLocked(int index, uint32_t resume_offset);
    void PauseFromFocusLoss();
    void PlaybackLoop(uint32_t generation);
    void PlaybackWav(uint32_t generation, const MediaFile& track, uint32_t start_offset);
    void PlaybackMp3(uint32_t generation, const MediaFile& track, uint32_t start_offset);
    PlaybackLoopResult QueuePcmChunk(uint32_t generation, std::vector<int16_t>&& output,
        uint32_t resume_offset, const std::string& error_on_cancel);
    void FinishPlayback(uint32_t generation, MusicPlayerState state, const std::string& error);
    void SetLastErrorLocked(const std::string& error);

    static void PlaybackTask(void* arg);
    static bool ParseWav(FILE* file, WavInfo& info, std::string& error);
    static const char* StateName(MusicPlayerState state);
};

#endif // MUSIC_PLAYER_H
