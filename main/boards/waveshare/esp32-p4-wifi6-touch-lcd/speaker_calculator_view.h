#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_calculator.h"
#include "speaker_navigation.h"

class SpeakerCalculatorView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void Show();
    void Hide();

private:
    struct KeyAction {
        SpeakerCalculatorView* view;
        char key;
        uint32_t codepoint;
    };

    void Press(char key);
    void Render();
    void AddHistory(const std::string& result);

    SpeakerCalculator calculator_;
    std::array<KeyAction, 19> actions_{};
    std::array<std::string, 6> history_;
    size_t history_count_ = 0;
    size_t next_history_ = 0;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* expression_ = nullptr;
    lv_obj_t* result_ = nullptr;
    lv_obj_t* history_scroll_ = nullptr;
    lv_obj_t* history_label_ = nullptr;
};

#endif
