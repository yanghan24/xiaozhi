#include "music_player.h"

#include "application.h"
#include "audio_service.h"
#include "board.h"

#include <esp_ae_rate_cvt.h>
#include <esp_audio_simple_dec.h>
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

struct PlaybackTaskArg {
    MusicPlayer* player = nullptr;
    uint32_t generation = 0;
};

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
} // namespace

MusicPlayer& MusicPlayer::GetInstance() {
    static MusicPlayer instance;
    return instance;
}

esp_err_t MusicPlayer::Play(int index) {
    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput,
            [](AudioFocusOwner owner, uint8_t resources) {
                (void)owner;
                if (resources & kAudioFocusResourceOutput) {
                    MusicPlayer::GetInstance().PauseFromFocusLoss();
                }
            })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    playlist_ = BuildPlayablePlaylistLocked();
    if (playlist_.empty()) {
        state_ = MusicPlayerState::kIdle;
        current_index_ = -1;
        SetLastErrorLocked("No WAV/MP3 files found under /sdcard/music or /sdcard/recordings.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
        return ESP_ERR_NOT_FOUND;
    }

    if (index < 0) {
        index = current_index_ >= 0 && current_index_ < static_cast<int>(playlist_.size()) ?
            current_index_ : 0;
    }
    if (index < 0 || index >= static_cast<int>(playlist_.size())) {
        SetLastErrorLocked("Track index is out of range.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
        return ESP_ERR_INVALID_ARG;
    }

    return StartPlaybackLocked(index, 0);
}

esp_err_t MusicPlayer::Pause() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != MusicPlayerState::kPlaying) {
            return ESP_OK;
        }
        state_ = MusicPlayerState::kPaused;
    }
    Application::GetInstance().GetAudioService().ReleaseAudioFocus(
        kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
    return ESP_OK;
}

esp_err_t MusicPlayer::Resume() {
    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput,
            [](AudioFocusOwner owner, uint8_t resources) {
                (void)owner;
                if (resources & kAudioFocusResourceOutput) {
                    MusicPlayer::GetInstance().PauseFromFocusLoss();
                }
            })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == MusicPlayerState::kPaused && current_index_ >= 0 &&
                current_index_ < static_cast<int>(playlist_.size())) {
            return StartPlaybackLocked(current_index_, resume_offset_);
        }
    }
    return Play();
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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        playback_generation_++;
        state_ = MusicPlayerState::kIdle;
        resume_offset_ = 0;
        elapsed_seconds_ = 0;
    }
    Application::GetInstance().GetAudioService().ReleaseAudioFocus(
        kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
    return ESP_OK;
}

esp_err_t MusicPlayer::Next() {
    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput,
            [](AudioFocusOwner owner, uint8_t resources) {
                (void)owner;
                if (resources & kAudioFocusResourceOutput) {
                    MusicPlayer::GetInstance().PauseFromFocusLoss();
                }
            })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    playlist_ = BuildPlayablePlaylistLocked();
    if (playlist_.empty()) {
        state_ = MusicPlayerState::kIdle;
        current_index_ = -1;
        SetLastErrorLocked("No WAV/MP3 files found under /sdcard/music or /sdcard/recordings.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
        return ESP_ERR_NOT_FOUND;
    }
    int next = current_index_ < 0 ? 0 :
        (current_index_ + 1) % static_cast<int>(playlist_.size());
    return StartPlaybackLocked(next, 0);
}

