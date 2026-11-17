#include "music_player.h"

#include "application.h"
#include "audio_service.h"
#include "board.h"
#include "device_control/device_control.h"

#include <esp_ae_rate_cvt.h>
#include <esp_audio_simple_dec.h>
#include <esp_err.h>
#include <esp_mp3_dec.h>
#include <esp_log.h>
#include <sdkconfig.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#define TAG "MusicPlayer"

namespace {
constexpr size_t kPlaybackChunkFrames = 1200;
constexpr size_t kMp3InputChunkBytes = 2048;
constexpr size_t kMp3OutputBufferBytes = 8192;
constexpr size_t kMaxListedTracks = 200;
constexpr uint32_t kPlaybackTaskStackBytes = 8192;
constexpr uint32_t kDeferredPlaybackTaskStackBytes = 4096;
constexpr uint32_t kDeferredPlaybackPollMs = 100;
constexpr uint32_t kDeferredPlaybackTimeoutMs = 20000;
constexpr uint32_t kDeferredPlaybackSettleMs = 200;
constexpr uint8_t kMusicFocusResources = kAudioFocusResourceOutput;

struct PlaybackTaskArg {
    MusicPlayer* player = nullptr;
    uint32_t generation = 0;
    uint64_t permission_generation = 0;
};

bool IsMusicPermissionCurrent(uint64_t generation) {
    auto& control = DeviceControl::GetInstance();
    return !control.Enabled() ||
           (control.IsVoiceAllowed() && control.IsCurrentGeneration(generation));
}

bool ReadExact(FILE* file, void* data, size_t size) {
    return fread(data, 1, size, file) == size;
}

bool ReadLe16(FILE* file, uint16_t& value) {
    uint8_t bytes[2] = {};
    if (!ReadExact(file, bytes, sizeof(bytes))) {
        return false;
    }
    value = static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8);
    return true;
}

bool ReadLe32(FILE* file, uint32_t& value) {
    uint8_t bytes[4] = {};
    if (!ReadExact(file, bytes, sizeof(bytes))) {
        return false;
    }
    value = static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool IsWavFile(const MediaFile& file) {
    return file.type == "wav" || file.extension == ".wav" || file.extension == ".WAV";
}

bool IsMp3File(const MediaFile& file) {
    return file.type == "mp3" || file.extension == ".mp3" || file.extension == ".MP3";
}

bool IsPlayableFile(const MediaFile& file) {
    return IsWavFile(file) || IsMp3File(file);
}

bool IsSuccessfulRegisterResult(esp_audio_err_t ret) {
    return ret == ESP_AUDIO_ERR_OK || ret == ESP_AUDIO_ERR_ALREADY_EXIST;
}

const char* TrackTypeName(const MediaFile& file) {
    if (IsMp3File(file)) {
        return "MP3";
    }
    if (IsWavFile(file)) {
        return "WAV";
    }
    return "audio";
}

class ScopedRateConverter {
public:
    ~ScopedRateConverter() {
        if (handle_ != nullptr) {
            esp_ae_rate_cvt_close(handle_);
        }
    }

    bool Open(int source_rate, int target_rate, int channels, std::string& error) {
        if (source_rate == target_rate) {
            channels_ = channels;
            return true;
        }
        if (source_rate <= 0 || target_rate <= 0 || channels <= 0) {
            error = "Invalid sample rate conversion parameters.";
            return false;
        }

        esp_ae_rate_cvt_cfg_t cfg = {};
        cfg.src_rate = static_cast<uint32_t>(source_rate);
        cfg.dest_rate = static_cast<uint32_t>(target_rate);
        cfg.channel = static_cast<uint8_t>(channels);
        cfg.bits_per_sample = ESP_AUDIO_BIT16;
        cfg.complexity = 2;
        cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;

        auto ret = esp_ae_rate_cvt_open(&cfg, &handle_);
        if (ret != ESP_AE_ERR_OK || handle_ == nullptr) {
            error = "Failed to create music resampler: " + std::to_string(static_cast<int>(ret));
            return false;
        }
        channels_ = channels;
        return true;
    }

    bool Process(std::vector<int16_t>& samples, std::string& error) {
        if (handle_ == nullptr || samples.empty()) {
            return true;
        }
        if (channels_ <= 0 || samples.size() % static_cast<size_t>(channels_) != 0) {
            error = "Invalid music PCM shape for resampling.";
            return false;
        }

        uint32_t output_samples = 0;
        uint32_t input_sample_points = static_cast<uint32_t>(
            samples.size() / static_cast<size_t>(channels_));
        auto ret = esp_ae_rate_cvt_get_max_out_sample_num(handle_,
            input_sample_points, &output_samples);
        if (ret != ESP_AE_ERR_OK || output_samples == 0) {
            error = "Failed to size music resampler output: " + std::to_string(static_cast<int>(ret));
            return false;
        }

        std::vector<int16_t> resampled(output_samples * static_cast<uint32_t>(channels_));
        uint32_t actual_output = output_samples;
        ret = esp_ae_rate_cvt_process(handle_, (esp_ae_sample_t)samples.data(),
            input_sample_points, (esp_ae_sample_t)resampled.data(),
            &actual_output);
        if (ret != ESP_AE_ERR_OK) {
            error = "Failed to resample music PCM: " + std::to_string(static_cast<int>(ret));
            return false;
        }

        resampled.resize(actual_output * static_cast<uint32_t>(channels_));
        samples = std::move(resampled);
        return true;
    }

private:
    esp_ae_rate_cvt_handle_t handle_ = nullptr;
    int channels_ = 0;
};

bool ConvertChannels(const int16_t* input, size_t frames, int input_channels,
        int output_channels, std::vector<int16_t>& output, std::string& error) {
    if (input == nullptr || frames == 0) {
        output.clear();
        return true;
    }
    if ((input_channels != 1 && input_channels != 2) ||
            (output_channels != 1 && output_channels != 2)) {
        error = "Unsupported audio channel conversion.";
        return false;
    }

    if (input_channels == output_channels) {
        output.assign(input, input + frames * input_channels);
        return true;
    }

    if (input_channels == 2 && output_channels == 1) {
        output.resize(frames);
        for (size_t i = 0, j = 0; i < frames; ++i, j += 2) {
            output[i] = static_cast<int16_t>(
                (static_cast<int32_t>(input[j]) + input[j + 1]) / 2);
        }
        return true;
    }

    output.resize(frames * 2);
    for (size_t i = 0, j = 0; i < frames; ++i, j += 2) {
        output[j] = input[i];
        output[j + 1] = input[i];
    }
    return true;
}

bool PrepareOutputChunk(const int16_t* input, size_t frames, int input_channels,
        int output_channels, ScopedRateConverter& rate_converter,
        std::vector<int16_t>& output, std::string& error) {
    if (!ConvertChannels(input, frames, input_channels, output_channels, output, error)) {
        return false;
    }
    return rate_converter.Process(output, error);
}

uint64_t NormalizeMp3Bitrate(uint32_t bitrate) {
    // esp_audio_simple_dec reports MP3 bitrate in kbps on some decoder versions
    // and in bps on others.
    return bitrate > 0 && bitrate < 10000 ? static_cast<uint64_t>(bitrate) * 1000
                                          : static_cast<uint64_t>(bitrate);
}

bool EnsureStorageForPlayback(std::string& error) {
    esp_err_t ret = Board::GetInstance().EnsureStorageReady();
    if (ret == ESP_OK || SdCardService::GetInstance().IsMounted()) {
        return true;
    }

    error = std::string("SD card is not ready: ") + esp_err_to_name(ret);
    return false;
}

bool IsVoiceOutputActive() {
    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();
    if (state == kDeviceStateConnecting ||
            state == kDeviceStateSpeaking) {
        return true;
    }
    return app.GetAudioService().HasAudioFocus(
        kAudioFocusOwnerVoiceInteraction, kAudioFocusResourceOutput);
}

void SuspendAudioInputForMusic() {
    auto& audio = Application::GetInstance().GetAudioService();
    audio.EnableVoiceProcessing(false);
    audio.EnableWakeWordDetection(false);
}

void RestoreIdleAudioInput() {
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();
    if (app.GetDeviceState() == kDeviceStateIdle &&
        !audio.HasAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput)) {
        audio.EnableVoiceProcessing(false);
        audio.EnableWakeWordDetection(true);
    }
}
} // namespace

