#include "speaker_settings_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "settings.h"
#include "speaker_account/speaker_account.h"
#include "speaker_account/speaker_ui_widgets.h"
#include <algorithm>
#include <esp_app_desc.h>

using namespace SpeakerUi;

namespace {
using Page = SpeakerNavigation::Page;
using ScreensaverStyle = SpeakerScreensaverView::Style;
constexpr char kNamespace[] = "speaker_ui";
constexpr char kScreensaverMinutes[] = "screen_idle_min";
constexpr char kScreensaverStyle[] = "screen_style";
constexpr std::array kMinutes{1, 3, 5, 10};
constexpr std::array kTimeoutLabels{"1 分钟", "3 分钟", "5 分钟", "10 分钟"};
constexpr std::array kStyles{ScreensaverStyle::Dashboard, ScreensaverStyle::Classic};
constexpr std::array kStyleLabels{"样式一", "样式二"};
constexpr int kMinBrightness = 10;
constexpr int kMaxLevel = 100;

bool SaveScreenSetting(const char* key, int value) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const bool saved = nvs_set_i32(handle, key, value) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
}

void UpdateScreenOption(lv_obj_t* button, bool selected) {
    lv_obj_set_style_bg_color(button, lv_color_hex(selected ? kCyan : kCard), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(button, 0), lv_color_hex(selected ? kDeep : kText), LV_PART_MAIN);
}

void RegisterPage(SpeakerNavigation& navigation, Page page, lv_obj_t* panel) {
    navigation.Register(page, [panel] { lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN); },
                        [panel] { lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); });
}

lv_obj_t* AboutRow(lv_obj_t* panel, const char* title, int y) {
    lv_obj_set_pos(Label(panel, title, kMuted), 54, y);
    auto* value = Label(panel, "");
    lv_obj_set_width(value, 478);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(value, 257, y);
    return value;
}
}

void SpeakerSettingsView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    Settings settings(kNamespace);
    const int saved = settings.GetInt(kScreensaverMinutes, kMinutes.front());
    if (std::ranges::find(kMinutes, saved) != kMinutes.end()) {
        screensaver_minutes_ = saved;
    }
    const int saved_style = settings.GetInt(kScreensaverStyle, static_cast<int>(ScreensaverStyle::Dashboard));
    if (saved_style == static_cast<int>(ScreensaverStyle::Classic)) {
        screensaver_style_ = ScreensaverStyle::Classic;
    }

    root_ = Panel(parent);
    Header(root_, "设置", SpeakerNavigation::OnBack, &navigation);
    constexpr int kFirstRowY = 103;
    constexpr int kRowHeight = 91;
    constexpr int kGap = 16;
    constexpr int kSideMargin = 45;
    constexpr int kColumns = 2;
    constexpr int kCardWidth = (kWidth - 2 * kSideMargin - kGap) / kColumns;
    struct Entry { const char* title; const char* icon; Page page; };
    constexpr Entry entries[] = {
        {"账号", MATERIAL_SYMBOLS_PERSON, Page::Account},
#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI
        {"Wi-Fi", MATERIAL_SYMBOLS_WIFI, Page::Wifi},
#endif
        {"声音和显示", MATERIAL_SYMBOLS_VOLUME_UP, Page::SoundAndDisplay},
        {"屏保", MATERIAL_SYMBOLS_SCHEDULE, Page::ScreensaverSettings},
        {"关于设备", MATERIAL_SYMBOLS_INFO, Page::About},
    };
    for (size_t index = 0; index < std::size(entries); ++index) {
        const auto& entry = entries[index];
        routes_[index] = {&navigation, entry.page};
        auto* row = Control(root_, [](lv_event_t* event) {
            auto* action = static_cast<RouteAction*>(lv_event_get_user_data(event));
            action->navigation->Open(action->page);
        }, &routes_[index]);
        const int column = static_cast<int>(index % kColumns);
        const int row_index = static_cast<int>(index / kColumns);
        lv_obj_set_pos(row, kSideMargin + column * (kCardWidth + kGap),
                       kFirstRowY + row_index * (kRowHeight + kGap));
        lv_obj_set_size(row, kCardWidth, kRowHeight);
        CardBorder(row);
        lv_obj_align(Icon(row, entry.icon, kCyan, 340), LV_ALIGN_LEFT_MID, 32, 0);
        lv_obj_align(Label(row, entry.title), LV_ALIGN_LEFT_MID, 105, 0);
        lv_obj_align(Icon(row, MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT), LV_ALIGN_RIGHT_MID, -29, 0);
    }
    RegisterPage(navigation, Page::Settings, root_);
    CreateSoundAndDisplay(parent, navigation);
    CreateScreensaver(parent, navigation);
    CreateAbout(parent, navigation);
}

