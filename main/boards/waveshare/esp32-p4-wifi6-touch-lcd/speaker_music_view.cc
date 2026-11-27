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
    const int line_height =
        lv_font_get_line_height(lv_obj_get_style_text_font(song_, LV_PART_MAIN));
    lv_obj_set_size(song_, 245, line_height);
    lv_obj_set_pos(song_, 207, 128);
    lv_label_set_long_mode(song_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    status_ = Label(root_, "", kMuted);
    lv_obj_set_width(status_, 245);
    lv_obj_set_pos(status_, 207, 128 + line_height + 12);

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
    delete_button_ = Button(
        root_, "删除",
        [](lv_event_t* event) {
            static_cast<SpeakerMusicView*>(lv_event_get_user_data(event))->OpenDeleteDialog();
        },
        this, kCard, kWarm);
    int x = 497;
    for (auto* button : {stop, refresh, delete_button_}) {
        Outline(button);
        lv_obj_set_size(button, 82, 52);
        lv_obj_set_pos(button, x, 393);
        x += 88;
    }
    lv_obj_add_state(delete_button_, LV_STATE_DISABLED);
}

void SpeakerMusicView::OpenDeleteDialog() {
    if (delete_dialog_ || !displayed_tracks_ || selected_index_ < 0)
        return;
    const auto& path = displayed_tracks_->at(selected_index_);
    delete_dialog_ = Panel(root_);
    lv_obj_remove_flag(delete_dialog_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(delete_dialog_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(delete_dialog_, LV_OPA_70, LV_PART_MAIN);
    auto* card = Surface(delete_dialog_, 100, 118, 600, 244);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_align(Label(card, "从 SD 卡删除此文件？"), LV_ALIGN_TOP_MID, 0, 16);
    auto* filename = Label(card, path.c_str(), kMuted);
    lv_obj_set_width(filename, 542);
    lv_label_set_long_mode(filename, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(filename, LV_ALIGN_TOP_MID, 0, 83);

    auto* cancel = Button(
        card, "取消",
        [](lv_event_t* event) {
            static_cast<SpeakerMusicView*>(lv_event_get_user_data(event))->CloseDeleteDialog();
        },
        this);
    Outline(cancel);
    auto* confirm = Button(
        card, "确认删除",
        [](lv_event_t* event) {
            auto* view = static_cast<SpeakerMusicView*>(lv_event_get_user_data(event));
            Application::GetInstance().Schedule(
                [tracks = view->displayed_tracks_, index = view->selected_index_] {
                    SpeakerMusic::Get().DeleteSelected(tracks, index);
                });
            view->CloseDeleteDialog();
        },
        this, kWarm, kDeep);
    lv_obj_set_size(cancel, 245, 56);
    lv_obj_set_size(confirm, 245, 56);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 36, -22);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -36, -22);
}

void SpeakerMusicView::CloseDeleteDialog() {
    if (delete_dialog_) {
        lv_obj_delete(delete_dialog_);
        delete_dialog_ = nullptr;
    }
}

void SpeakerMusicView::Show() {
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    SpeakerMusic::Get().Refresh();
    Update();
}

void SpeakerMusicView::Hide() {
    CloseDeleteDialog();
    if (root_) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerMusicView::Update() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) return;
    const auto snapshot = SpeakerMusic::Get().GetSnapshot();
    if (snapshot.UpdatingFiles() || snapshot.tracks != displayed_tracks_ ||
        snapshot.selected != selected_index_) {
        CloseDeleteDialog();
    }
    lv_obj_set_state(delete_button_, LV_STATE_DISABLED,
                     snapshot.selected < 0 || snapshot.UpdatingFiles());
    if (displayed_tracks_ != snapshot.tracks) {
        displayed_tracks_ = snapshot.tracks;
        lv_obj_clean(list_);
        selected_index_ = -1;
        if (displayed_tracks_) {
            for (size_t i = 0; i < displayed_tracks_->size(); ++i) {
                auto* button = Button(list_, FileName((*displayed_tracks_)[i]), [](lv_event_t* event) {
                    auto* view = static_cast<SpeakerMusicView*>(lv_event_get_user_data(event));
                    const int index = lv_obj_get_index(lv_event_get_current_target_obj(event));
                    Application::GetInstance().Schedule([tracks = view->displayed_tracks_, index] {
                        SpeakerMusic::Get().Select(tracks, index);
                    });
                }, this);
                lv_obj_set_size(button, 252, 62);
                auto* label = lv_obj_get_child(button, 0);
                lv_obj_set_size(
                    label, 222,
                    lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
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
        case SpeakerMusic::State::Deleting:
            state = "正在删除";
            break;
        case SpeakerMusic::State::DeleteFailed:
            state = "删除失败，请刷新";
            break;
        case SpeakerMusic::State::Idle:
            state = snapshot.tracks && snapshot.tracks->empty() ? "未找到 MP3 或 WAV 文件" : "就绪";
            break;
    }
    UpdateLabel(status_, state);
    UpdateLabel(toggle_label_, snapshot.state == SpeakerMusic::State::Playing
        ? MATERIAL_SYMBOLS_PAUSE : MATERIAL_SYMBOLS_PLAY_ARROW);
}

#endif