MusicPlayer& MusicPlayer::GetInstance() {
    static MusicPlayer instance;
    return instance;
}

esp_err_t MusicPlayer::Play(int index) {
    return PlayForGeneration(index, DeviceControl::GetInstance().GetGeneration());
}

esp_err_t MusicPlayer::PlayForGeneration(int index, uint64_t permission_generation) {
    auto start = Application::GetInstance().GetAudioService().TryBeginActivity();
    if (!start)
        return ESP_ERR_INVALID_STATE;
    if (!IsMusicPermissionCurrent(permission_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    std::string storage_error;
    if (!EnsureStorageForPlayback(storage_error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = MusicPlayerState::kError;
        SetLastErrorLocked(storage_error);
        return ESP_ERR_INVALID_STATE;
    }
    Application::GetInstance().EndVoiceInteraction();

    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources,
                                 [](AudioFocusOwner owner, uint8_t resources) {
                                     (void)owner;
                                     if (resources & kMusicFocusResources) {
                                         MusicPlayer::GetInstance().StopFromFocusLoss();
                                     }
                                 })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }
    SuspendAudioInputForMusic();

    std::lock_guard<std::mutex> lock(mutex_);
    if (playlist_.empty()) {
        playlist_ = BuildPlayablePlaylistLocked();
    }
    if (playlist_.empty()) {
        state_ = MusicPlayerState::kIdle;
        current_index_ = -1;
        SetLastErrorLocked("No WAV/MP3 files found under /sdcard/music or /sdcard/recordings.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources);
        RestoreIdleAudioInput();
        return ESP_ERR_NOT_FOUND;
    }

    if (index < 0) {
        index = current_index_ >= 0 && current_index_ < static_cast<int>(playlist_.size()) ?
            current_index_ : 0;
    }
    if (index < 0 || index >= static_cast<int>(playlist_.size())) {
        SetLastErrorLocked("Track index is out of range.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources);
        RestoreIdleAudioInput();
        return ESP_ERR_INVALID_ARG;
    }

    return StartPlaybackLocked(index, 0, permission_generation);
}

esp_err_t MusicPlayer::Pause() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        CancelDeferredPlaybackLocked();
        auto_resume_after_focus_loss_ = false;
        if (state_ != MusicPlayerState::kPlaying) {
            return ESP_OK;
        }
        state_ = MusicPlayerState::kPaused;
    }
    Application::GetInstance().GetAudioService().ReleaseAudioFocus(kAudioFocusOwnerLocalMusic,
                                                                   kMusicFocusResources);
    RestoreIdleAudioInput();
    return ESP_OK;
}

esp_err_t MusicPlayer::Resume() {
    return ResumeForGeneration(DeviceControl::GetInstance().GetGeneration());
}

esp_err_t MusicPlayer::ResumeForGeneration(uint64_t permission_generation) {
    auto start = Application::GetInstance().GetAudioService().TryBeginActivity();
    if (!start)
        return ESP_ERR_INVALID_STATE;
    if (!IsMusicPermissionCurrent(permission_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    Application::GetInstance().EndVoiceInteraction();

    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources,
                                 [](AudioFocusOwner owner, uint8_t resources) {
                                     (void)owner;
                                     if (resources & kMusicFocusResources) {
                                         MusicPlayer::GetInstance().StopFromFocusLoss();
                                     }
                                 })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }
    SuspendAudioInputForMusic();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == MusicPlayerState::kPaused && current_index_ >= 0 &&
                current_index_ < static_cast<int>(playlist_.size())) {
            return StartPlaybackLocked(current_index_, resume_offset_, permission_generation);
        }
    }
    return PlayForGeneration(-1, permission_generation);
}

