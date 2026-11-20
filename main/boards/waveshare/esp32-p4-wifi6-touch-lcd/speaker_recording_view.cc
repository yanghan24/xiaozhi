#include "speaker_recording_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "speaker_recorder.h"
#include <cstdio>

using namespace SpeakerHomeWidgets;

void SpeakerRecordingView::Create(lv_obj_t* parent) {
    root_ = Panel(parent);
    lv_obj_set_pos(Label(root_, "本地录音", kText), 18, 7);
    back_ = Button(root_, "返回", [](lv_event_t* event) {
        static_cast<SpeakerRecordingView*>(lv_event_get_user_data(event))->Hide();
    }, this);
    lv_obj_set_size(back_, 84, 40);
    lv_obj_set_pos(back_, 518, 4);

    auto* timer = Card(root_, 18, 72, 366, 356);
    lv_obj_set_pos(Label(timer, "录音时长", kCyan), 18, 15);
    Rule(timer, 18, 56, 326, 1, kCyan, LV_OPA_40);
    lv_obj_center(elapsed_.Create(timer, kCyan));
    auto* controls = Card(root_, 420, 72, 364, 356);
    lv_obj_set_pos(Label(controls, "保存到 SD 卡", kCyan), 18, 15);
    Rule(controls, 18, 56, 326, 1, kCyan, LV_OPA_40);
    status_ = Label(controls, "", kText);
    lv_obj_set_width(status_, 326);
    lv_obj_set_pos(status_, 18, 90);
    file_ = Label(controls, "", kMuted);
    lv_obj_set_width(file_, 326);
    lv_obj_set_pos(file_, 18, 155);
    lv_label_set_long_mode(file_, LV_LABEL_LONG_DOT);
    start_ = Button(controls, "开始录音", [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerRecorder::Get().Start(); });
    }, nullptr);
    stop_ = Button(controls, "停止并保存", [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerRecorder::Get().Stop(); });
    }, nullptr);
    lv_obj_set_size(start_, 146, 48);
    lv_obj_set_pos(start_, 18, 250);
    lv_obj_set_size(stop_, 146, 48);
    lv_obj_set_pos(stop_, 196, 250);
    Hide();
}

void SpeakerRecordingView::Show() {
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    Update();
}

void SpeakerRecordingView::Hide() {
    if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerRecordingView::Update() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) return;
    const auto snapshot = SpeakerRecorder::Get().GetSnapshot();
    const bool capturing = snapshot.state == SpeakerRecorder::State::Starting ||
            snapshot.state == SpeakerRecorder::State::Recording;
    const bool busy = capturing || snapshot.state == SpeakerRecorder::State::Saving;
    lv_obj_set_state(back_, LV_STATE_DISABLED, busy);
    lv_obj_set_state(start_, LV_STATE_DISABLED, busy);
    lv_obj_set_state(stop_, LV_STATE_DISABLED, !capturing);
    const bool show_hours = snapshot.seconds >= 100 * 60;
    const auto major = show_hours ? snapshot.seconds / 3600 : snapshot.seconds / 60;
    const auto minor = show_hours ? snapshot.seconds / 60 % 60 : snapshot.seconds % 60;
    char time[16];
    std::snprintf(time, sizeof(time), "%02u:%02u", static_cast<unsigned>(major), static_cast<unsigned>(minor));
    elapsed_.SetTime(time);
    const char* status = "";
    switch (snapshot.state) {
        case SpeakerRecorder::State::Idle: status = "等待录音"; break;
        case SpeakerRecorder::State::Starting: status = "正在准备 SD 卡"; break;
        case SpeakerRecorder::State::Recording: status = "录音中，语音唤醒已暂停"; break;
        case SpeakerRecorder::State::Saving: status = "正在保存"; break;
        case SpeakerRecorder::State::Saved: status = "已保存，可在音乐页播放"; break;
        case SpeakerRecorder::State::NoCard: status = "未检测到可用 SD 卡"; break;
        case SpeakerRecorder::State::Failed: status = "录音失败，请检查 SD 卡"; break;
    }
    UpdateLabel(status_, status);
    UpdateLabel(file_, snapshot.file.c_str());
}

#endif
