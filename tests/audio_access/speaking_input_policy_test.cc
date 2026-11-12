#include "speaking_input_policy.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

struct AudioHardware {
    std::vector<std::string>& events;
    int pending_input = 2;
    bool playing = true;
    void EnableWakeWordDetection(bool enable) {
        assert(enable);
        events.push_back("keyword_detector_on");
    }
    bool PopPacketFromSendQueue() {
        if (pending_input == 0) return false;
        --pending_input;
        events.push_back("discard_input");
        return true;
    }
    void ResetDecoder() {
        playing = false;
        events.push_back("clear_playback");
    }
};

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

    std::vector<std::string> events;
    AudioHardware audio{events};
    auto abort = [&]() { events.push_back("abort:wake_word_detected"); };
    auto listen = [&]() {
        assert(!audio.playing && audio.pending_input == 0);
        events.push_back("listen.start");
    };
    // Other detector matches rearm keyword input without stopping the answer.
    assert(!InterruptPlaybackWithWakeWord("小爱同学", audio, abort, listen));
    assert(audio.playing);
    assert((events == std::vector<std::string>{"keyword_detector_on"}));
    events.clear();
    assert(InterruptPlaybackWithWakeWord("你好小智", audio, abort, listen));
    assert((events == std::vector<std::string>{
        "abort:wake_word_detected", "discard_input", "discard_input",
        "clear_playback", "listen.start"}));
    // A second interruption is safe with empty queues; no popup/greeting action.
    events.clear();
    assert(InterruptPlaybackWithWakeWord("你好小智", audio, abort, listen));
    assert((events == std::vector<std::string>{
        "abort:wake_word_detected", "clear_playback", "listen.start"}));
    std::cout << "PASS: playback input policy and keyword cancellation -> listening\n";
}
