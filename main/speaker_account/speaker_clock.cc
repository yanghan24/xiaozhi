#include "speaker_clock.h"

#include <cJSON.h>
#include <sys/time.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace SpeakerClock {
namespace {

constexpr int64_t kEarliestTime = 1704067200;  // 2024-01-01
constexpr int64_t kLatestTime = 4102444800;    // 2100-01-01
constexpr int64_t kMaxOffsetMinutes = 18 * 60;
constexpr int64_t kCorrectionThresholdMs = 2000;

std::mutex sync_mutex;
std::atomic<bool> synchronized_once{false};

bool IsIntegerInRange(const cJSON* item, int64_t min, int64_t max) {
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const auto value = item->valuedouble;
    return value >= min && value <= max && std::trunc(value) == value;
}

}  // namespace

bool Synchronize(const cJSON* timestamp, const cJSON* offset) {
    if (!IsIntegerInRange(timestamp, kEarliestTime * 1000, kLatestTime * 1000 - 1) ||
        !IsIntegerInRange(offset, -kMaxOffsetMinutes, kMaxOffsetMinutes)) {
        return false;
    }

    // Keep the upstream local-clock convention used by the existing reminder dates.
    const auto local_ms = static_cast<int64_t>(timestamp->valuedouble) +
                          static_cast<int64_t>(offset->valuedouble) * 60000;
    const timeval target{
        .tv_sec = static_cast<std::time_t>(local_ms / 1000),
        .tv_usec = static_cast<suseconds_t>(local_ms % 1000) * 1000,
    };
    if (target.tv_sec < kEarliestTime || target.tv_sec >= kLatestTime) {
        return false;
    }

    std::lock_guard lock(sync_mutex);
    timeval current{};
    if (synchronized_once.load() && gettimeofday(&current, nullptr) == 0) {
        const auto delta_ms = local_ms - (static_cast<int64_t>(current.tv_sec) * 1000 + current.tv_usec / 1000);
        if (delta_ms > -kCorrectionThresholdMs && delta_ms < kCorrectionThresholdMs) {
            return true;
        }
    }
    if (settimeofday(&target, nullptr) != 0) {
        return false;
    }
    synchronized_once.store(true);
    return true;
}

std::time_t Now() {
    if (!synchronized_once.load()) {
        return 0;
    }
    const auto now = std::time(nullptr);
    return now >= kEarliestTime && now < kLatestTime ? now : 0;
}

}  // namespace SpeakerClock