void SpeakerSettingsView::CreateSoundAndDisplay(lv_obj_t* parent, SpeakerNavigation& navigation) {
    auto* panel = Panel(parent);
    Header(panel, "声音和显示", SpeakerNavigation::OnBack, &navigation);
    CreateLevelSlider(panel, volume_, "音量", MATERIAL_SYMBOLS_VOLUME_UP, 103, false);
    CreateLevelSlider(panel, brightness_, "屏幕亮度", MATERIAL_SYMBOLS_BRIGHTNESS_6, 264, true);
    navigation.Register(Page::SoundAndDisplay, [this, panel] {
        UpdateSoundAndDisplay();
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }, [panel] { lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); });
}

void SpeakerSettingsView::UpdateSoundAndDisplay() {
    auto& board = Board::GetInstance();
    auto* codec = board.GetAudioCodec();
    auto* backlight = board.GetBacklight();
    for (auto* slider : {&volume_, &brightness_}) {
        if (slider->editing) {
            continue;
        }
        const bool available = slider->brightness ? backlight != nullptr : codec != nullptr;
        lv_obj_set_state(slider->control, LV_STATE_DISABLED, !available);
        if (!available) {
            UpdateLabel(slider->value_label, "暂不可用");
            continue;
        }
        const int value = slider->brightness ? backlight->target_brightness() : codec->output_volume();
        if (slider->displayed_value != value) {
            slider->displayed_value = value;
            lv_slider_set_value(slider->control, value, LV_ANIM_OFF);
            lv_label_set_text_fmt(slider->value_label, "%d%%", value);
        }
    }
}