esp_err_t MusicPlayer::TogglePlayPause() {
    auto status = GetStatus();
    if (status.state == MusicPlayerState::kPlaying) {
        return Pause();
    }
    if (status.state == MusicPlayerState::kPaused) {
        return Resume();
    }
    return Play();
}

esp_err_t MusicPlayer::Stop() {
    bool had_active_playback = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        had_active_playback = state_ == MusicPlayerState::kPlaying ||
                              state_ == MusicPlayerState::kPaused ||
                              playback_task_handle_ != nullptr;
        CancelDeferredPlaybackLocked();
        playback_generation_++;
        state_ = MusicPlayerState::kIdle;
        resume_offset_ = 0;
        elapsed_seconds_ = 0;
        progress_per_mille_ = 0;
    }
    auto& audio = Application::GetInstance().GetAudioService();
    if (had_active_playback) {
        audio.ResetDecoder();
    }
    audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources);
    RestoreIdleAudioInput();
    return ESP_OK;
}

esp_err_t MusicPlayer::Next() {
    auto start = Application::GetInstance().GetAudioService().TryBeginActivity();
    if (!start)
        return ESP_ERR_INVALID_STATE;
    const auto permission_generation = DeviceControl::GetInstance().GetGeneration();
    if (!IsMusicPermissionCurrent(permission_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    std::string storage_error;
    if (!EnsureStorageForPlayback(storage_error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = MusicPlayerState::kError;
        SetLastErrorLocked(storage_error);
        return ESP_ERR_INVALID_STATE;
    }

    Application::GetInstance().EndVoiceInteraction();

    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources,
                                 [](AudioFocusOwner owner, uint8_t resources) {
                                     (void)owner;
                                     if (resources & kMusicFocusResources) {
                                         MusicPlayer::GetInstance().StopFromFocusLoss();
                                     }
                                 })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }
    SuspendAudioInputForMusic();

    std::lock_guard<std::mutex> lock(mutex_);
    if (playlist_.empty()) {
        playlist_ = BuildPlayablePlaylistLocked();
    }
    if (playlist_.empty()) {
        state_ = MusicPlayerState::kIdle;
        current_index_ = -1;
        SetLastErrorLocked("No WAV/MP3 files found under /sdcard/music or /sdcard/recordings.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources);
        RestoreIdleAudioInput();
        return ESP_ERR_NOT_FOUND;
    }
    int next = current_index_ < 0 ? 0 :
        (current_index_ + 1) % static_cast<int>(playlist_.size());
    return StartPlaybackLocked(next, 0, permission_generation);
}

esp_err_t MusicPlayer::Previous() {
    auto start = Application::GetInstance().GetAudioService().TryBeginActivity();
    if (!start)
        return ESP_ERR_INVALID_STATE;
    const auto permission_generation = DeviceControl::GetInstance().GetGeneration();
    if (!IsMusicPermissionCurrent(permission_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    std::string storage_error;
    if (!EnsureStorageForPlayback(storage_error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = MusicPlayerState::kError;
        SetLastErrorLocked(storage_error);
        return ESP_ERR_INVALID_STATE;
    }

    Application::GetInstance().EndVoiceInteraction();

    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources,
                                 [](AudioFocusOwner owner, uint8_t resources) {
                                     (void)owner;
                                     if (resources & kMusicFocusResources) {
                                         MusicPlayer::GetInstance().StopFromFocusLoss();
                                     }
                                 })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }
    SuspendAudioInputForMusic();

    std::lock_guard<std::mutex> lock(mutex_);
    if (playlist_.empty()) {
        playlist_ = BuildPlayablePlaylistLocked();
    }
    if (playlist_.empty()) {
        state_ = MusicPlayerState::kIdle;
        current_index_ = -1;
        SetLastErrorLocked("No WAV/MP3 files found under /sdcard/music or /sdcard/recordings.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kMusicFocusResources);
        RestoreIdleAudioInput();
        return ESP_ERR_NOT_FOUND;
    }
    int previous = current_index_ < 0 ? 0 :
        (current_index_ - 1 + static_cast<int>(playlist_.size())) %
            static_cast<int>(playlist_.size());
    return StartPlaybackLocked(previous, 0, permission_generation);
}

MusicPlayerStatus MusicPlayer::GetStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    MusicPlayerStatus status;
    status.state = state_;
    status.current_index = current_index_;
    status.playlist_size = playlist_.size();
    if (current_index_ >= 0 && current_index_ < static_cast<int>(playlist_.size())) {
        status.track_name = playlist_[current_index_].display_name;
        status.track_path = playlist_[current_index_].path;
        status.track_type = playlist_[current_index_].type;
    }
    status.last_error = last_error_;
    status.sample_rate = sample_rate_;
    status.channels = channels_;
    status.elapsed_seconds = elapsed_seconds_;
    status.duration_seconds = duration_seconds_;
    status.progress_per_mille = progress_per_mille_;
    return status;
}

std::vector<MediaFile> MusicPlayer::GetPlaylist() {
    std::string storage_error;
    if (!EnsureStorageForPlayback(storage_error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = MusicPlayerState::kError;
        SetLastErrorLocked(storage_error);
        return {};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    playlist_ = BuildPlayablePlaylistLocked();
    return playlist_;
}

std::string MusicPlayer::GetStatusText() {
    auto status = GetStatus();
    std::string text = std::string(StateName(status.state));
    if (!status.track_name.empty()) {
        if (!status.track_type.empty()) {
            text += " " + status.track_type;
        }
        text += ": " + status.track_name;
    }
    if (status.duration_seconds > 0) {
        text += " " + std::to_string(status.elapsed_seconds) + "/" +
            std::to_string(status.duration_seconds) + "s";
    }
    if (!status.last_error.empty() && status.state == MusicPlayerState::kError) {
        text += " (" + status.last_error + ")";
    }
    return text;
}

std::vector<MediaFile> MusicPlayer::BuildPlayablePlaylistLocked() {
    auto files = SdCardService::GetInstance().ScanMediaFiles(kMaxListedTracks);
    files.erase(std::remove_if(files.begin(), files.end(), [](const MediaFile& file) {
        return !IsPlayableFile(file);
    }), files.end());
    return files;
}

esp_err_t MusicPlayer::StartPlaybackLocked(int index, uint32_t resume_offset,
                                           uint64_t permission_generation) {
    if (!IsMusicPermissionCurrent(permission_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index < 0 || index >= static_cast<int>(playlist_.size())) {
        SetLastErrorLocked("Track index is out of range.");
        return ESP_ERR_INVALID_ARG;
    }

    playback_generation_++;
    playback_permission_generation_ = permission_generation;
    current_index_ = index;
    resume_offset_ = resume_offset;
    if (resume_offset == 0) {
        elapsed_seconds_ = 0;
        progress_per_mille_ = 0;
    }
    duration_seconds_ = 0;
    sample_rate_ = 0;
    channels_ = 0;
    last_error_.clear();
    auto_resume_after_focus_loss_ = false;
    deferred_play_index_ = -1;
    deferred_resume_ = false;
    state_ = MusicPlayerState::kPlaying;

    auto* arg = new PlaybackTaskArg();
    arg->player = this;
    arg->generation = playback_generation_;
    arg->permission_generation = permission_generation;
    BaseType_t ret = xTaskCreate(&MusicPlayer::PlaybackTask, "music_player",
        kPlaybackTaskStackBytes, arg, 4, &playback_task_handle_);
    if (ret != pdPASS) {
        delete arg;
        playback_task_handle_ = nullptr;
        state_ = MusicPlayerState::kError;
        SetLastErrorLocked("Failed to create music playback task.");
        Application::GetInstance().GetAudioService().ReleaseAudioFocus(kAudioFocusOwnerLocalMusic,
                                                                       kMusicFocusResources);
        RestoreIdleAudioInput();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Playing %s track %d/%u: %s", TrackTypeName(playlist_[index]),
        index + 1, static_cast<unsigned>(playlist_.size()), playlist_[index].path.c_str());
    return ESP_OK;
}

void MusicPlayer::StopFromFocusLoss() {
    ESP_LOGI(TAG, "Local music stopped by audio focus loss");
    Stop();
}

esp_err_t MusicPlayer::SchedulePlayAfterVoiceInteraction(int index,
                                                         uint64_t permission_generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto_resume_after_focus_loss_ = false;
    return StartDeferredPlaybackTaskLocked(index, false, permission_generation);
}

esp_err_t MusicPlayer::ScheduleResumeAfterVoiceInteraction(uint64_t permission_generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    return StartDeferredPlaybackTaskLocked(current_index_, true, permission_generation);
}

esp_err_t MusicPlayer::StartDeferredPlaybackTaskLocked(int index, bool resume,
                                                       uint64_t permission_generation) {
    if (!IsMusicPermissionCurrent(permission_generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    deferred_play_index_ = index;
    deferred_resume_ = resume;
    deferred_permission_generation_ = permission_generation;
    if (deferred_playback_task_handle_ != nullptr) {
        ESP_LOGI(TAG, "Updated deferred music playback request");
        return ESP_OK;
    }

    auto* arg = new PlaybackTaskArg();
    arg->player = this;
    arg->generation = ++deferred_playback_generation_;
    BaseType_t ret = xTaskCreate(&MusicPlayer::DeferredPlaybackTask,
        "music_defer", kDeferredPlaybackTaskStackBytes, arg, 3,
        &deferred_playback_task_handle_);
    if (ret != pdPASS) {
        delete arg;
        deferred_playback_task_handle_ = nullptr;
        deferred_play_index_ = -1;
        deferred_resume_ = false;
        auto_resume_after_focus_loss_ = false;
        SetLastErrorLocked("Failed to create deferred music playback task.");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Deferred local music until voice interaction finishes");
    return ESP_OK;
}

void MusicPlayer::CancelDeferredPlaybackLocked() {
    deferred_playback_generation_++;
    deferred_playback_task_handle_ = nullptr;
    deferred_play_index_ = -1;
    deferred_resume_ = false;
}

void MusicPlayer::DeferredPlaybackLoop(uint32_t generation) {
    uint32_t waited_ms = 0;
    while (IsVoiceOutputActive() && waited_ms < kDeferredPlaybackTimeoutMs) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != deferred_playback_generation_) {
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kDeferredPlaybackPollMs));
        waited_ms += kDeferredPlaybackPollMs;
    }

    if (IsVoiceOutputActive()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == deferred_playback_generation_) {
            deferred_playback_task_handle_ = nullptr;
            deferred_play_index_ = -1;
            deferred_resume_ = false;
            auto_resume_after_focus_loss_ = false;
            SetLastErrorLocked("Timed out waiting for voice interaction to finish.");
            ESP_LOGW(TAG, "%s", last_error_.c_str());
        }
        return;
    }

    auto& audio = Application::GetInstance().GetAudioService();
    audio.WaitForPlaybackQueueEmpty();
    vTaskDelay(pdMS_TO_TICKS(kDeferredPlaybackSettleMs));

    int index = -1;
    bool resume = false;
    uint64_t permission_generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != deferred_playback_generation_) {
            return;
        }
        index = deferred_play_index_;
        resume = deferred_resume_ || auto_resume_after_focus_loss_;
        permission_generation = deferred_permission_generation_;
        deferred_playback_task_handle_ = nullptr;
        deferred_play_index_ = -1;
        deferred_resume_ = false;
        auto_resume_after_focus_loss_ = false;
    }

    esp_err_t ret = resume ? ResumeForGeneration(permission_generation)
                           : PlayForGeneration(index, permission_generation);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Deferred local music playback failed: %s",
            esp_err_to_name(ret));
    }
}

void MusicPlayer::PlaybackLoop(uint32_t generation, uint64_t permission_generation) {
    MediaFile track;
    uint32_t start_offset = 0;
    bool invalid_track = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != playback_generation_) {
            return;
        }
        if (current_index_ < 0 ||
                current_index_ >= static_cast<int>(playlist_.size())) {
            invalid_track = true;
        } else {
            track = playlist_[current_index_];
            start_offset = resume_offset_;
        }
    }
    if (invalid_track) {
        FinishPlayback(generation, MusicPlayerState::kIdle, "");
        return;
    }

    if (IsMp3File(track)) {
        PlaybackMp3(generation, permission_generation, track, start_offset);
        return;
    }

    PlaybackWav(generation, permission_generation, track, start_offset);
}

