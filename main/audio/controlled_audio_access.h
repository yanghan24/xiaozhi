#pragma once

#include <atomic>
#include <cstdint>

// A nonblocking revocation fence. A stop is complete only after every PCM write
// admitted before revocation has returned. Generations must never be reused.
class ControlledAudioAccess {
public:
    void Grant(uint64_t generation) {
        generation_.store(generation);
        allowed_.store(true);
    }

    void Revoke(uint64_t generation) {
        allowed_.store(false);
        generation_.store(generation);
    }

    bool IsCurrent(uint64_t generation) const {
        return allowed_.load() && generation_.load() == generation;
    }

    bool TryBeginOutput(uint64_t generation) {
        output_in_flight_.fetch_add(1);
        if (IsCurrent(generation)) {
            return true;
        }
        output_in_flight_.fetch_sub(1);
        return false;
    }

    void EndOutput() { output_in_flight_.fetch_sub(1); }
    bool IsStopped() const { return output_in_flight_.load() == 0; }

private:
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> allowed_{false};
    std::atomic<unsigned> output_in_flight_{0};
};