void SpeakerSettingsView::CreateLevelSlider(lv_obj_t* panel, LevelSlider& slider, const char* title,
                                            const char* icon, int y, bool brightness) {
    auto* card = Surface(panel, 45, y, 710, 138);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_set_pos(Icon(card, icon, kCyan), 25, 24);
    lv_obj_set_pos(Label(card, title), 76, 12);
    slider.value_label = Label(card, "", kMuted);
    lv_obj_align(slider.value_label, LV_ALIGN_TOP_RIGHT, -27, 12);
    slider.brightness = brightness;

    auto* control = lv_slider_create(card);
    slider.control = control;
    lv_obj_remove_style_all(control);
    lv_obj_set_pos(control, 42, 92);
    lv_obj_set_size(control, 624, 14);
    lv_obj_set_ext_click_area(control, 23);
    lv_obj_add_flag(control, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_bg_color(control, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_set_style_bg_color(control, lv_color_hex(kCyan), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(control, lv_color_hex(kText), LV_PART_KNOB);
    for (auto part : {LV_PART_MAIN, LV_PART_INDICATOR, LV_PART_KNOB}) {
        lv_obj_set_style_bg_opa(control, LV_OPA_COVER, part);
        lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, part);
    }
    lv_obj_set_style_pad_all(control, 9, LV_PART_KNOB);
    lv_slider_set_range(control, brightness ? kMinBrightness : 0, kMaxLevel);
    for (auto code : {LV_EVENT_PRESSED, LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
        lv_obj_add_event_cb(control, OnLevelSlider, code, &slider);
    }
}

void SpeakerSettingsView::OnLevelSlider(lv_event_t* event) {
    auto& slider = *static_cast<LevelSlider*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        slider.editing = true;
        slider.changed = false;
        return;
    }
    if (!slider.editing) {
        return;
    }
    const int value = lv_slider_get_value(slider.control);
    if (code == LV_EVENT_VALUE_CHANGED) {
        slider.changed = true;
        slider.displayed_value = value;
        lv_label_set_text_fmt(slider.value_label, "%d%%", value);
        if (slider.brightness) {
            if (auto* backlight = Board::GetInstance().GetBacklight()) {
                backlight->SetBrightness(static_cast<uint8_t>(value));
            }
        }
        return;
    }

    slider.editing = false;
    if (!slider.changed && (code == LV_EVENT_PRESS_LOST || value == slider.displayed_value)) {
        return;
    }
    slider.displayed_value = value;
    lv_label_set_text_fmt(slider.value_label, "%d%%", value);
    // Commit on the application task, which also handles the voice MCP controls.
    Application::GetInstance().Schedule([value, brightness = slider.brightness] {
        auto& board = Board::GetInstance();
        if (brightness) {
            if (auto* backlight = board.GetBacklight()) {
                backlight->SetBrightness(static_cast<uint8_t>(value), true);
            }
        } else if (auto* codec = board.GetAudioCodec(); codec && codec->output_volume() != value) {
            codec->SetOutputVolume(value);
        }
    });
}

void SpeakerSettingsView::CreateScreensaver(lv_obj_t* parent, SpeakerNavigation& navigation) {
    auto* panel = Panel(parent);
    Header(panel, "屏保", SpeakerNavigation::OnBack, &navigation);
    timeout_hint_ = Label(panel, "自动进入时间", kMuted);
    lv_obj_set_pos(timeout_hint_, 54, 129);
    for (size_t index = 0; index < kMinutes.size(); ++index) {
        timeouts_[index] = {this, kMinutes[index]};
        auto* button = Button(panel, kTimeoutLabels[index], [](lv_event_t* event) {
            auto* action = static_cast<TimeoutAction*>(lv_event_get_user_data(event));
            action->view->SetScreensaverMinutes(action->minutes);
        }, &timeouts_[index]);
        lv_obj_set_size(button, 155, 80);
        lv_obj_set_pos(button, 54 + static_cast<int>(index) * 179, 204);
        timeout_buttons_[index] = button;
    }
    UpdateTimeoutButtons();
    style_hint_ = Label(panel, "样式", kMuted);
    lv_obj_set_pos(style_hint_, 54, 309);
    for (size_t index = 0; index < kStyles.size(); ++index) {
        styles_[index] = {this, kStyles[index]};
        auto* button = Button(panel, kStyleLabels[index], [](lv_event_t* event) {
            auto* action = static_cast<StyleAction*>(lv_event_get_user_data(event));
            action->view->SetScreensaverStyle(action->style);
        }, &styles_[index]);
        lv_obj_set_size(button, 334, 80);
        lv_obj_set_pos(button, 54 + static_cast<int>(index) * 358, 362);
        style_buttons_[index] = button;
    }
    UpdateStyleButtons();
    RegisterPage(navigation, Page::ScreensaverSettings, panel);
}

void SpeakerSettingsView::CreateAbout(lv_obj_t* parent, SpeakerNavigation& navigation) {
    auto* panel = Panel(parent);
    Header(panel, "关于设备", SpeakerNavigation::OnBack, &navigation);
    UpdateLabel(AboutRow(panel, "设备型号", 123), "筑音 P4X 4.3 寸");
    Rule(panel, 54, 178, 692, 1);
    UpdateLabel(AboutRow(panel, "固件版本", 202), esp_app_get_description()->version);
    Rule(panel, 54, 257, 692, 1);
    auto* device_id = AboutRow(panel, "设备编号", 281);
    navigation.Register(Page::About, [panel, device_id] {
        const auto& hardware_id = SpeakerAccount::Get().hardware_id();
        UpdateLabel(device_id, hardware_id.empty() ? "暂不可用" : hardware_id.c_str());
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }, [panel] { lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); });
}

uint32_t SpeakerSettingsView::ScreensaverDelayMs() const {
    return static_cast<uint32_t>(screensaver_minutes_) * 60 * 1000;
}

void SpeakerSettingsView::SetScreensaverMinutes(int minutes) {
    if (minutes == screensaver_minutes_) {
        return;
    }
    if (!SaveScreenSetting(kScreensaverMinutes, minutes)) {
        UpdateLabel(timeout_hint_, "保存失败，请重试");
        return;
    }
    screensaver_minutes_ = minutes;
    UpdateLabel(timeout_hint_, "自动进入时间");
    UpdateTimeoutButtons();
    lv_display_trigger_activity(lv_obj_get_display(root_));
}

void SpeakerSettingsView::UpdateTimeoutButtons() {
    for (size_t index = 0; index < timeout_buttons_.size(); ++index) {
        UpdateScreenOption(timeout_buttons_[index], kMinutes[index] == screensaver_minutes_);
    }
}

void SpeakerSettingsView::SetScreensaverStyle(ScreensaverStyle style) {
    if (style == screensaver_style_) {
        return;
    }
    if (!SaveScreenSetting(kScreensaverStyle, static_cast<int>(style))) {
        UpdateLabel(style_hint_, "保存失败，请重试");
        return;
    }
    screensaver_style_ = style;
    UpdateLabel(style_hint_, "样式");
    UpdateStyleButtons();
    lv_display_trigger_activity(lv_obj_get_display(root_));
}

void SpeakerSettingsView::UpdateStyleButtons() {
    for (size_t index = 0; index < style_buttons_.size(); ++index) {
        UpdateScreenOption(style_buttons_[index], kStyles[index] == screensaver_style_);
    }
}

#endif