void MusicPlayer::PlaybackWav(uint32_t generation, uint64_t permission_generation,
                              const MediaFile& track, uint32_t start_offset) {
    FILE* file = fopen(track.path.c_str(), "rb");
    if (file == nullptr) {
        FinishPlayback(generation, MusicPlayerState::kError,
            std::string("Failed to open WAV file: errno=") + std::to_string(errno));
        return;
    }

    WavInfo info;
    std::string error;
    if (!ParseWav(file, info, error)) {
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError, error);
        return;
    }

    auto* codec = Board::GetInstance().GetAudioCodec();
    int output_sample_rate = codec != nullptr ? codec->output_sample_rate() : 0;
    int output_channels = codec != nullptr ? codec->output_channels() : 1;
    if (output_channels <= 0) {
        output_channels = 1;
    }
    if (output_sample_rate <= 0) {
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError,
            "Audio output sample rate is unavailable.");
        return;
    }

    if (info.audio_format != 1 || info.bits_per_sample != 16 ||
            (info.channels != 1 && info.channels != 2)) {
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError,
            "Only 16-bit PCM mono/stereo WAV files are supported.");
        return;
    }
    if (output_channels != 1 && output_channels != 2) {
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError,
            "Unsupported audio output channel count.");
        return;
    }

    ScopedRateConverter rate_converter;
    if (!rate_converter.Open(static_cast<int>(info.sample_rate), output_sample_rate,
            output_channels, error)) {
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError, error);
        return;
    }

    uint32_t playback_offset = start_offset;
    if (playback_offset > info.data_size) {
        playback_offset = 0;
    }
    fseek(file, static_cast<long>(info.data_offset + playback_offset), SEEK_SET);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == playback_generation_) {
            sample_rate_ = static_cast<int>(info.sample_rate);
            channels_ = static_cast<int>(info.channels);
            duration_seconds_ = info.byte_rate > 0 ?
                static_cast<int>(info.data_size / info.byte_rate) : 0;
            elapsed_seconds_ = info.byte_rate > 0 ?
                static_cast<int>(playback_offset / info.byte_rate) : 0;
            progress_per_mille_ =
                info.data_size > 0
                    ? static_cast<int>((static_cast<uint64_t>(playback_offset) * 1000) /
                                       info.data_size)
                    : 0;
        }
    }

    const size_t input_channels = info.channels;
    const size_t input_samples_per_chunk = kPlaybackChunkFrames * input_channels;
    std::vector<int16_t> input(input_samples_per_chunk);

    while (playback_offset < info.data_size) {
        MusicPlayerState requested_state = MusicPlayerState::kPlaying;
        std::string requested_error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != playback_generation_) {
                fclose(file);
                return;
            }
            if (state_ == MusicPlayerState::kPaused) {
                resume_offset_ = playback_offset;
                requested_state = MusicPlayerState::kPaused;
            } else if (state_ != MusicPlayerState::kPlaying) {
                requested_state = state_;
                requested_error = last_error_;
            }
        }
        if (requested_state != MusicPlayerState::kPlaying) {
            fclose(file);
            FinishPlayback(generation, requested_state, requested_error);
            return;
        }

        size_t bytes_left = info.data_size - playback_offset;
        size_t frames_to_read = std::min(kPlaybackChunkFrames,
            bytes_left / (input_channels * sizeof(int16_t)));
        if (frames_to_read == 0) {
            break;
        }

        size_t samples_to_read = frames_to_read * input_channels;
        size_t samples_read = fread(input.data(), sizeof(int16_t), samples_to_read, file);
        if (samples_read == 0) {
            break;
        }
        size_t frames_read = samples_read / input_channels;
        playback_offset += static_cast<uint32_t>(frames_read * input_channels * sizeof(int16_t));

        std::vector<int16_t> output;
        if (!PrepareOutputChunk(input.data(), frames_read,
                static_cast<int>(input_channels), output_channels, rate_converter,
                output, error)) {
            fclose(file);
            FinishPlayback(generation, MusicPlayerState::kError, error);
            return;
        }

        auto queue_result = QueuePcmChunk(generation, permission_generation, std::move(output),
                                          playback_offset, "Failed to queue WAV PCM.");
        if (queue_result != PlaybackLoopResult::kContinue) {
            fclose(file);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation == playback_generation_) {
                resume_offset_ = playback_offset;
                if (info.byte_rate > 0) {
                    elapsed_seconds_ = static_cast<int>(playback_offset / info.byte_rate);
                }
                progress_per_mille_ =
                    info.data_size > 0
                        ? std::clamp(
                              static_cast<int>((static_cast<uint64_t>(playback_offset) * 1000) /
                                               info.data_size),
                              0, 1000)
                        : 0;
            }
        }
    }

    fclose(file);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == playback_generation_) {
            progress_per_mille_ = 1000;
        }
    }
    FinishPlayback(generation, MusicPlayerState::kIdle, "");
}

