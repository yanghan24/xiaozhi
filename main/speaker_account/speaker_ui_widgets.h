#pragma once

#include <lvgl.h>
#include <material_symbols.h>
#include <cstdint>
#include <cstring>

LV_FONT_DECLARE(font_material_symbols_30_4);
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
LV_FONT_DECLARE(speaker_digits_font);
#endif

namespace SpeakerUi {

inline constexpr int kWidth = 800;
inline constexpr int kHeight = 480;
inline constexpr uint32_t kNavy = 0x050D30;
inline constexpr uint32_t kDeep = 0x030B22;
inline constexpr uint32_t kText = 0xF4F6FF;
inline constexpr uint32_t kMuted = 0xAEBFE0;
inline constexpr uint32_t kCyan = 0x69CDEC;
inline constexpr uint32_t kBlue = 0x1232A6;
inline constexpr uint32_t kEdge = 0x2A416E;
inline constexpr uint32_t kCard = 0x132952;
inline constexpr uint32_t kWarm = 0xFF9B76;

inline lv_obj_t* Label(lv_obj_t* parent, const char* text, uint32_t color = kText) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

inline void UpdateLabel(lv_obj_t* label, const char* text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

inline void Scale(lv_obj_t* object, int scale) {
    lv_obj_set_style_transform_pivot_x(object, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(object, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_transform_scale(object, scale, LV_PART_MAIN);
}

inline lv_obj_t* Icon(lv_obj_t* parent, const char* symbol, uint32_t color = kText, int scale = 256) {
    auto* label = Label(parent, symbol, color);
    lv_obj_set_style_text_font(label, &font_material_symbols_30_4, LV_PART_MAIN);
    if (scale != 256) {
        Scale(label, scale);
    }
    return label;
}

inline lv_obj_t* Digits(lv_obj_t* parent, const char* text, int width, int scale = 256) {
    auto* label = Label(parent, text);
#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT && CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    lv_obj_set_style_text_font(label, &speaker_digits_font, LV_PART_MAIN);
#elif LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, LV_PART_MAIN);
#endif
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    Scale(label, scale);
    return label;
}

inline lv_obj_t* Surface(lv_obj_t* parent, int x, int y, int width, int height) {
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    return object;
}

inline lv_obj_t* Panel(lv_obj_t* parent) {
    auto* panel = Surface(parent, 0, 0, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(panel, lv_color_hex(kNavy), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(kDeep), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

inline lv_obj_t* Rule(lv_obj_t* parent, int x, int y, int width, int height) {
    auto* rule = Surface(parent, x, y, width, height);
    lv_obj_set_style_bg_color(rule, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    return rule;
}

inline lv_obj_t* Control(lv_obj_t* parent, lv_event_cb_t handler, void* context,
                         uint32_t background = kCard, int radius = 18) {
    auto* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    if (handler) {
        lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, context);
    }
    return button;
}

inline lv_obj_t* Button(lv_obj_t* parent, const char* text, lv_event_cb_t handler, void* context,
                        uint32_t background = kCard, uint32_t foreground = kText) {
    auto* button = Control(parent, handler, context, background);
    lv_obj_center(Label(button, text, foreground));
    return button;
}

inline void CardBorder(lv_obj_t* card) {
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(kEdge), LV_PART_MAIN);
}

inline void Outline(lv_obj_t* button, uint32_t color = kMuted) {
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(color), LV_PART_MAIN);
}

inline lv_obj_t* Header(lv_obj_t* parent, const char* title, lv_event_cb_t back, void* context) {
    auto* label = Label(parent, title);
    Scale(label, 290);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 23);
    if (!back) {
        return nullptr;
    }
    auto* button = Control(parent, back, context);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_size(button, 62, 62);
    lv_obj_set_pos(button, 27, 8);
    lv_obj_center(Icon(button, MATERIAL_SYMBOLS_ARROW_BACK, kText, 300));
    return button;
}

inline lv_obj_t* Wifi(lv_obj_t* parent) {
    auto* icon = Icon(parent, MATERIAL_SYMBOLS_WIFI);
    lv_obj_align(icon, LV_ALIGN_TOP_RIGHT, -29, 20);
    return icon;
}

}  // namespace SpeakerUi