esp_err_t MusicPlayer::Previous() {
    auto& audio = Application::GetInstance().GetAudioService();
    if (!audio.RequestAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput,
            [](AudioFocusOwner owner, uint8_t resources) {
                (void)owner;
                if (resources & kAudioFocusResourceOutput) {
                    MusicPlayer::GetInstance().PauseFromFocusLoss();
                }
            })) {
        std::lock_guard<std::mutex> lock(mutex_);
        SetLastErrorLocked("Audio output is busy.");
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    playlist_ = BuildPlayablePlaylistLocked();
    if (playlist_.empty()) {
        state_ = MusicPlayerState::kIdle;
        current_index_ = -1;
        SetLastErrorLocked("No WAV/MP3 files found under /sdcard/music or /sdcard/recordings.");
        audio.ReleaseAudioFocus(kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
        return ESP_ERR_NOT_FOUND;
    }
    int previous = current_index_ < 0 ? 0 :
        (current_index_ - 1 + static_cast<int>(playlist_.size())) %
            static_cast<int>(playlist_.size());
    return StartPlaybackLocked(previous, 0);
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
    return status;
}

std::vector<MediaFile> MusicPlayer::GetPlaylist() {
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

esp_err_t MusicPlayer::StartPlaybackLocked(int index, uint32_t resume_offset) {
    if (index < 0 || index >= static_cast<int>(playlist_.size())) {
        SetLastErrorLocked("Track index is out of range.");
        return ESP_ERR_INVALID_ARG;
    }

    playback_generation_++;
    current_index_ = index;
    resume_offset_ = resume_offset;
    elapsed_seconds_ = 0;
    duration_seconds_ = 0;
    sample_rate_ = 0;
    channels_ = 0;
    last_error_.clear();
    state_ = MusicPlayerState::kPlaying;

    auto* arg = new PlaybackTaskArg();
    arg->player = this;
    arg->generation = playback_generation_;
    BaseType_t ret = xTaskCreate(&MusicPlayer::PlaybackTask, "music_player",
        4096, arg, 4, &playback_task_handle_);
    if (ret != pdPASS) {
        delete arg;
        playback_task_handle_ = nullptr;
        state_ = MusicPlayerState::kError;
        SetLastErrorLocked("Failed to create music playback task.");
        Application::GetInstance().GetAudioService().ReleaseAudioFocus(
            kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Playing %s track %d/%u: %s", TrackTypeName(playlist_[index]),
        index + 1, static_cast<unsigned>(playlist_.size()), playlist_[index].path.c_str());
    return ESP_OK;
}

void MusicPlayer::PauseFromFocusLoss() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == MusicPlayerState::kPlaying) {
        state_ = MusicPlayerState::kPaused;
        ESP_LOGI(TAG, "Local music paused by audio focus loss");
    }
}

void MusicPlayer::PlaybackLoop(uint32_t generation) {
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
        PlaybackMp3(generation, track, start_offset);
        return;
    }

    PlaybackWav(generation, track, start_offset);
}

void MusicPlayer::PlaybackWav(uint32_t generation, const MediaFile& track,
        uint32_t start_offset) {
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

        auto queue_result = QueuePcmChunk(generation, std::move(output), playback_offset,
            "Failed to queue WAV PCM.");
        if (queue_result != PlaybackLoopResult::kContinue) {
            fclose(file);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation == playback_generation_ && info.byte_rate > 0) {
                resume_offset_ = playback_offset;
                elapsed_seconds_ = static_cast<int>(playback_offset / info.byte_rate);
            }
        }
    }

    fclose(file);
    FinishPlayback(generation, MusicPlayerState::kIdle, "");
}

void MusicPlayer::PlaybackMp3(uint32_t generation, const MediaFile& track,
        uint32_t start_offset) {
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
    std::string error;

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
                        duration_seconds_ = info.bitrate > 0 ?
                            static_cast<int>((track.size_bytes * 8) / info.bitrate) : 0;
                    }
                }

                size_t decoded_samples = out_frame.decoded_size / sizeof(int16_t);
                size_t frames = decoded_samples / static_cast<size_t>(source_channels);
                std::vector<int16_t> output;
                if (!PrepareOutputChunk(reinterpret_cast<int16_t*>(out_frame.buffer), frames,
                        source_channels, output_channels, rate_converter, output, error)) {
                    break;
                }

                auto queue_result = QueuePcmChunk(generation, std::move(output),
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
                    elapsed_seconds_ = static_cast<int>(
                        decoded_source_frames / static_cast<uint64_t>(source_sample_rate));
                    if (duration_seconds_ == 0 && info.bitrate > 0) {
                        duration_seconds_ = static_cast<int>(
                            (track.size_bytes * 8) / info.bitrate);
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
    FinishPlayback(generation, MusicPlayerState::kIdle, "");
#else
    (void)track;
    (void)start_offset;
    FinishPlayback(generation, MusicPlayerState::kError,
        "MP3 playback requires CONFIG_AUDIO_DECODER_MP3_SUPPORT.");
#endif
}

MusicPlayer::PlaybackLoopResult MusicPlayer::QueuePcmChunk(uint32_t generation,
        std::vector<int16_t>&& output, uint32_t resume_offset,
        const std::string& error_on_cancel) {
    while (!Application::GetInstance().GetAudioService().PushPcmToPlaybackQueue(
            kAudioFocusOwnerLocalMusic, std::move(output), false)) {
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
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return PlaybackLoopResult::kContinue;
}

void MusicPlayer::FinishPlayback(uint32_t generation, MusicPlayerState state,
        const std::string& error) {
    bool release_focus = state == MusicPlayerState::kIdle || !error.empty();
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
        Application::GetInstance().GetAudioService().ReleaseAudioFocus(
            kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput);
    }
}

void MusicPlayer::SetLastErrorLocked(const std::string& error) {
    last_error_ = error;
}

void MusicPlayer::PlaybackTask(void* arg) {
    auto* task_arg = static_cast<PlaybackTaskArg*>(arg);
    MusicPlayer* player = task_arg->player;
    uint32_t generation = task_arg->generation;
    delete task_arg;
    player->PlaybackLoop(generation);
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
