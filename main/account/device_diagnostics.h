#pragma once

#include "device_state.h"
#include <string>

namespace account::diagnostics {
enum class Event { Boot, State, VoiceNetworkError, OtaCheckFailed, OtaUpgradeFailed, HeartbeatFailed };

void Initialize();
void ObserveState(DeviceState state);
void Record(Event event, int code = 0);

// The account worker owns the diagnostic lease and the pending HTTP batch.
void ApplyHeartbeat(const std::string& response);
const std::string& PendingReport();
void CompleteReport(int status);
}
