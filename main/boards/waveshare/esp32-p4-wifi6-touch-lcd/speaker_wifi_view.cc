#include "speaker_wifi_view.h"

#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI

#include "speaker_account/speaker_ui_widgets.h"
#include <utility>

using namespace SpeakerUi;

namespace {
using Action = SpeakerWifiSetup::Action;
using Submission = SpeakerWifiSetup::Submission;

lv_obj_t* TextLine(lv_obj_t* parent, const char* text, int x, int y, int width,
                   uint32_t color = kText) {
    auto* label = Label(parent, text, color);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

lv_obj_t* DetailCard(lv_obj_t* parent, int y) {
    auto* card = Surface(parent, 45, y, 710, 77);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    return card;
}
}  // namespace

lv_obj_t* SpeakerWifiView::CreatePage(Page page, const char* title) {
    auto* panel = Panel(root_);
    pages_[static_cast<size_t>(page)] = panel;
    auto* back = Header(panel, title, [](lv_event_t* event) {
        static_cast<SpeakerWifiView*>(lv_event_get_user_data(event))->Back();
    }, this);
    if (page == Page::Summary) {
        summary_back_ = back;
    }
    return panel;
}

void SpeakerWifiView::Create(lv_obj_t* parent, SpeakerNavigation& navigation) {
    navigation_ = &navigation;
    root_ = Surface(parent, 0, 0, kWidth, kHeight);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    auto* summary = CreatePage(Page::Summary, "Wi-Fi");
    auto* current = DetailCard(summary, 103);
    lv_obj_align(Icon(current, MATERIAL_SYMBOLS_WIFI, kCyan, 330), LV_ALIGN_LEFT_MID, 26, 0);
    network_name_ = TextLine(current, "未连接", 92, 0, 437);
    lv_obj_align(network_name_, LV_ALIGN_LEFT_MID, 92, 0);
    network_status_ = Label(current, "", kMuted);
    lv_obj_align(network_status_, LV_ALIGN_RIGHT_MID, -25, 0);
    auto* select = Button(summary, "选择网络", [](lv_event_t* event) {
        static_cast<SpeakerWifiView*>(lv_event_get_user_data(event))->Begin(Page::Networks);
    }, this);
    lv_obj_set_pos(select, 45, 202);
    lv_obj_set_size(select, 710, 77);
    choose_label_ = lv_obj_get_child(select, 0);
    auto* phone = Button(summary, "用手机配网", [](lv_event_t* event) {
        static_cast<SpeakerWifiView*>(lv_event_get_user_data(event))->Begin(Page::Phone);
    }, this);
    lv_obj_set_pos(phone, 45, 295);
    lv_obj_set_size(phone, 710, 77);
    lv_obj_align(Icon(select, MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT), LV_ALIGN_RIGHT_MID, -24, 0);
    lv_obj_align(Icon(phone, MATERIAL_SYMBOLS_KEYBOARD_ARROW_RIGHT), LV_ALIGN_RIGHT_MID, -24, 0);

    auto* networks = CreatePage(Page::Networks, "附近网络");
    auto* refresh = Button(networks, "刷新", [](lv_event_t* event) {
        auto& view = *static_cast<SpeakerWifiView*>(lv_event_get_user_data(event));
        if (!view.connecting_ && !view.exiting_) {
            view.refresh_ = true;
            view.message_ = "";
            view.UpdateLabels();
        }
    }, this);
    lv_obj_set_pos(refresh, 646, 15);
    lv_obj_set_size(refresh, 108, 51);
    list_status_ = TextLine(networks, "", 47, 82, 706, kMuted);
    list_ = Surface(networks, 45, 120, 710, 321);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 9, LV_PART_MAIN);

