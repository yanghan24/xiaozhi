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

namespace {
constexpr int64_t kSecondUs = 1000000;
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
    lv_obj_align(state_label_, LV_ALIGN_CENTER, -191, 68);

    work_ = Button(panel_, "专注 25 分钟", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->Reset(25);
    }, this);
    rest_ = Button(panel_, "休息 5 分钟", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->Reset(5);
    }, this);
    for (auto* mode : {work_, rest_}) {
        lv_obj_set_style_bg_opa(mode, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_size(mode, 271, 62);
    }
    lv_obj_set_pos(work_, 454, 117);
    lv_obj_set_pos(rest_, 454, 192);
    auto* action = Button(panel_, "开始", [](lv_event_t* event) {
        static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event))->Toggle();
    }, this, kCyan, kDeep);
    action_label_ = lv_obj_get_child(action, 0);
    lv_obj_set_size(action, 178, 73);
    lv_obj_set_pos(action, 453, 301);
    auto* reset = Button(panel_, "重置", [](lv_event_t* event) {
        auto* view = static_cast<SpeakerPomodoroView*>(lv_event_get_user_data(event));
        view->Reset(view->minutes_);
    }, this);
    Outline(reset);
    lv_obj_set_size(reset, 111, 73);
    lv_obj_set_pos(reset, 646, 301);
    Reset(minutes_);
}

void SpeakerPomodoroView::Show() {
    lv_obj_remove_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    Update();
}

void SpeakerPomodoroView::Hide() {
    lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerPomodoroView::Reset(int minutes) {
    minutes_ = minutes;
    remaining_us_ = minutes * 60 * kSecondUs;
    state_ = State::Ready;
    sound_pending_ = false;
    displayed_seconds_ = -1;
    lv_obj_set_style_outline_width(work_, minutes == 25 ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_outline_color(work_, lv_color_hex(0x27E5FF), LV_PART_MAIN);
    lv_obj_set_style_outline_width(rest_, minutes == 5 ? 2 : 0, LV_PART_MAIN);
    lv_obj_set_style_outline_color(rest_, lv_color_hex(0x27E5FF), LV_PART_MAIN);
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

void SpeakerPomodoroView::DispatchSound() {
    if (sound_scheduled_.exchange(true)) {
        return;
    }
    Application::GetInstance().Schedule([this] {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateIdle && app.GetAudioService().IsPlaybackIdle()
            && sound_pending_.exchange(false)) {
            app.PlaySound(Lang::Sounds::OGG_POPUP);
        }
        sound_scheduled_ = false;
    });
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
    switch (state_) {
        case State::Ready:
            lv_label_set_text(state_label_, "准备开始");
            lv_label_set_text(action_label_, "开始");
            break;
        case State::Running:
            lv_label_set_text(state_label_, "计时中");
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
