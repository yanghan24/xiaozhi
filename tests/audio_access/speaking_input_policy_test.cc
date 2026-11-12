#include "speaking_input_policy.h"

#include <cassert>
#include <iostream>

int main() {
    // AEC/realtime must not turn off the keyword detector during playback.
    auto speaking = GetSpeakingInputPolicy(true, true, true);
    assert(!speaking.process_voice);
    assert(speaking.detect_wake_word);

    // The same playback behavior applies without realtime AEC.
    speaking = GetSpeakingInputPolicy(false, true, true);
    assert(!speaking.process_voice && speaking.detect_wake_word);

    // Unsupported detectors must never be started concurrently with output.
    speaking = GetSpeakingInputPolicy(true, false, true);
    assert(!speaking.process_voice && !speaking.detect_wake_word);

    // Opting out keeps the original realtime behavior.
    speaking = GetSpeakingInputPolicy(true, true, false);
    assert(speaking.process_voice && !speaking.detect_wake_word);
    speaking = GetSpeakingInputPolicy(false, true, false);
    assert(!speaking.process_voice && speaking.detect_wake_word);
    std::cout << "PASS: speaking input policy with/without AEC and wake detector\n";
}
