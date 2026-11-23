#pragma once

#include <ctime>

struct cJSON;

namespace SpeakerClock {

// Both OTA and the authenticated heartbeat provide UTC milliseconds and an offset in minutes.
bool Synchronize(const cJSON* timestamp, const cJSON* offset);

// Returns zero until a valid time sample has been applied in this boot.
std::time_t Now();

}  // namespace SpeakerClock
