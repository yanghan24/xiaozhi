#pragma once

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
