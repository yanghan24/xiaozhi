#include "speaker_conversation_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"

#include <algorithm>

using namespace SpeakerUi;

namespace {
constexpr int kMessageWidth = 378;
constexpr int kMessageScale = 470;
constexpr int kLineSpacing = 6;
constexpr int kMessageCenterY = -46;
constexpr uint32_t kMsPerLine = 2500;
constexpr uint32_t kScrollRefreshMs = 84;

int ScaleToScreen(int value) {
    return (value * kMessageScale + 128) / 256;
}
}

void SpeakerConversationView::Create(lv_obj_t* parent, void (*on_exit)(void*), void* context) {
    root_ = Panel(parent);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x070A15), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_NONE, LV_PART_MAIN);

    message_ = Label(root_, "");
    lv_obj_set_width(message_, kMessageWidth);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(message_, kLineSpacing, LV_PART_MAIN);
    lv_obj_set_style_max_height(message_, 104, LV_PART_MAIN);
    Scale(message_, kMessageScale);
    lv_obj_align(message_, LV_ALIGN_CENTER, 0, kMessageCenterY);

    subtitle_window_ = Surface(root_, 0, 0, ScaleToScreen(kMessageWidth), 0);
    lv_obj_add_flag(subtitle_window_, LV_OBJ_FLAG_HIDDEN);
    scrolling_subtitle_ = Label(subtitle_window_, "");
    lv_obj_set_width(scrolling_subtitle_, kMessageWidth);
    lv_label_set_long_mode(scrolling_subtitle_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(scrolling_subtitle_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(scrolling_subtitle_, kLineSpacing, LV_PART_MAIN);
    lv_obj_set_style_transform_scale(scrolling_subtitle_, kMessageScale, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(scrolling_subtitle_, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(scrolling_subtitle_, 0, LV_PART_MAIN);

    wave_.Create(root_, 405, 60, SpeakerWave::Shape::Ribbon);
    exit_.Create(root_, on_exit, context);
    exit_.SetVisible(true);
}

void SpeakerConversationView::Update(bool visible, SpeakerWave::Motion motion, const char* status,
                                     const char* subtitle, const lv_font_t* font, bool spoken_subtitle) {
    lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !visible);
    wave_.SetMotion(visible ? motion : SpeakerWave::Motion::Still);
    if (!visible) {
        StopSubtitleScroll();
        if (!lv_obj_has_flag(subtitle_window_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(subtitle_window_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(scrolling_subtitle_, "");
        }
        displayed_text_.clear();
        return;
    }

    const char* text = *subtitle ? subtitle : status;
    spoken_subtitle = spoken_subtitle && *subtitle;
    if (!new_subtitle_ && text == displayed_text_ && font == message_font_ && spoken_subtitle == spoken_subtitle_) {
        return;
    }
    new_subtitle_ = false;
    displayed_text_ = text;
    spoken_subtitle_ = spoken_subtitle;
    StopSubtitleScroll();
    if (font && font != message_font_) {
        lv_obj_set_style_text_font(message_, font, LV_PART_MAIN);
        lv_obj_set_style_text_font(scrolling_subtitle_, font, LV_PART_MAIN);
    }
    message_font_ = font;

    const auto* text_font = lv_obj_get_style_text_font(message_, LV_PART_MAIN);
    const int line_height = lv_font_get_line_height(text_font);
    const int window_height = 2 * line_height + kLineSpacing;
    lv_point_t size{};
    if (spoken_subtitle) {
        lv_text_get_size(&size, text, text_font, 0, kLineSpacing, kMessageWidth, LV_TEXT_FLAG_NONE);
    }
    const bool scroll = size.y > window_height;
    lv_obj_set_flag(message_, LV_OBJ_FLAG_HIDDEN, scroll);
    lv_obj_set_flag(subtitle_window_, LV_OBJ_FLAG_HIDDEN, !scroll);
    if (!scroll) {
        UpdateLabel(scrolling_subtitle_, "");
        UpdateLabel(message_, text);
        return;
    }

    lv_obj_set_height(subtitle_window_, ScaleToScreen(window_height));
    lv_obj_align(subtitle_window_, LV_ALIGN_CENTER, 0, kMessageCenterY);
    lv_obj_scroll_by(subtitle_window_, 0, lv_obj_get_scroll_y(subtitle_window_), LV_ANIM_OFF);
    UpdateLabel(scrolling_subtitle_, text);
    scroll_distance_ = ScaleToScreen(size.y - window_height);
    scroll_duration_ = (size.y - window_height) / (line_height + kLineSpacing) * kMsPerLine;
    scroll_start_ = lv_tick_get();
    subtitle_timer_ = lv_timer_create([](lv_timer_t* timer) {
        static_cast<SpeakerConversationView*>(lv_timer_get_user_data(timer))->StepSubtitleScroll();
    }, kScrollRefreshMs, this);
}

void SpeakerConversationView::StepSubtitleScroll() {
    const auto elapsed = std::min(lv_tick_elaps(scroll_start_), scroll_duration_);
    const auto offset = static_cast<int32_t>(static_cast<int64_t>(scroll_distance_) * elapsed / scroll_duration_);
    // Scroll the clipped window so LVGL invalidates the viewport, not the transformed label bounds.
    lv_obj_scroll_by(subtitle_window_, 0, lv_obj_get_scroll_y(subtitle_window_) - offset, LV_ANIM_OFF);
    if (elapsed == scroll_duration_) {
        StopSubtitleScroll();
    }
}

void SpeakerConversationView::StopSubtitleScroll() {
    if (subtitle_timer_) {
        lv_timer_delete(subtitle_timer_);
        subtitle_timer_ = nullptr;
    }
}

void SpeakerConversationView::Stop() {
    StopSubtitleScroll();
    wave_.Stop();
}

#endif