void MusicPlayer::PlaybackMp3(uint32_t generation, uint64_t permission_generation,
                              const MediaFile& track, uint32_t start_offset) {
#if CONFIG_AUDIO_DECODER_MP3_SUPPORT
    FILE* file = fopen(track.path.c_str(), "rb");
    if (file == nullptr) {
        FinishPlayback(generation, MusicPlayerState::kError,
            std::string("Failed to open MP3 file: errno=") + std::to_string(errno));
        return;
    }
    if (start_offset > 0) {
        fseek(file, static_cast<long>(start_offset), SEEK_SET);
    }

    auto register_ret = esp_mp3_dec_register();
    if (!IsSuccessfulRegisterResult(register_ret)) {
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError,
            "Failed to register MP3 decoder: " +
            std::to_string(static_cast<int>(register_ret)));
        return;
    }

    esp_audio_simple_dec_cfg_t dec_cfg = {};
    dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    dec_cfg.use_frame_dec = false;

    esp_audio_simple_dec_handle_t decoder = nullptr;
    auto ret = esp_audio_simple_dec_open(&dec_cfg, &decoder);
    if (ret != ESP_AUDIO_ERR_OK || decoder == nullptr) {
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError,
            "Failed to open MP3 decoder: " + std::to_string(static_cast<int>(ret)));
        return;
    }

    auto* codec = Board::GetInstance().GetAudioCodec();
    int output_sample_rate = codec != nullptr ? codec->output_sample_rate() : 0;
    int output_channels = codec != nullptr ? codec->output_channels() : 1;
    if (output_channels <= 0) {
        output_channels = 1;
    }
    if (output_sample_rate <= 0 || (output_channels != 1 && output_channels != 2)) {
        esp_audio_simple_dec_close(decoder);
        fclose(file);
        FinishPlayback(generation, MusicPlayerState::kError,
            "Unsupported audio output format for MP3 playback.");
        return;
    }

    std::vector<uint8_t> input(kMp3InputChunkBytes);
    std::vector<uint8_t> decoded(kMp3OutputBufferBytes);
    ScopedRateConverter rate_converter;
    bool format_ready = false;
    int source_sample_rate = 0;
    int source_channels = 0;
    uint64_t decoded_source_frames = 0;
    uint32_t resume_file_offset = static_cast<uint32_t>(ftell(file));
    int elapsed_base_seconds = 0;
    std::string error;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == playback_generation_) {
            elapsed_base_seconds = elapsed_seconds_;
            progress_per_mille_ =
                track.size_bytes > 0
                    ? std::clamp(
                          static_cast<int>((static_cast<uint64_t>(resume_file_offset) * 1000) /
                                           track.size_bytes),
                          0, 1000)
                    : 0;
        }
    }

    while (true) {
        MusicPlayerState requested_state = MusicPlayerState::kPlaying;
        std::string requested_error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != playback_generation_) {
                esp_audio_simple_dec_close(decoder);
                fclose(file);
                return;
            }
            if (state_ == MusicPlayerState::kPaused) {
                resume_offset_ = resume_file_offset;
                requested_state = MusicPlayerState::kPaused;
            } else if (state_ != MusicPlayerState::kPlaying) {
                requested_state = state_;
                requested_error = last_error_;
            }
        }
        if (requested_state != MusicPlayerState::kPlaying) {
            esp_audio_simple_dec_close(decoder);
            fclose(file);
            FinishPlayback(generation, requested_state, requested_error);
            return;
        }

        long chunk_start = ftell(file);
        size_t bytes_read = fread(input.data(), 1, input.size(), file);
        if (bytes_read == 0) {
            break;
        }

        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = input.data();
        raw.len = static_cast<uint32_t>(bytes_read);
        raw.eos = bytes_read < input.size() && feof(file);

        uint32_t chunk_consumed = 0;
        while (raw.len > 0) {
            esp_audio_simple_dec_out_t out_frame = {};
            out_frame.buffer = decoded.data();
            out_frame.len = static_cast<uint32_t>(decoded.size());

            ret = esp_audio_simple_dec_process(decoder, &raw, &out_frame);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                if (out_frame.needed_size == 0) {
                    error = "MP3 decoder requested an invalid output buffer size.";
                    break;
                }
                decoded.resize(out_frame.needed_size);
                continue;
            }
            if (ret != ESP_AUDIO_ERR_OK) {
                error = "Failed to decode MP3 data: " + std::to_string(static_cast<int>(ret));
                break;
            }

            uint32_t consumed = std::min(raw.consumed, raw.len);
            chunk_consumed += consumed;
            resume_file_offset = static_cast<uint32_t>(chunk_start + chunk_consumed);

            if (out_frame.decoded_size > 0) {
                esp_audio_simple_dec_info_t info = {};
                auto info_ret = esp_audio_simple_dec_get_info(decoder, &info);
                if (info_ret != ESP_AUDIO_ERR_OK) {
                    error = "Failed to query MP3 stream info: " +
                        std::to_string(static_cast<int>(info_ret));
                    break;
                }
                if (info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2)) {
                    error = "Only 16-bit mono/stereo MP3 output is supported.";
                    break;
                }

                if (!format_ready) {
                    source_sample_rate = static_cast<int>(info.sample_rate);
                    source_channels = static_cast<int>(info.channel);
                    if (!rate_converter.Open(source_sample_rate, output_sample_rate,
                            output_channels, error)) {
                        break;
                    }
                    format_ready = true;
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (generation == playback_generation_) {
                        sample_rate_ = source_sample_rate;
                        channels_ = source_channels;
                        uint64_t bitrate_bps = NormalizeMp3Bitrate(info.bitrate);
                        duration_seconds_ =
                            bitrate_bps > 0 ? static_cast<int>((track.size_bytes * 8) / bitrate_bps)
                                            : 0;
                    }
                }

                size_t decoded_samples = out_frame.decoded_size / sizeof(int16_t);
                size_t frames = decoded_samples / static_cast<size_t>(source_channels);
                std::vector<int16_t> output;
                if (!PrepareOutputChunk(reinterpret_cast<int16_t*>(out_frame.buffer), frames,
                        source_channels, output_channels, rate_converter, output, error)) {
                    break;
                }

                auto queue_result =
                    QueuePcmChunk(generation, permission_generation, std::move(output),
                                  resume_file_offset, "Failed to queue MP3 PCM.");
                if (queue_result != PlaybackLoopResult::kContinue) {
                    esp_audio_simple_dec_close(decoder);
                    fclose(file);
                    return;
                }

                decoded_source_frames += frames;
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation == playback_generation_ && source_sample_rate > 0) {
                    resume_offset_ = resume_file_offset;
                    elapsed_seconds_ = elapsed_base_seconds +
                                       static_cast<int>(decoded_source_frames /
                                                        static_cast<uint64_t>(source_sample_rate));
                    progress_per_mille_ =
                        track.size_bytes > 0
                            ? std::clamp(static_cast<int>(
                                             (static_cast<uint64_t>(resume_file_offset) * 1000) /
                                             track.size_bytes),
                                         0, 1000)
                            : 0;
                    if (duration_seconds_ == 0 && info.bitrate > 0) {
                        uint64_t bitrate_bps = NormalizeMp3Bitrate(info.bitrate);
                        duration_seconds_ =
                            bitrate_bps > 0 ? static_cast<int>((track.size_bytes * 8) / bitrate_bps)
                                            : 0;
                    }
                }
            }

            if (consumed == 0 && out_frame.decoded_size == 0) {
                break;
            }
            raw.buffer += consumed;
            raw.len -= consumed;
        }

        if (!error.empty()) {
            break;
        }
        if (raw.eos) {
            break;
        }
    }

    esp_audio_simple_dec_close(decoder);
    fclose(file);
    if (!error.empty()) {
        FinishPlayback(generation, MusicPlayerState::kError, error);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == playback_generation_) {
            progress_per_mille_ = 1000;
        }
    }
    FinishPlayback(generation, MusicPlayerState::kIdle, "");
