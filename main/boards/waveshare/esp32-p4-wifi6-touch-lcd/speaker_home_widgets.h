#pragma once

#include <lvgl.h>

namespace SpeakerHomeWidgets {

inline constexpr uint32_t kText = 0xF4F8FF;
inline constexpr uint32_t kMuted = 0xA8B6CB;
inline constexpr uint32_t kCyan = 0x27E5FF;
inline constexpr uint32_t kBlue = 0x3487FF;
inline constexpr uint32_t kEdge = 0x17415D;
inline constexpr uint32_t kCard = 0x081B31;

inline lv_obj_t* Label(lv_obj_t* parent, const char* text, uint32_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

inline lv_obj_t* Button(lv_obj_t* parent, const char* text, lv_event_cb_t handler, void* context) {
    auto* button = lv_button_create(parent);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(button, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(button, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(kEdge), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(kBlue), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    auto* label = Label(button, text, kText);
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
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x050A14), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(panel, lv_color_hex(0x071525), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(panel, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

inline lv_obj_t* Card(lv_obj_t* parent, int x, int y, int width, int height) {
    auto* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    return card;
}

class DigitalClock {
public:
    lv_obj_t* Create(lv_obj_t* parent, uint32_t color) {
        color_ = lv_color_hex(color);
        auto* face = lv_obj_create(parent);
        lv_obj_remove_style_all(face);
        lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(face, 272, 104);

        constexpr int digit_x[] = {0, 66, 152, 218};
        constexpr int geometry[][4] = {
            {8, 0, 38, 8}, {46, 8, 8, 39}, {46, 56, 8, 40},
            {8, 96, 38, 8}, {0, 56, 8, 40}, {0, 8, 8, 39}, {8, 48, 38, 8},
        };
        for (int digit = 0; digit < 4; ++digit) {
            for (int bar = 0; bar < 7; ++bar) {
                const auto& rect = geometry[bar];
                bars_[digit][bar] = Bar(face, digit_x[digit] + rect[0], rect[1], rect[2], rect[3]);
            }
        }
        constexpr int colon_y[] = {34, 62};
        for (int y : colon_y) {
            auto* dot = Bar(face, 132, y, 8, 8);
            lv_obj_set_style_bg_color(dot, color_, LV_PART_MAIN);
        }
        SetTime("--:--");
        return face;
    }

    // The four digits of a normalized HH:MM or MM:SS string.
    void SetTime(const char* time) {
        constexpr int positions[] = {0, 1, 3, 4};
        constexpr uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
        for (int digit = 0; digit < 4; ++digit) {
            const char character = time[positions[digit]];
            if (character == rendered_[digit]) {
                continue;
            }
            rendered_[digit] = character;
            const auto mask = character >= '0' && character <= '9' ? masks[character - '0'] : 0x40;
            for (int bar = 0; bar < 7; ++bar) {
                const auto color = (mask & (1 << bar)) ? color_ : lv_color_hex(0x0A3145);
                lv_obj_set_style_bg_color(bars_[digit][bar], color, LV_PART_MAIN);
            }
        }
    }

private:
    static lv_obj_t* Bar(lv_obj_t* parent, int x, int y, int width, int height) {
        auto* bar = lv_obj_create(parent);
        lv_obj_remove_style_all(bar);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(bar, x, y);
        lv_obj_set_size(bar, width, height);
        lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        return bar;
    }

    lv_obj_t* bars_[4][7]{};
    char rendered_[4]{};
    lv_color_t color_{};
};

}  // namespace SpeakerHomeWidgets
