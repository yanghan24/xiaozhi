#include "speaker_calculator_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"

using namespace SpeakerUi;

namespace {
constexpr int kLeft = 42;
constexpr int kLeftWidth = 320;
constexpr int kInset = 16;
constexpr int kDisplayWidth = kLeftWidth - 2 * kInset;
constexpr int kExpressionHeight = 46;
constexpr int kTop = 88;
constexpr int kKeyLeft = 382;
constexpr int kKeyWidth = 85;
constexpr int kKeyHeight = 60;
constexpr int kKeyGap = 12;

struct Key {
    char value;
    const char* label;
    uint32_t codepoint = 0;
};

constexpr Key kKeys[] = {
    {'C', "C"}, {'/', "÷", U'÷'}, {'*', "×", U'×'}, {'\b', LV_SYMBOL_BACKSPACE, 0xF55A},
    {'7', "7"}, {'8', "8"},       {'9', "9"},       {'-', "-"},
    {'4', "4"}, {'5', "5"},       {'6', "6"},       {'+', "+"},
    {'1', "1"}, {'2', "2"},       {'3', "3"},       {'%', "%"},
    {'0', "0"}, {'.', "."},       {'=', "="},
};

void CenterKeyGlyph(lv_obj_t* caption, uint32_t codepoint) {
    const auto* font = lv_obj_get_style_text_font(caption, LV_PART_MAIN);
    lv_font_glyph_dsc_t glyph;
    if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0)) {
        lv_obj_set_pos(caption, 0, 0);
        return;
    }
    // LVGL centers the advance box and line height, not the visible glyph.
    const int x = glyph.adv_w / 2 - glyph.box_w / 2 - glyph.ofs_x;
    const int y = font->base_line + glyph.ofs_y + (glyph.box_h - font->line_height) / 2;
    lv_obj_set_pos(caption, x, y);
}

lv_obj_t* Card(lv_obj_t* parent, int y, int height) {
    auto* card = Surface(parent, kLeft, y, kLeftWidth, height);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    CardBorder(card);
    return card;
}

std::string DisplayExpression(const std::string& expression) {
    std::string text;
    for (char c : expression) {
        if (c == '*') {
            text += "×";
        } else if (c == '/') {
            text += "÷";
        } else {
            text += c;
        }
    }
    return text;
}
}  // namespace

