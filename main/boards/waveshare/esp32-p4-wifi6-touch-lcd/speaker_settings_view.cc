#include "speaker_settings_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

void SpeakerSettingsView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    root_ = Panel(parent);
    Header(root_, "设置", SpeakerNavigation::OnBack, &navigation);
    auto* account = Control(root_, [](lv_event_t* event) {
        static_cast<SpeakerNavigation*>(lv_event_get_user_data(event))->Open(SpeakerNavigation::Page::Account);
    }, &navigation);
    lv_obj_set_pos(account, 45, 123);
    lv_obj_set_size(account, 710, 104);
    lv_obj_set_style_border_width(account, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(account, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_align(Icon(account, MATERIAL_SYMBOLS_PERSON, kCyan, 340), LV_ALIGN_LEFT_MID, 32, 0);
    lv_obj_align(Label(account, "账号"), LV_ALIGN_LEFT_MID, 105, 0);
    lv_obj_align(Icon(account, MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT), LV_ALIGN_RIGHT_MID, -29, 0);
}

void SpeakerSettingsView::Show() {
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void SpeakerSettingsView::Hide() {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

#endif