#else
    (void)permission_generation;
    (void)track;
    (void)start_offset;
    FinishPlayback(generation, MusicPlayerState::kError,
        "MP3 playback requires CONFIG_AUDIO_DECODER_MP3_SUPPORT.");
#endif
}

MusicPlayer::PlaybackLoopResult MusicPlayer::QueuePcmChunk(uint32_t generation,
                                                           uint64_t permission_generation,
                                                           std::vector<int16_t>&& output,
                                                           uint32_t resume_offset,
                                                           const std::string& error_on_cancel) {
    auto& audio = Application::GetInstance().GetAudioService();
    while (true) {
        if (!IsMusicPermissionCurrent(permission_generation)) {
            return PlaybackLoopResult::kStopped;
        }
        MusicPlayerState requested_state = MusicPlayerState::kPlaying;
        std::string requested_error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation != playback_generation_) {
                return PlaybackLoopResult::kStopped;
            }
            if (state_ == MusicPlayerState::kPaused) {
                resume_offset_ = resume_offset;
                requested_state = MusicPlayerState::kPaused;
            } else if (state_ != MusicPlayerState::kPlaying) {
                requested_state = state_;
                requested_error = last_error_;
            }
        }
        if (requested_state != MusicPlayerState::kPlaying) {
            FinishPlayback(generation, requested_state, requested_error);
            return PlaybackLoopResult::kStopped;
        }
        if (output.empty()) {
            FinishPlayback(generation, MusicPlayerState::kError, error_on_cancel);
            return PlaybackLoopResult::kError;
        }

        if (audio.PushPcmToPlaybackQueue(kAudioFocusOwnerLocalMusic, std::move(output),
                                         permission_generation, true)) {
            return PlaybackLoopResult::kContinue;
        }
        if (!audio.HasAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput)) {
            FinishPlayback(generation, MusicPlayerState::kPaused, "");
            return PlaybackLoopResult::kStopped;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void MusicPlayer::FinishPlayback(uint32_t generation, MusicPlayerState state,
        const std::string& error) {
    bool release_focus = state == MusicPlayerState::kIdle || !error.empty();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != playback_generation_) {
            return;
        }
    }
    if (state == MusicPlayerState::kIdle && error.empty()) {
        Application::GetInstance().GetAudioService().WaitForPlaybackQueueEmpty();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation != playback_generation_) {
            return;
        }
        state_ = state;
        if (state == MusicPlayerState::kIdle) {
            resume_offset_ = 0;
        }
        if (!error.empty()) {
            SetLastErrorLocked(error);
            state_ = MusicPlayerState::kError;
            ESP_LOGW(TAG, "%s", error.c_str());
        }
        playback_task_handle_ = nullptr;
    }
    if (release_focus) {
        Application::GetInstance().GetAudioService().ReleaseAudioFocus(kAudioFocusOwnerLocalMusic,
                                                                       kMusicFocusResources);
        RestoreIdleAudioInput();
    }
}