    auto* password_page = CreatePage(Page::Password, "输入密码");
    target_name_ = TextLine(password_page, "", 46, 78, 706, kMuted);
    password_ = lv_textarea_create(password_page);
    lv_obj_set_pos(password_, 45, 112);
    lv_obj_set_size(password_, 710, 52);
    lv_textarea_set_one_line(password_, true);
    lv_textarea_set_password_mode(password_, true);
    lv_textarea_set_max_length(password_, 64);
    lv_textarea_set_placeholder_text(password_, "Wi-Fi 密码");
    lv_obj_set_style_bg_color(password_, lv_color_hex(kCard), LV_PART_MAIN);
    lv_obj_set_style_border_color(password_, lv_color_hex(kEdge), LV_PART_MAIN);
    lv_obj_set_style_text_color(password_, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_set_style_text_color(password_, lv_color_hex(kMuted), LV_PART_TEXTAREA_PLACEHOLDER);
    password_status_ = TextLine(password_page, "", 46, 169, 706, kMuted);
    keyboard_ = lv_keyboard_create(password_page);
    lv_obj_set_size(keyboard_, 710, 244);
    lv_obj_align(keyboard_, LV_ALIGN_TOP_LEFT, 45, 202);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(kDeep), LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(kCard), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard_, lv_color_hex(kText), LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard_, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard_, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard_, 6, LV_PART_MAIN);
    lv_keyboard_set_textarea(keyboard_, password_);
    lv_obj_add_event_cb(keyboard_, [](lv_event_t* event) {
        auto& view = *static_cast<SpeakerWifiView*>(lv_event_get_user_data(event));
        view.Connect(lv_textarea_get_text(view.password_));
    }, LV_EVENT_READY, this);
    lv_obj_add_event_cb(keyboard_, [](lv_event_t* event) {
        static_cast<SpeakerWifiView*>(lv_event_get_user_data(event))->Back();
    }, LV_EVENT_CANCEL, this);

    auto* phone_page = CreatePage(Page::Phone, "用手机配网");
    TextLine(phone_page, "手机连接热点", 46, 94, 706, kMuted);
    hotspot_name_ = TextLine(DetailCard(phone_page, 129), "", 25, 19, 654);
    TextLine(phone_page, "浏览器打开", 46, 222, 706, kMuted);
    hotspot_url_ = TextLine(DetailCard(phone_page, 258), "", 25, 19, 654);
    phone_status_ = TextLine(phone_page, "", 46, 357, 706, kMuted);

    gestures_.Create(root_, [](void* context) { static_cast<SpeakerWifiView*>(context)->Back(true); }, this);
    navigation.Register(SpeakerNavigation::Page::Wifi, [this] { Show(Page::Summary); }, [] {},
                        [this] { return !entering_ && !configuring_ && !connecting_ && !exiting_; });
    Show(Page::Summary);
}

void SpeakerWifiView::Show(Page page) {
    if (page_ == Page::Password && page != Page::Password) {
        lv_textarea_set_text(password_, "");
    }
    for (size_t i = 0; i < pages_.size(); ++i) {
        lv_obj_set_flag(pages_[i], LV_OBJ_FLAG_HIDDEN, i != static_cast<size_t>(page));
    }
    page_ = page;
    UpdateLabels();
}

void SpeakerWifiView::Begin(Page page) {
    if (entering_ || connecting_ || exiting_) {
        return;
    }
    message_ = "";
    if (!configuring_) {
        previous_network_ = connected_;
        entering_ = true;
        entered_at_ = lv_tick_get();
        operation_ = SpeakerWifiSetup::Request{Action::Enter, {}, {}};
    }
    refresh_ = page == Page::Networks;
    Show(page);
}

