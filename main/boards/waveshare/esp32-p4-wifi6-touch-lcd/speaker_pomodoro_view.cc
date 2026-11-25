#include "speaker_pomodoro_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "display.h"
#include "speaker_account/speaker_ui_widgets.h"

#include <esp_timer.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>

namespace {
constexpr int64_t kSecondUs = 1000000;
constexpr int kMinMinutes = 5;
constexpr int kStepMinutes = 5;
constexpr int kMaxMinutes = 60;
constexpr int kFocusMinutes = 25;
constexpr int kRestMinutes = 5;

int MinutesToIndex(int minutes) {
    return std::clamp((minutes + kStepMinutes / 2) / kStepMinutes, kMinMinutes / kStepMinutes,
                      kMaxMinutes / kStepMinutes);
}

int IndexToMinutes(int index) {
    return std::clamp(index, kMinMinutes / kStepMinutes, kMaxMinutes / kStepMinutes) * kStepMinutes;
}
}

using namespace SpeakerUi;

void SpeakerPomodoroView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    panel_ = Panel(parent);
    Header(panel_, "番茄钟", SpeakerNavigation::OnBack, &navigation);
    Rule(panel_, 416, 105, 1, 316);
#if LV_USE_ARC
    progress_ = lv_arc_create(panel_);
    lv_obj_set_size(progress_, 318, 318);
    lv_obj_set_pos(progress_, 50, 99);
    lv_arc_set_rotation(progress_, 270);
    lv_arc_set_bg_angles(progress_, 0, 360);
    lv_arc_set_range(progress_, 0, 1000);
    lv_obj_remove_flag(progress_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE));
    lv_obj_set_style_arc_color(progress_, lv_color_hex(0x18376B), LV_PART_MAIN);
    lv_obj_set_style_arc_color(progress_, lv_color_hex(kCyan), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_, 16, LV_PART_INDICATOR);
    lv_obj_remove_style(progress_, nullptr, LV_PART_KNOB);
#endif
    clock_ = Digits(panel_, "25:00", 211, 321);
    lv_obj_align(clock_, LV_ALIGN_CENTER, -191, 11);
    state_label_ = Label(panel_, "", kMuted);
    Scale(state_label_, 320);
    lv_obj_align(state_label_, LV_ALIGN_CENTER, -191, 62);

    work_ = Button(panel_, "专注", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->SetMode(Mode::Focus);
    }, this);
    rest_ = Button(panel_, "休息", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->SetMode(Mode::Rest);
    }, this);
    for (auto* mode : {work_, rest_}) {
        lv_obj_set_size(mode, 146, 52);
        lv_obj_set_style_radius(mode, 14, LV_PART_MAIN);
        Scale(lv_obj_get_child(mode, 0), 320);
    }
    lv_obj_set_pos(work_, 454, 124);
    lv_obj_set_pos(rest_, 609, 124);

    minus_ = Button(panel_, "-", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->ChangeMinutes(-kStepMinutes);
    }, this);
    plus_ = Button(panel_, "+", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->ChangeMinutes(kStepMinutes);
    }, this);
    for (auto* step : {minus_, plus_}) {
        lv_obj_set_size(step, 59, 55);
        lv_obj_set_style_radius(step, 14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(step, 0), lv_color_hex(kMuted), LV_PART_MAIN);
        Scale(lv_obj_get_child(step, 0), 320);
    }
    lv_obj_set_pos(minus_, 454, 204);
    lv_obj_set_pos(plus_, 696, 204);
    minutes_label_ = Label(panel_, "25 分钟", kText);
    Scale(minutes_label_, 320);
    lv_obj_set_width(minutes_label_, 183);
    lv_obj_set_style_text_align(minutes_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(minutes_label_, 513, 215);

    slider_ = lv_slider_create(panel_);
    lv_obj_set_pos(slider_, 469, 284);
    lv_obj_set_size(slider_, 272, 20);
    lv_slider_set_range(slider_, kMinMinutes / kStepMinutes, kMaxMinutes / kStepMinutes);
    lv_obj_set_style_pad_top(slider_, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(slider_, 7, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(0x1B2F5A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider_, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(kCyan), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(0x9DB3D0), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider_, 12, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_, [](lv_event_t* event) {
        auto* view = static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event));
        if (view->adjusting_) {
            return;
        }
        const int minutes = IndexToMinutes(lv_slider_get_value(view->slider_));
        if (minutes != view->minutes_) {
            view->SetMinutes(minutes);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* action = Button(panel_, "开始", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->Toggle();
    }, this, kCyan, kDeep);
    action_label_ = lv_obj_get_child(action, 0);
    Scale(action_label_, 320);
    lv_obj_set_size(action, 176, 66);
    lv_obj_set_pos(action, 454, 346);
    auto* reset = Button(panel_, "重置", [](lv_event_t* event) {
        auto* view = static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event));
        view->Reset(view->minutes_);
    }, this);
    Outline(reset);
    Scale(lv_obj_get_child(reset, 0), 320);
    lv_obj_set_size(reset, 114, 66);
    lv_obj_set_pos(reset, 641, 346);
    RefreshMode();
    SetMinutes(minutes_);
}