void MusicPlayer::SetLastErrorLocked(const std::string& error) {
    last_error_ = error;
}

void MusicPlayer::PlaybackTask(void* arg) {
    auto* task_arg = static_cast<PlaybackTaskArg*>(arg);
    MusicPlayer* player = task_arg->player;
    uint32_t generation = task_arg->generation;
    const auto permission_generation = task_arg->permission_generation;
    delete task_arg;
    player->PlaybackLoop(generation, permission_generation);
    vTaskDelete(nullptr);
}

void MusicPlayer::DeferredPlaybackTask(void* arg) {
    auto* task_arg = static_cast<PlaybackTaskArg*>(arg);
    MusicPlayer* player = task_arg->player;
    uint32_t generation = task_arg->generation;
    delete task_arg;
    player->DeferredPlaybackLoop(generation);
    vTaskDelete(nullptr);
}

bool MusicPlayer::ParseWav(FILE* file, WavInfo& info, std::string& error) {
    char riff[4] = {};
    char wave[4] = {};
    uint32_t riff_size = 0;
    if (!ReadExact(file, riff, sizeof(riff)) ||
            !ReadLe32(file, riff_size) ||
            !ReadExact(file, wave, sizeof(wave)) ||
            std::memcmp(riff, "RIFF", 4) != 0 ||
            std::memcmp(wave, "WAVE", 4) != 0) {
        error = "Invalid WAV header.";
        return false;
    }
    (void)riff_size;

    bool fmt_found = false;
    bool data_found = false;
    while (!data_found) {
        char chunk_id[4] = {};
        uint32_t chunk_size = 0;
        if (!ReadExact(file, chunk_id, sizeof(chunk_id)) ||
                !ReadLe32(file, chunk_size)) {
            break;
        }

        long chunk_start = ftell(file);
        if (std::memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16 ||
                    !ReadLe16(file, info.audio_format) ||
                    !ReadLe16(file, info.channels) ||
                    !ReadLe32(file, info.sample_rate) ||
                    !ReadLe32(file, info.byte_rate)) {
                error = "Invalid WAV fmt chunk.";
                return false;
            }
            uint16_t block_align = 0;
            if (!ReadLe16(file, block_align) ||
                    !ReadLe16(file, info.bits_per_sample)) {
                error = "Invalid WAV fmt chunk.";
                return false;
            }
            (void)block_align;
            fmt_found = true;
        } else if (std::memcmp(chunk_id, "data", 4) == 0) {
            info.data_offset = static_cast<uint32_t>(ftell(file));
            info.data_size = chunk_size;
            data_found = true;
            break;
        }

        long next = chunk_start + static_cast<long>(chunk_size);
        if (chunk_size & 1) {
            next++;
        }
        if (fseek(file, next, SEEK_SET) != 0) {
            error = "Invalid WAV chunk layout.";
            return false;
        }
    }

    if (!fmt_found || !data_found || info.data_size == 0) {
        error = "WAV fmt/data chunk not found.";
        return false;
    }
    return true;
}

const char* MusicPlayer::StateName(MusicPlayerState state) {
    switch (state) {
        case MusicPlayerState::kPlaying:
            return "Playing";
        case MusicPlayerState::kPaused:
            return "Paused";
        case MusicPlayerState::kError:
            return "Error";
        case MusicPlayerState::kIdle:
        default:
            return "Idle";
    }
}
