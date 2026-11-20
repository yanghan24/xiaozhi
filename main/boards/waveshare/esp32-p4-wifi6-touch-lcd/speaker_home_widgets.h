#pragma once

#include <lvgl.h>

namespace SpeakerHomeWidgets {

inline lv_obj_t* Button(lv_obj_t* parent, const char* text, lv_event_cb_t handler, void* context) {
    auto* button = lv_button_create(parent);
    lv_obj_set_size(button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(button, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_top(button, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(button, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x175CD3), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, context);
    return button;
}

inline lv_obj_t* Panel(lv_obj_t* parent) {
    auto* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, lv_pct(100), lv_pct(100));
    lv_obj_center(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x071525), LV_PART_MAIN);
    lv_obj_set_style_text_color(panel, lv_color_hex(0xF4F8FF), LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

}  // namespace SpeakerHomeWidgets
