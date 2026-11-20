#include "speaker_music_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

namespace {
const char* FileName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1;
}

void DrawCover(lv_event_t* event) {
    auto* layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_current_target(event)), &area);
    struct Ridge {
        int left;
        int peak;
        int right;
        int top;
        uint32_t color;
    };
    constexpr Ridge ridges[] = {
        {3, 43, 110, 60, 0x6AA2CB},
        {36, 105, 129, 62, 0x5B91BD},
        {3, 43, 122, 83, 0x2C628F},
        {29, 105, 129, 90, 0x184C77},
        {5, 27, 104, 100, 0x143960},
        {46, 103, 126, 108, 0x102F50},
    };
    lv_draw_triangle_dsc_t triangle;
    lv_draw_triangle_dsc_init(&triangle);
    triangle.opa = LV_OPA_COVER;
    for (const auto& ridge : ridges) {
        triangle.color = lv_color_hex(ridge.color);
        triangle.p[0] = {area.x1 + ridge.left, area.y1 + 124};
        triangle.p[1] = {area.x1 + ridge.peak, area.y1 + ridge.top};
        triangle.p[2] = {area.x1 + ridge.right, area.y1 + 124};
        lv_draw_triangle(layer, &triangle);
    }
}
}  // namespace

void SpeakerMusicView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    root_ = Panel(parent);
    Header(root_, "音乐", SpeakerNavigation::OnBack, &navigation);
    Rule(root_, 472, 94, 1, 306);

    auto* cover = Surface(root_, 46, 129, 132, 132);
    lv_obj_set_style_bg_color(cover, lv_color_hex(0xBED1E5), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(cover, lv_color_hex(0x122E50), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(cover, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(cover, 21, LV_PART_MAIN);
    lv_obj_add_event_cb(cover, DrawCover, LV_EVENT_DRAW_MAIN_END, nullptr);

    song_ = Label(root_, "选择音频文件");
    lv_obj_set_width(song_, 245);
    lv_obj_set_pos(song_, 207, 138);
    lv_label_set_long_mode(song_, LV_LABEL_LONG_DOT);
    status_ = Label(root_, "", kMuted);
    lv_obj_set_width(status_, 245);
    lv_obj_set_pos(status_, 207, 188);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);

    const auto add = [&](const char* icon, int x, int y, int size, uint32_t color, lv_event_cb_t callback) {
        auto* button = Control(root_, callback, nullptr, color, LV_RADIUS_CIRCLE);
        lv_obj_set_size(button, size, size);
        lv_obj_set_pos(button, x, y);
        lv_obj_center(Icon(button, icon, color == kCyan ? kDeep : kText, 325));
        return lv_obj_get_child(button, 0);
    };
    add(MATERIAL_SYMBOLS_SKIP_PREVIOUS, 74, 289, 69, kCard, [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Step(-1); });
    });
    toggle_label_ = add(MATERIAL_SYMBOLS_PLAY_ARROW, 182, 274, 98, kCyan, [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Toggle(); });
    });
    add(MATERIAL_SYMBOLS_SKIP_NEXT, 320, 289, 69, kCard, [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Step(1); });
    });

    list_ = Surface(root_, 497, 102, 258, 285);
    lv_obj_add_flag(list_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 9, LV_PART_MAIN);
    auto* stop = Button(root_, "停止", [](lv_event_t*) {
        Application::GetInstance().Schedule([] { SpeakerMusic::Get().Stop(); });
    }, nullptr);
    auto* refresh = Button(root_, "刷新", [](lv_event_t*) { SpeakerMusic::Get().Refresh(); }, nullptr);
    for (auto* button : {stop, refresh}) {
        Outline(button);
        lv_obj_set_size(button, 110, 52);
    }
    lv_obj_set_pos(stop, 502, 393);
    lv_obj_set_pos(refresh, 633, 393);
}

void SpeakerMusicView::Show() {
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
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
        selected_index_ = -1;
        if (displayed_tracks_) {
            for (size_t i = 0; i < displayed_tracks_->size(); ++i) {
                auto* button = Button(list_, FileName((*displayed_tracks_)[i]), [](lv_event_t* event) {
                    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
                    Application::GetInstance().Schedule([index] { SpeakerMusic::Get().Select(index); });
                }, reinterpret_cast<void*>(i));
                lv_obj_set_size(button, 252, 62);
                auto* label = lv_obj_get_child(button, 0);
                lv_obj_set_width(label, 222);
                lv_obj_align(label, LV_ALIGN_LEFT_MID, 17, 0);
                lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            }
        }
    }
    if (selected_index_ != snapshot.selected) {
        for (int index : {selected_index_, snapshot.selected}) {
            if (index >= 0 && static_cast<uint32_t>(index) < lv_obj_get_child_count(list_)) {
                const uint32_t color = index == snapshot.selected ? 0x1B3C70 : kCard;
                lv_obj_set_style_bg_color(lv_obj_get_child(list_, index), lv_color_hex(color), LV_PART_MAIN);
            }
        }
        selected_index_ = snapshot.selected;
    }
    const auto* title = snapshot.tracks && snapshot.selected >= 0 &&
        static_cast<size_t>(snapshot.selected) < snapshot.tracks->size()
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
    UpdateLabel(toggle_label_, snapshot.state == SpeakerMusic::State::Playing
        ? MATERIAL_SYMBOLS_PAUSE : MATERIAL_SYMBOLS_PLAY_ARROW);
}

#endif
