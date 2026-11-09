#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// The processor serializes buffer access, then delivers detached frames without
// holding its lock. Both delayed fetches and callbacks retain their source.
class ProcessingAudioBuffer {
public:
    uint64_t Start(uint64_t source_generation) {
        samples_.clear();
        running_ = true;
        source_generation_ = source_generation;
        return ++generation_;
    }

    void Stop() {
        running_ = false;
        ++generation_;
        samples_.clear();
    }

    uint64_t GetGeneration() const { return running_ ? generation_ : 0; }
    uint64_t GetSourceGeneration() const { return source_generation_; }

    void Append(uint64_t generation, const int16_t* samples, size_t count) {
        if (running_ && generation == generation_) {
            samples_.insert(samples_.end(), samples, samples + count);
        }
    }

    bool PopFrame(uint64_t generation, size_t frame_samples, std::vector<int16_t>& frame) {
        uint64_t source_generation;
        return PopFrame(generation, frame_samples, frame, source_generation);
    }

    bool PopFrame(uint64_t generation, size_t frame_samples, std::vector<int16_t>& frame,
                  uint64_t& source_generation) {
        if (!running_ || generation != generation_ || frame_samples == 0 ||
            samples_.size() < frame_samples) {
            return false;
        }
        frame.assign(samples_.begin(), samples_.begin() + frame_samples);
        source_generation = source_generation_;
        samples_.erase(samples_.begin(), samples_.begin() + frame_samples);
        return true;
    }

private:
    uint64_t generation_ = 0;
    uint64_t source_generation_ = 0;
    bool running_ = false;
    std::vector<int16_t> samples_;
};
