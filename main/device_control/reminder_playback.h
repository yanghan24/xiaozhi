#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

// A single, dedicated connection owns each delivery. The WebSocket receive task
// observes the frame boundaries and the main task compares them to codec writes.
class ReminderPlayback {
public:
    void Begin(uint64_t connection, const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_ = connection;
        id_ = id;
        phase_ = Phase::Claimed;
        received_ = 0;
        expected_ = 0;
    }

    void Observe(uint64_t connection, std::string_view type, std::string_view state,
                 std::string_view id, int frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ != connection || !connection || phase_ == Phase::Failed)
            return;
        if (type == "reminder") {
            if (id == id_ && state == "start" && phase_ == Phase::Claimed) {
                phase_ = Phase::Started;
            } else if (id == id_ && state == "end" && phase_ == Phase::Stopped &&
                       frames > 0 && frames == received_) {
                phase_ = Phase::Ended;
                expected_ = frames;
            } else {
                phase_ = Phase::Failed;
            }
        } else if (type == "tts" && state == "start" && phase_ == Phase::Started) {
            phase_ = Phase::Playing;
        } else if (type == "tts" && state == "stop" && phase_ == Phase::Playing) {
            phase_ = Phase::Stopped;
        }
    }

    bool ReceiveAudio(uint64_t connection) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ != connection || !connection)
            return false;
        if (phase_ != Phase::Playing) {
            phase_ = Phase::Failed;
            return false;
        }
        ++received_;
        return true;
    }

    // Negative means cancel. Zero means the stream has not ended.
    int ExpectedFrames() {
        std::lock_guard<std::mutex> lock(mutex_);
        return phase_ == Phase::Failed ? -1 : expected_;
    }

private:
    enum class Phase { Claimed, Started, Playing, Stopped, Ended, Failed };
    std::mutex mutex_;
    std::string id_;
    uint64_t connection_ = 0;
    Phase phase_ = Phase::Claimed;
    int received_ = 0;
    int expected_ = 0;
};
