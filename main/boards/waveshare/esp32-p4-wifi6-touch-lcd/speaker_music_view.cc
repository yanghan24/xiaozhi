#include "speaker_music_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "speaker_home_widgets.h"

using namespace SpeakerHomeWidgets;

namespace {
const char* FileName(const std::string& path) {
    return path.c_str() + path.find_last_of('/') + 1;
}
}  // namespace

void SpeakerMusicView::Create(lv_obj_t* parent) {
    root_ = Panel(parent);
    lv_obj_set_pos(Label(root_, "本地音乐", kText), 18, 7);
    auto* back = Button(root_, "返回", [](lv_event_t* event) {
        static_cast<SpeakerMusicView*>(lv_event_get_user_data(event))->Hide();
    }, this);
    lv_obj_set_size(back, 84, 40);
    lv_obj_set_pos(back, 518, 4);

    list_ = Card(root_, 18, 72, 366, 356);
    lv_obj_set_style_pad_all(list_, 8, LV_PART_MAIN);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    auto* player = Card(root_, 420, 72, 364, 356);
    lv_obj_set_pos(Label(player, "SD 卡播放", kCyan), 18, 15);
    Rule(player, 18, 56, 326, 1, kCyan, LV_OPA_40);
    song_ = Label(player, "选择音频文件", kText);
    lv_obj_set_width(song_, 326);
    lv_obj_set_pos(song_, 18, 92);
    lv_label_set_long_mode(song_, LV_LABEL_LONG_DOT);
    status_ = Label(player, "", kMuted);
    lv_obj_set_pos(status_, 18, 144);

    const auto add = [&](const char* name, int x, int y, lv_event_cb_t callback) {
        auto* button = Button(player, name, callback, nullptr);
        lv_obj_set_size(button, 100, 45);
        lv_obj_set_pos(button, x, y);
        return button;
    };
    add("上一首", 18, 214, [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Step(-1); });
    });
    auto* toggle = add("播放", 131, 214, [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Toggle(); });
    });
    toggle_label_ = lv_obj_get_child(toggle, 0);
    add("下一首", 244, 214, [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Step(1); });
    });
    add("停止", 75, 278, [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Stop(); });
    });
    add("刷新", 188, 278, [](lv_event_t*) { SpeakerMusic::Get().Refresh(); });
    Hide();
}

void SpeakerMusicView::Show() {
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    SpeakerMusic::Get().Refresh();
    Update();
}

void SpeakerMusicView::Hide() {
    if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerMusicView::Update() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) return;
    const auto snapshot = SpeakerMusic::Get().GetSnapshot();
    if (displayed_tracks_ != snapshot.tracks) {
        displayed_tracks_ = snapshot.tracks;
        lv_obj_clean(list_);
        if (displayed_tracks_) {
            for (size_t i = 0; i < displayed_tracks_->size(); ++i) {
                auto* button = Button(list_, FileName((*displayed_tracks_)[i]), [](lv_event_t* event) {
                    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
                    Application::GetInstance().Schedule([index] { SpeakerMusic::Get().Select(index); });
                }, reinterpret_cast<void*>(i));
                lv_obj_set_size(button, 340, 42);
                auto* label = lv_obj_get_child(button, 0);
                lv_obj_set_width(label, 316);
                lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            }
        }
    }
    const auto* title = snapshot.tracks && snapshot.selected >= 0
        ? FileName((*snapshot.tracks)[snapshot.selected]) : "选择音频文件";
    UpdateLabel(song_, title);
    const char* state = "";
    switch (snapshot.state) {
        case SpeakerMusic::State::Scanning: state = "正在读取 SD 卡"; break;
        case SpeakerMusic::State::NoCard: state = "未检测到可用 SD 卡"; break;
        case SpeakerMusic::State::Playing: state = "正在播放"; break;
        case SpeakerMusic::State::Paused: state = "已暂停"; break;
        case SpeakerMusic::State::Failed: state = "无法播放此文件"; break;
        case SpeakerMusic::State::Idle:
            state = snapshot.tracks && snapshot.tracks->empty() ? "未找到 MP3 或 WAV 文件" : "就绪";
            break;
    }
    UpdateLabel(status_, state);
    UpdateLabel(toggle_label_, snapshot.state == SpeakerMusic::State::Playing ? "暂停" : "播放");
}

#endif