void SpeakerPomodoroView::Show() {
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    Update();
}

void SpeakerPomodoroView::Hide() {
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerPomodoroView::RefreshMode() {
    const bool focus = mode_ == Mode::Focus;
    lv_obj_set_style_bg_color(work_, lv_color_hex(kBlue), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(work_, focus ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(work_, focus ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(work_, lv_color_hex(focus ? 0x3D6FE0 : kEdge), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(work_, 0), lv_color_hex(focus ? kText : kMuted), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rest_, lv_color_hex(kBlue), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rest_, focus ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rest_, focus ? 1 : 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(rest_, lv_color_hex(focus ? kEdge : 0x3D6FE0), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(rest_, 0), lv_color_hex(focus ? kMuted : kText), LV_PART_MAIN);
}

void SpeakerPomodoroView::SetMode(Mode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    RefreshMode();
    SetMinutes(mode == Mode::Focus ? kFocusMinutes : kRestMinutes);
}

void SpeakerPomodoroView::SetMinutes(int minutes) {
    const int index = MinutesToIndex(minutes);
    char text[24];
    std::snprintf(text, sizeof(text), "%d 分钟", IndexToMinutes(index));
    UpdateLabel(minutes_label_, text);
    adjusting_ = true;
    lv_slider_set_value(slider_, index, LV_ANIM_OFF);
    adjusting_ = false;
    Reset(IndexToMinutes(index));
}

void SpeakerPomodoroView::ChangeMinutes(int delta) {
    SetMinutes(minutes_ + delta);
}

void SpeakerPomodoroView::Reset(int minutes) {
    minutes_ = minutes;
    remaining_us_ = minutes * 60 * kSecondUs;
    state_ = State::Ready;
    StopAlarm();
    displayed_seconds_ = -1;
    Render(esp_timer_get_time());
}

void SpeakerPomodoroView::Toggle() {
    const bool was_running = state_ == State::Running;
    Update();
    if (was_running && state_ == State::Finished) {
        return;
    }
    const auto now = esp_timer_get_time();
    if (state_ == State::Running) {
        remaining_us_ = std::max<int64_t>(0, deadline_us_ - now);
        state_ = State::Paused;
    } else {
        if (state_ == State::Finished) {
            Reset(minutes_);
        }
        deadline_us_ = now + remaining_us_;
        state_ = State::Running;
    }
    Render(now);
}

void SpeakerPomodoroView::Update() {
    const auto now = esp_timer_get_time();
    if (state_ == State::Running && now >= deadline_us_) {
        remaining_us_ = 0;
        state_ = State::Finished;
        ++alarm_epoch_;
        sound_pending_ = true;
        next_sound_check_us_ = now;
        Application::GetInstance().Schedule([] {
            Board::GetInstance().GetDisplay()->ShowNotification("番茄钟时间到");
        });
    }
    if (sound_pending_ && now >= next_sound_check_us_
        && Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        next_sound_check_us_ = now + kSecondUs;
        DispatchSound();
    }
    if (!lv_obj_has_flag(panel_, LV_OBJ_FLAG_HIDDEN)) {
        Render(now);
    }
}

void SpeakerPomodoroView::StopAlarm() {
    ++alarm_epoch_;
    sound_pending_ = false;
    const auto generation = alarm_generation_.exchange(0);
    if (generation) {
        Application::GetInstance().GetAudioService().CancelLocalPlaybackIfCurrent(generation);
    }
}

void SpeakerPomodoroView::DispatchSound() {
    if (sound_scheduled_.exchange(true)) return;
    if (xTaskCreate(&SpeakerPomodoroView::AlarmTask, "pomodoro_alarm", 4096, this, 2, nullptr) != pdPASS) {
        sound_scheduled_ = false;
    }
}

void SpeakerPomodoroView::AlarmTask(void* context) {
    auto* view = static_cast<SpeakerPomodoroView*>(context);
    view->RunAlarm();
    view->sound_scheduled_ = false;
    vTaskDelete(nullptr);
}

void SpeakerPomodoroView::RunAlarm() {
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();
    const uint32_t epoch = alarm_epoch_;
    if (!sound_pending_ || app.GetDeviceState() != kDeviceStateIdle || audio.IsLocalCaptureRunning()) return;

    const uint32_t generation = audio.TryBeginLocalPlayback();
    if (!generation) return;
    alarm_generation_ = generation;
    if (!sound_pending_ || alarm_epoch_ != epoch) {
        if (alarm_generation_.exchange(0) == generation) audio.CancelLocalPlaybackIfCurrent(generation);
        return;
    }
    const auto active = [&] {
        return sound_pending_ && alarm_epoch_ == epoch && app.GetDeviceState() == kDeviceStateIdle
            && !audio.IsLocalCaptureRunning() && audio.IsLocalPlaybackCurrent(generation);
    };
    const int rate = audio.LocalOutputSampleRate();
    constexpr int kChunkMs = 20;
    constexpr int kBeepMs = 120;
    constexpr int kGapMs = 120;
    constexpr int kGroupGapMs = 700;
    const int samples = rate * kChunkMs / 1000;
    int phase = 0;
    for (int group = 0; group < 10 && active(); ++group) {
        for (int beep = 0; beep < 3 && active(); ++beep) {
            for (int ms = 0; ms < kBeepMs + kGapMs && active(); ms += kChunkMs) {
                if (app.GetDeviceState() != kDeviceStateIdle || audio.IsLocalCaptureRunning()) break;
                std::vector<int16_t> pcm(samples, 0);
                if (ms < kBeepMs) {
                    for (int i = 0; i < samples; ++i, ++phase) {
                        const int pos = ms * rate / 1000 + i;
                        const int fade = rate / 200;
                        const float gain = std::min(1.0f, std::min(static_cast<float>(pos) / fade,
                            static_cast<float>(kBeepMs * rate / 1000 - pos) / fade));
                        pcm[i] = static_cast<int16_t>(7000 * gain * std::sin(6.28318530718 * 1000 * phase / rate));
                    }
                }
                if (!audio.QueueLocalPcm(generation, std::move(pcm))) break;
            }
        }
        for (int ms = 0; group < 9 && ms < kGroupGapMs && active(); ms += kChunkMs) {
            if (app.GetDeviceState() != kDeviceStateIdle || audio.IsLocalCaptureRunning()
                || !audio.QueueLocalPcm(generation, std::vector<int16_t>(samples, 0))) break;
        }
        if (app.GetDeviceState() != kDeviceStateIdle || audio.IsLocalCaptureRunning()) break;
    }
    while (active() && !audio.IsPlaybackIdle()) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    audio.CancelLocalPlaybackIfCurrent(generation);
    uint32_t expected = generation;
    alarm_generation_.compare_exchange_strong(expected, 0);
    if (alarm_epoch_ == epoch) sound_pending_ = false;
}

void SpeakerPomodoroView::Render(int64_t now) {
    const auto remaining = state_ == State::Running ? std::max<int64_t>(0, deadline_us_ - now)
                                                    : remaining_us_;
    const auto seconds = static_cast<int>((remaining + kSecondUs - 1) / kSecondUs);
    if (seconds != displayed_seconds_) {
        displayed_seconds_ = seconds;
        char time[16];
        std::snprintf(time, sizeof(time), "%02d:%02d", seconds / 60, seconds % 60);
        UpdateLabel(clock_, time);
#if LV_USE_ARC
        const int total = minutes_ * 60;
        lv_arc_set_value(progress_, (total - seconds) * 1000 / total);
#endif
    }
    if (state_ == displayed_state_) {
        return;
    }
    displayed_state_ = state_;
    const char* running = mode_ == Mode::Focus ? "专注中" : "休息中";
    switch (state_) {
        case State::Ready:
            lv_label_set_text(state_label_, "准备开始");
            lv_label_set_text(action_label_, "开始");
            break;
        case State::Running:
            lv_label_set_text(state_label_, running);
            lv_label_set_text(action_label_, "暂停");
            break;
        case State::Paused:
            lv_label_set_text(state_label_, "已暂停");
            lv_label_set_text(action_label_, "继续");
            break;
        case State::Finished:
            lv_label_set_text(state_label_, "时间到");
            lv_label_set_text(action_label_, "再来一次");
            break;
    }
}

#endif