void SpeakerWifiView::Back(bool to_launcher) {
    if (entering_ || connecting_ || exiting_) {
        return;
    }
    if (page_ == Page::Password && !to_launcher) {
        message_ = "";
        Show(Page::Networks);
        return;
    }
    if (configuring_ && !previous_network_.empty()) {
        operation_ = SpeakerWifiSetup::Request{Action::Leave, previous_network_, {}};
        exiting_ = true;
        exit_started_at_ = lv_tick_get();
        portal_closed_ = false;
        return_home_ = to_launcher;
        message_ = "正在恢复原网络…";
        UpdateLabels();
        return;
    }
    if (page_ != Page::Summary) {
        message_ = "";
        Show(Page::Summary);
    } else if (!configuring_) {
        if (to_launcher) {
            navigation_->Home();
        } else {
            navigation_->Back();
        }
    }
}

void SpeakerWifiView::SelectNetwork(size_t index) {
    if (index >= networks_.size() || entering_ || connecting_ || exiting_ || !configuring_) {
        return;
    }
    const auto& selected = networks_[index];
    target_ = selected.ssid;
    if (selected.secured) {
        lv_textarea_set_text(password_, "");
        lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
        message_ = "";
        Show(Page::Password);
    } else {
        Connect("");
    }
}

void SpeakerWifiView::Connect(const char* password) {
    if (connecting_ || exiting_ || !configuring_) {
        return;
    }
    std::string secret = password;
    if (page_ == Page::Password && (secret.empty() || secret.size() > 64)) {
        message_ = "请输入 Wi-Fi 密码，最多 64 字节";
    } else {
        operation_ = SpeakerWifiSetup::Request{Action::Connect, target_, std::move(secret)};
        connecting_ = true;
        message_ = "正在连接…";
        lv_textarea_set_text(password_, "");
    }
    UpdateLabels();
}

void SpeakerWifiView::ReadResult(SpeakerWifiSetup::Result result) {
    connected_ = std::move(result.connected);
    hotspot_ = std::move(result.hotspot);
    portal_url_ = std::move(result.portal_url);
    switch (result.action) {
        case Action::Inspect:
            break;
        case Action::Enter:
            if (!connected_.empty()) {
                previous_network_ = connected_;
            }
            if (!result.ok) {
                entering_ = false;
                message_ = "暂未进入配网，请返回重试";
            }
            break;
        case Action::Scan:
        case Action::Refresh:
            if (result.ok && configuring_) {
                scan_seen_ = true;
                if (networks_ != result.networks) {
                    networks_ = std::move(result.networks);
                    RebuildNetworks();
                }
            }
            break;
        case Action::Connect:
            connecting_ = false;
            if (configuring_) {
                exiting_ = result.ok;
                exit_started_at_ = lv_tick_get();
                portal_closed_ = false;
                message_ = result.ok ? "网络已验证，正在连接…" : "连接失败，请检查密码或信号后重试";
            }
            break;
        case Action::Leave:
            if (configuring_ && !result.ok) {
                exiting_ = false;
                return_home_ = false;
                message_ = "暂未恢复网络，请重试";
            }
            break;
    }
}

