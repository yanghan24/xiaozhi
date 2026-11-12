#pragma once

#include <string_view>

// Audio-input decisions shared by the application and host state-transition
// checks. Acoustic wake recognition remains in the hardware audio service.
struct SpeakingInputPolicy {
    bool process_voice;
    bool detect_wake_word;
};

inline SpeakingInputPolicy GetSpeakingInputPolicy(bool realtime, bool afe_wake_word,
                                                 bool wake_word_only) {
    if (wake_word_only) {
        return {false, afe_wake_word};
    }
    return {realtime, !realtime && afe_wake_word};
}

// The playback -> listening transition. Audio is the hardware seam; cancel
// sends the protocol abort and listen changes application state. Neither may
// wait for remote model output. The caller suppresses the listening popup.
template <typename Audio, typename Cancel, typename Listen>
bool InterruptPlaybackWithWakeWord(std::string_view word, Audio& audio,
                                   Cancel cancel, Listen listen) {
    if (word != "你好小智") {
        // Detectors stop themselves on any match. Keep the current answer.
        audio.EnableWakeWordDetection(true);
        return false;
    }
    cancel();
    while (audio.PopPacketFromSendQueue()) {}
    audio.ResetDecoder();
    listen();
    return true;
}