void SpeakerCalculatorView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    panel_ = Panel(parent);
    Header(panel_, "计算器", SpeakerNavigation::OnBack, &navigation);

    auto* display = Card(panel_, kTop, 131);
    auto* expression_clip = Surface(display, kInset, 11, kDisplayWidth, kExpressionHeight);
    expression_ = Label(expression_clip, "0", kMuted);
    lv_obj_align(expression_, LV_ALIGN_RIGHT_MID, 0, 0);
    Rule(display, kInset, 63, kDisplayWidth, 1);
    result_ = Label(display, "0", kCyan);
    lv_obj_set_width(result_, kDisplayWidth);
    lv_obj_set_style_text_align(result_, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(result_, LV_LABEL_LONG_DOT);
    lv_obj_align(result_, LV_ALIGN_BOTTOM_RIGHT, -kInset, -12);

    auto* history = Card(panel_, 235, 201);
    lv_obj_set_pos(Label(history, "历史记录", kMuted), kInset, 10);
    Rule(history, kInset, 53, kDisplayWidth, 1);
    history_scroll_ = Surface(history, kInset, 62, kDisplayWidth, 126);
    lv_obj_add_flag(history_scroll_,
                    static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_scroll_dir(history_scroll_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(history_scroll_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(history_scroll_, lv_color_hex(kMuted), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(history_scroll_, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(history_scroll_, 3, LV_PART_SCROLLBAR);
    history_label_ = Label(history_scroll_, "暂无记录", kMuted);
    lv_obj_set_width(history_label_, kDisplayWidth - 7);
    lv_obj_set_style_text_align(history_label_, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(history_label_, 3, LV_PART_MAIN);

    for (size_t index = 0; index < std::size(kKeys); ++index) {
        const auto& key = kKeys[index];
        const bool equal = key.value == '=';
        const bool numeric = (key.value >= '0' && key.value <= '9') || key.value == '.';
        const auto foreground = equal ? kDeep : (numeric ? kText : kCyan);
        auto* button = Control(
            panel_,
            [](lv_event_t* event) {
                auto* action = static_cast<KeyAction*>(lv_event_get_user_data(event));
                action->view->Press(action->key);
            },
            &actions_[index], equal ? kCyan : (numeric ? kCard : kBlue));
        actions_[index] = {this, key.value, key.codepoint ? key.codepoint : uint32_t(key.value)};
        lv_obj_set_pos(button, kKeyLeft + index % 4 * (kKeyWidth + kKeyGap),
                       kTop + index / 4 * (kKeyHeight + kKeyGap));
        lv_obj_set_size(button, equal ? 2 * kKeyWidth + kKeyGap : kKeyWidth, kKeyHeight);
        auto* caption = Label(button, key.label, foreground);
        if (key.value == '\b') {
            // The LVGL default font provides this symbol; the product text font does not.
            lv_obj_set_style_text_font(caption, LV_FONT_DEFAULT, LV_PART_MAIN);
        }
        lv_obj_center(caption);
        CenterKeyGlyph(caption, actions_[index].codepoint);
        // Asset loading can replace the inherited font after the view is created.
        lv_obj_add_event_cb(
            caption,
            [](lv_event_t* event) {
                const auto* action = static_cast<KeyAction*>(lv_event_get_user_data(event));
                CenterKeyGlyph(lv_event_get_target_obj(event), action->codepoint);
            },
            LV_EVENT_STYLE_CHANGED, &actions_[index]);
    }
}

void SpeakerCalculatorView::Show() { lv_obj_remove_flag(panel_, LV_OBJ_FLAG_HIDDEN); }

void SpeakerCalculatorView::Hide() { lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN); }

void SpeakerCalculatorView::Press(char key) {
    if (calculator_.Press(key)) {
        Render();
    }
}

void SpeakerCalculatorView::Render() {
    UpdateLabel(expression_, DisplayExpression(calculator_.Expression()).c_str());
    const auto result = calculator_.Preview();
    std::string text;
    switch (result.status) {
        case SpeakerCalculator::Status::DivisionByZero:
            text = "不能除以零";
            break;
        case SpeakerCalculator::Status::OutOfRange:
            text = "超出范围";
            break;
        case SpeakerCalculator::Status::Value:
            text = SpeakerCalculator::Format(result.value);
            if (calculator_.Confirmed()) {
                AddHistory(text);
            }
            break;
    }
    // Scientific notation can exceed the card width in the 30 px theme font.
    const auto* theme_font = lv_obj_get_style_text_font(lv_obj_get_parent(result_), LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), theme_font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const auto* font = size.x <= kDisplayWidth ? theme_font : LV_FONT_DEFAULT;
    if (font != lv_obj_get_style_text_font(result_, LV_PART_MAIN)) {
        if (font == theme_font) {
            lv_obj_remove_local_style_prop(result_, LV_STYLE_TEXT_FONT, LV_PART_MAIN);
        } else {
            lv_obj_set_style_text_font(result_, font, LV_PART_MAIN);
        }
    }
    UpdateLabel(result_, text.c_str());
}

void SpeakerCalculatorView::AddHistory(const std::string& result) {
    history_[next_history_] = DisplayExpression(calculator_.Expression()) + " = " + result;
    next_history_ = (next_history_ + 1) % history_.size();
    if (history_count_ < history_.size()) {
        ++history_count_;
    }
    std::string text;
    for (size_t i = history_count_; i > 0; --i) {
        const auto index = (next_history_ + history_.size() - i) % history_.size();
        if (!text.empty()) {
            text += '\n';
        }
        text += history_[index];
    }
    lv_label_set_text(history_label_, text.c_str());
    lv_obj_update_layout(history_scroll_);
    lv_obj_scroll_to_y(
        history_scroll_,
        lv_obj_get_scroll_y(history_scroll_) + lv_obj_get_scroll_bottom(history_scroll_),
        LV_ANIM_OFF);
}

#endif