void SpeakerWifiView::Update(bool visible, bool configuring) {
    auto& setup = SpeakerWifiSetup::Get();
    if (configuring_ && !configuring) {
        const bool home = std::exchange(return_home_, false);
        entering_ = connecting_ = exiting_ = refresh_ = false;
        scan_seen_ = portal_closed_ = false;
        operation_.reset();
        previous_network_.clear();
        networks_.clear();
        RebuildNetworks();
        message_ = "";
        configuring_ = false;
        Show(Page::Summary);
        if (home) {
            navigation_->Home();
            visible = false;
        }
    }
    configuring_ = configuring;
    if (auto result = setup.TakeResult()) {
        ReadResult(std::move(*result));
        if (exiting_) {
            portal_closed_ |= hotspot_.empty();
        }
    }
    if (visible != shown_) {
        shown_ = visible;
        lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !visible);
        gestures_.SetVisible(visible);
        last_poll_ = lv_tick_get() - 800;
    }
    if (!visible) {
        return;
    }
    if (operation_) {
        const auto action = operation_->action;
        switch (setup.Submit(std::move(*operation_))) {
            case Submission::Accepted:
                operation_.reset();
                break;
            case Submission::Unavailable:
                operation_.reset();
                ReadResult({action, connected_, hotspot_, portal_url_});
                break;
            case Submission::Busy:
                break;
        }
    }

    if (entering_ && (configuring_ || lv_tick_elaps(entered_at_) >= 6000)) {
        entering_ = false;
        if (!configuring_) {
            message_ = "暂未进入配网，请返回重试";
        }
    }
    if (exiting_ && ((portal_closed_ && !hotspot_.empty()) || lv_tick_elaps(exit_started_at_) >= 65000)) {
        exiting_ = false;
        return_home_ = false;
        message_ = "未能联网，请重试或用手机配网";
    }
    if (!operation_ && !connecting_ && (page_ != Page::Password || exiting_) &&
        (lv_tick_elaps(last_poll_) >= 800 || (refresh_ && configuring_))) {
        const bool scan = !exiting_ && page_ == Page::Networks && configuring_ && !hotspot_.empty();
        const auto action = scan ? (refresh_ ? Action::Refresh : Action::Scan) : Action::Inspect;
        if (setup.Submit({action, {}, {}}) == Submission::Accepted) {
            last_poll_ = lv_tick_get();
            if (scan) {
                refresh_ = false;
            }
        }
    }
    UpdateLabels();
}

void SpeakerWifiView::UpdateLabels() {
    const bool connected = !connected_.empty() && !configuring_;
    UpdateLabel(network_name_, connected ? connected_.c_str() : "未连接");
    UpdateLabel(network_status_, connected ? "已连接" : "");
    UpdateLabel(choose_label_, connected ? "更换网络" : "选择网络");
    lv_obj_set_flag(summary_back_, LV_OBJ_FLAG_HIDDEN, configuring_);
    const char* scan_status = refresh_ || !scan_seen_ ? "正在扫描附近网络…" :
                                  networks_.empty() ? "未发现网络，可点刷新" : "";
    UpdateLabel(list_status_, *message_ ? message_ : scan_status);
    UpdateLabel(target_name_, target_.c_str());
    UpdateLabel(password_status_, message_);
    lv_obj_set_state(keyboard_, LV_STATE_DISABLED, connecting_ || exiting_);
    lv_obj_set_state(password_, LV_STATE_DISABLED, connecting_ || exiting_);
    UpdateLabel(hotspot_name_, hotspot_.empty() ? "正在启动热点…" : hotspot_.c_str());
    UpdateLabel(hotspot_url_, portal_url_.c_str());
    UpdateLabel(phone_status_, *message_ ? message_ : "在手机网页上选网络并输入密码");
}

void SpeakerWifiView::RebuildNetworks() {
    const auto scroll = lv_obj_get_scroll_y(list_);
    lv_obj_clean(list_);
    for (size_t i = 0; i < networks_.size(); ++i) {
        const auto& network = networks_[i];
        auto* row = Control(list_, [](lv_event_t* event) {
            auto& view = *static_cast<SpeakerWifiView*>(lv_event_get_user_data(event));
            auto* button = lv_event_get_current_target_obj(event);
            view.SelectNetwork(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
        }, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(i));
        lv_obj_set_size(row, lv_pct(100), 65);
        auto* name = TextLine(row, network.ssid.c_str(), 21, 0, 447);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 21, 0);
        if (network.secured) {
            lv_obj_align(Icon(row, MATERIAL_SYMBOLS_LOCK, kMuted), LV_ALIGN_RIGHT_MID, -104, 0);
        }
        const char* signal = network.rssi >= -60 ? "信号强" : network.rssi >= -75 ? "信号中" : "信号弱";
        lv_obj_align(Label(row, signal, kMuted), LV_ALIGN_RIGHT_MID, -22, 0);
    }
    lv_obj_scroll_to_y(list_, scroll, LV_ANIM_OFF);
}

#endif
