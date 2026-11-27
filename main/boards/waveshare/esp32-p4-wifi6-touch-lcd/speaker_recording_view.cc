#include "speaker_recording_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "speaker_account/speaker_ui_widgets.h"
#include "speaker_recorder.h"

#include <cmath>
#include <cstdio>

using namespace SpeakerUi;

namespace {
constexpr int kContentShiftUp = 25;
constexpr int kWaveCenterY = 270 - kContentShiftUp;

bool Busy(SpeakerRecorder::State state) {
    return state == SpeakerRecorder::State::Starting || state == SpeakerRecorder::State::Recording ||
           state == SpeakerRecorder::State::Saving;
}
}

void SpeakerRecordingView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    root_ = Panel(parent);
    back_ = Header(root_, "录音", SpeakerNavigation::OnBack, &navigation);
    auto* ring = Surface(root_, 86, 141 - kContentShiftUp, 171, 171);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, lv_color_hex(0x9E6E8E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ring, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ring, LV_OPA_40, LV_PART_MAIN);
    lv_obj_center(Icon(ring, MATERIAL_SYMBOLS_MIC, kText, 625));

    elapsed_ = Digits(root_, "00:00", 244, 404);
    lv_obj_align(elapsed_, LV_ALIGN_CENTER, 124, -53 - kContentShiftUp);
    for (size_t i = 0; i < bars_.size(); ++i) {
        auto* bar = Surface(root_, 352 + static_cast<int>(i) * 13, kWaveCenterY - 1, 5, 7);
        lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(i < 11 ? kWarm : 0x4D6CAB), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        bars_[i] = bar;
    }

    status_ = Label(root_, "", kMuted);
    lv_obj_align(status_, LV_ALIGN_BOTTOM_MID, 0, -126 - kContentShiftUp);
    file_ = Label(root_, "", kMuted);
    lv_obj_set_width(file_, 670);
    lv_obj_set_style_text_align(file_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(file_, LV_LABEL_LONG_DOT);
    lv_obj_align(file_, LV_ALIGN_BOTTOM_MID, 0, -14);

    start_ = Button(root_, "开始录音", [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerRecorder::Get().Start(); });
    }, nullptr, kCyan, kDeep);
    stop_ = Button(root_, "停止录音", [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerRecorder::Get().Stop(); });
    }, nullptr, kCyan, kDeep);
    for (auto* button : {start_, stop_}) {
        lv_obj_set_size(button, 402, 71);
        lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -48 - kContentShiftUp);
        lv_obj_align(lv_obj_get_child(button, 0), LV_ALIGN_CENTER, 0, 4);
    }
}

void SpeakerRecordingView::Show() {
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    Update();
}

void SpeakerRecordingView::Hide() {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

bool SpeakerRecordingView::CanLeave() const {
    return !Busy(SpeakerRecorder::Get().GetSnapshot().state);
}

void SpeakerRecordingView::Update() {
    if (lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const auto snapshot = SpeakerRecorder::Get().GetSnapshot();
    const bool capturing = snapshot.state == SpeakerRecorder::State::Starting ||
                           snapshot.state == SpeakerRecorder::State::Recording;
    const bool busy = Busy(snapshot.state);
    lv_obj_set_state(back_, LV_STATE_DISABLED, busy);
    lv_obj_set_state(start_, LV_STATE_DISABLED, busy);
    lv_obj_set_flag(start_, LV_OBJ_FLAG_HIDDEN, capturing);
    lv_obj_set_flag(stop_, LV_OBJ_FLAG_HIDDEN, !capturing);

    const bool show_hours = snapshot.seconds >= 100 * 60;
    const auto major = show_hours ? snapshot.seconds / 3600 : snapshot.seconds / 60;
    const auto minor = show_hours ? snapshot.seconds / 60 % 60 : snapshot.seconds % 60;
    char time[16];
    std::snprintf(time, sizeof(time), "%02u:%02u", static_cast<unsigned>(major), static_cast<unsigned>(minor));
    UpdateLabel(elapsed_, time);

    const bool recording = snapshot.state == SpeakerRecorder::State::Recording;
    const float phase = (lv_tick_get() % 1700) * 0.003696f;
    for (size_t i = 0; i < bars_.size(); ++i) {
        const int size = recording ? 8 + static_cast<int>(42 * std::abs(std::sin(i * 0.57f + phase))) : 6;
        lv_obj_set_height(bars_[i], size);
        lv_obj_set_y(bars_[i], kWaveCenterY - size / 2);
    }

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
