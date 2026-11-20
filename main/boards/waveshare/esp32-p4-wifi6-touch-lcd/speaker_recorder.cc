#include "speaker_recorder.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "speaker_sd_storage.h"
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kByteRate = 2 * kSampleRate;

std::array<uint8_t, 44> WavHeader(uint32_t size) {
    std::array<uint8_t, 44> header{
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0,
    };
    const auto write = [&](unsigned offset, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) header[offset + i] = value >> (8 * i);
    };
    write(4, 36 + size);
    write(24, kSampleRate);
    write(28, kByteRate);
    write(40, size);
    return header;
}

std::string RecordingName() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char timestamp[32];
    if (!std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &local)) return {};
    for (unsigned suffix = 0; suffix < 100; ++suffix) {
        std::string name = "REC-" + std::string(timestamp) + "-" + std::to_string(suffix) + ".wav";
        const std::string target = std::string(SpeakerSdStorage::Recordings()) + "/" + name;
        if (access(target.c_str(), F_OK) != 0 && access((target + ".part").c_str(), F_OK) != 0) {
            return name;
        }
    }
    return {};
}
}  // namespace

SpeakerRecorder& SpeakerRecorder::Get() {
    static SpeakerRecorder recorder;
    return recorder;
}

SpeakerRecorder::Snapshot SpeakerRecorder::GetSnapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void SpeakerRecorder::Start() {
    std::lock_guard lock(mutex_);
    if (snapshot_.state == State::Starting || snapshot_.state == State::Recording ||
        snapshot_.state == State::Saving) return;
    if (!queue_) queue_ = xQueueCreate(32, sizeof(Frame));
    if (queue_ && !task_) {
        xTaskCreate([](void* self) { static_cast<SpeakerRecorder*>(self)->Run(); },
                    "sd_record", 6144, this, 2, &task_);
    }
    if (!task_) {
        snapshot_ = {State::Failed, 0, {}};
        return;
    }
    xQueueReset(queue_);
    stop_ = overflow_ = false;
    auto& app = Application::GetInstance();
    if (!app.BeginLocalRecording([this](const int16_t* pcm, size_t size, int channels) {
            Push(pcm, size, channels);
        })) {
        snapshot_ = {State::Failed, 0, {}};
        return;
    }
    snapshot_ = {State::Starting, 0, {}};
    xTaskNotifyGive(task_);
}

void SpeakerRecorder::Stop() {
    {
        std::lock_guard lock(mutex_);
        if (snapshot_.state != State::Starting && snapshot_.state != State::Recording) return;
        snapshot_.state = State::Saving;
        accepting_ = false;
        stop_ = true;
    }
    Application::GetInstance().EndLocalRecording();
}

void SpeakerRecorder::Push(const int16_t* pcm, size_t size, int channels) {
    if (!accepting_ || stop_ || overflow_ || channels <= 0) return;
    Frame frame{};
    if (size != frame.size() * static_cast<size_t>(channels)) {
        overflow_ = true;
        return;
    }
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = pcm[i * channels];
    if (xQueueSend(queue_, &frame, 0) != pdTRUE) overflow_ = true;
}

void SpeakerRecorder::Run() {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Record();
    }
}

void SpeakerRecorder::Record() {
    if (!SpeakerSdStorage::Get().Mount()) {
        Finish(stop_ ? State::Idle : State::NoCard, 0);
        return;
    }
    if (stop_) {
        Finish(State::Idle, 0);
        return;
    }
    const std::string name = RecordingName();
    if (name.empty()) {
        Finish(State::Failed, 0);
        return;
    }
    const std::string final_path = std::string(SpeakerSdStorage::Recordings()) + "/" + name;
    const std::string temporary = final_path + ".part";
    const int fd = open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0664);
    FILE* file = fd >= 0 ? fdopen(fd, "wb") : nullptr;
    if (!file) {
        if (fd >= 0) {
            close(fd);
            unlink(temporary.c_str());
        }
        Finish(State::Failed, 0);
        return;
    }

    const auto placeholder = WavHeader(0);
    bool success = fwrite(placeholder.data(), 1, placeholder.size(), file) == placeholder.size();
    {
        std::lock_guard lock(mutex_);
        if (!stop_ && success) {
            snapshot_.state = State::Recording;
            accepting_ = true;
        }
    }
    uint32_t bytes = 0;
    Frame frame;
    while (success && !overflow_) {
        const bool capturing = Application::GetInstance().GetAudioService().IsLocalCaptureRunning();
        const bool reading = accepting_ && !stop_ && capturing;
        if (xQueueReceive(queue_, &frame, reading ? pdMS_TO_TICKS(50) : 0) != pdTRUE) {
            if (reading) continue;
            break;
        }
        constexpr uint32_t frame_bytes = sizeof(Frame);
        if (bytes > std::numeric_limits<uint32_t>::max() - 36 - frame_bytes ||
            fwrite(frame.data(), 1, frame_bytes, file) != frame_bytes) {
            success = false;
            break;
        }
        bytes += frame_bytes;
        if (bytes % kByteRate == 0) {
            std::lock_guard lock(mutex_);
            snapshot_.seconds = bytes / kByteRate;
        }
    }
    accepting_ = false;
    {
        std::lock_guard lock(mutex_);
        snapshot_.state = State::Saving;
    }
    const auto header = WavHeader(bytes);
    success = success && !overflow_ && fseek(file, 0, SEEK_SET) == 0 &&
            fwrite(header.data(), 1, header.size(), file) == header.size() && fflush(file) == 0 &&
            fsync(fileno(file)) == 0;
    if (fclose(file) != 0) success = false;
    if (success && bytes > 0 && rename(temporary.c_str(), final_path.c_str()) == 0) {
        Finish(State::Saved, bytes / kByteRate, name);
    } else {
        unlink(temporary.c_str());
        Finish(success && bytes == 0 && stop_ ? State::Idle : State::Failed, 0);
    }
}

void SpeakerRecorder::Finish(State state, uint32_t seconds, std::string filename) {
    accepting_ = false;
    Application::GetInstance().Schedule([this, state, seconds, filename = std::move(filename)] {
        Application::GetInstance().EndLocalRecording();
        std::lock_guard lock(mutex_);
        snapshot_ = {state, seconds, filename};
    });
}

#endif
