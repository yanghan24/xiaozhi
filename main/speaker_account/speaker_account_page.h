#pragma once

#include "speaker_account.h"
#include <lvgl.h>

class SpeakerAccountPage {
public:
    void Create(lv_obj_t* apps, lv_obj_t* system, lv_event_cb_t back, void* context);
    void Destroy();
    void Show();
    void Hide();
    void SetPromptsAllowed(bool allowed);
    void Apply(const SpeakerAccount::View& view);
    bool PromptVisible() const;

private:
    void Render();
    void RenderContent(bool prompt);
    static void OnUnbind(lv_event_t* event);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* apps_ = nullptr;
    lv_obj_t* system_ = nullptr;
    lv_draw_buf_t* qr_buffer_ = nullptr;
    lv_event_cb_t back_ = nullptr;
    void* back_context_ = nullptr;
    SpeakerAccount::View view_;
    bool requested_ = false;
    bool prompts_allowed_ = false;
    bool confirm_unbind_ = false;
};
