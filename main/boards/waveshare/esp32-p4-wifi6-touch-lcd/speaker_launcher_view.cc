#include "speaker_launcher_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

namespace {
constexpr int kClockWidth = 230;
constexpr int kClockDetailsScale = 205;  // 24 px with the existing 30 px theme font.

lv_obj_t* Tomato(lv_obj_t* parent) {
    auto* icon = Surface(parent, 0, 0, 50, 51);
    auto* fruit = Surface(icon, 1, 8, 48, 43);
    lv_obj_set_style_radius(fruit, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fruit, lv_color_hex(0xFF9673), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(fruit, lv_color_hex(0xDF584E), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(fruit, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fruit, LV_OPA_COVER, LV_PART_MAIN);
    for (int side : {-1, 1}) {
        auto* leaf = Surface(icon, 22 + side * 6, 3, 12, 7);
        lv_obj_set_style_radius(leaf, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(leaf, lv_color_hex(0x61CEAC), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(leaf, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_transform_rotation(leaf, side * -280, LV_PART_MAIN);
    }
    return icon;
}
}

void SpeakerLauncherView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    using Page = SpeakerNavigation::Page;
    root_ = Panel(parent);
    wifi_ = Wifi(root_);

    auto* clock_group = Surface(root_, 0, 0, kClockWidth, 160);
    lv_obj_align(clock_group, LV_ALIGN_RIGHT_MID, -17, -26);
    clock_ = Digits(clock_group, "--:--", kClockWidth, 360);
    lv_obj_align(clock_, LV_ALIGN_CENTER, 0, -28);
    date_ = Label(clock_group, "正在校时", kMuted);
    weather_ = Label(clock_group, "", kMuted);
    Scale(date_, kClockDetailsScale);
    Scale(weather_, kClockDetailsScale);
    lv_obj_align(date_, LV_ALIGN_CENTER, 0, 22);
    lv_obj_align(weather_, LV_ALIGN_CENTER, 0, 56);

    constexpr Entry entries[] = {
        {"设置", MATERIAL_SYMBOLS_SETTINGS, 0x8BC9FA, Page::Settings},
#if LV_USE_CALENDAR
        {"日历", MATERIAL_SYMBOLS_CALENDAR_MONTH, 0x69CDEC, Page::Calendar},
#endif
        {"音乐", MATERIAL_SYMBOLS_MUSIC_NOTE, 0xA99AF9, Page::Music},
        {"录音", MATERIAL_SYMBOLS_MIC, 0xFF9B76, Page::Recording},
        {"番茄钟", nullptr, 0, Page::Pomodoro},
        {"计算器", MATERIAL_SYMBOLS_CALCULATE, 0x80D8BD, Page::Calculator},
    };
    for (size_t index = 0; index < std::size(entries); ++index) {
        const auto& entry = entries[index];
        auto* tile = Control(root_, nullptr, nullptr, kCard, 27);
        lv_obj_set_size(tile, 145, 145);
        lv_obj_set_pos(tile, 42 + static_cast<int>(index % 3) * 160,
                       61 + static_cast<int>(index / 3) * 160);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, lv_color_hex(kEdge), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0x0C1E44), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, LV_PART_MAIN);
        actions_[index] = {&navigation, entry.page};
        lv_obj_add_event_cb(tile, [](lv_event_t* event) {
            auto* target = static_cast<LaunchAction*>(lv_event_get_user_data(event));
            target->navigation->Open(target->page);
        }, LV_EVENT_CLICKED, &actions_[index]);
        auto* icon = entry.glyph ? Icon(tile, entry.glyph, entry.tint, 470) : Tomato(tile);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -23);
        lv_obj_align(Label(tile, entry.title), LV_ALIGN_BOTTOM_MID, 0, -17);
    }
}

void SpeakerLauncherView::Show() {
    background_.Load(root_, SpeakerBackground::Artwork::Home);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerLauncherView::Hide() {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerLauncherView::Update(const char* time, const char* date, const char* weather, const char* wifi) {
    UpdateLabel(clock_, time);
    UpdateLabel(date_, date);
    UpdateLabel(weather_, weather);
    UpdateLabel(wifi_, wifi);
}

#endif
