#include "speaker_settings_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "settings.h"
#include "speaker_account/speaker_account.h"
#include "speaker_account/speaker_ui_widgets.h"
#include <algorithm>
#include <esp_app_desc.h>

using namespace SpeakerUi;

namespace {
using Page = SpeakerNavigation::Page;
constexpr char kNamespace[] = "speaker_ui";
constexpr char kScreensaverMinutes[] = "screen_idle_min";
constexpr std::array kMinutes{1, 3, 5, 10};
constexpr std::array kTimeoutLabels{"1 分钟", "3 分钟", "5 分钟", "10 分钟"};

bool SaveTimeout(int minutes) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const bool saved = nvs_set_i32(handle, kScreensaverMinutes, minutes) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
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

    root_ = Panel(parent);
    Header(root_, "设置", SpeakerNavigation::OnBack, &navigation);
    struct Entry { const char* title; const char* icon; Page page; };
    constexpr Entry entries[] = {
        {"账号", MATERIAL_SYMBOLS_PERSON, Page::Account},
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
        lv_obj_set_pos(row, 45, 103 + static_cast<int>(index) * 107);
        lv_obj_set_size(row, 710, 91);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(kEdge), LV_PART_MAIN);
        lv_obj_align(Icon(row, entry.icon, kCyan, 340), LV_ALIGN_LEFT_MID, 32, 0);
        lv_obj_align(Label(row, entry.title), LV_ALIGN_LEFT_MID, 105, 0);
        lv_obj_align(Icon(row, MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT), LV_ALIGN_RIGHT_MID, -29, 0);
    }
    RegisterPage(navigation, Page::Settings, root_);
    CreateScreensaver(parent, navigation);
    CreateAbout(parent, navigation);
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
    if (!SaveTimeout(minutes)) {
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
        auto* button = timeout_buttons_[index];
        const bool selected = kMinutes[index] == screensaver_minutes_;
        lv_obj_set_style_bg_color(button, lv_color_hex(selected ? kCyan : kCard), LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0), lv_color_hex(selected ? kDeep : kText), LV_PART_MAIN);
    }
}

#endif
