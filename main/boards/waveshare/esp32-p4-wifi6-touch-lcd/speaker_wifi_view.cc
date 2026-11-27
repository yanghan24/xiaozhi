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

const char* SignalIcon(int signal) {
    switch (signal) {
        case 3:
            return MATERIAL_SYMBOLS_WIFI;
        case 2:
            return MATERIAL_SYMBOLS_WIFI_2_BAR;
        case 1:
            return MATERIAL_SYMBOLS_WIFI_1_BAR;
        default:
            return nullptr;
    }
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

    auto* networks = CreatePage(Page::Networks, "选择网络");
    auto* refresh = Button(networks, "刷新", [](lv_event_t* event) {
        auto& view = *static_cast<SpeakerWifiView*>(lv_event_get_user_data(event));
        if (!view.Busy()) {
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

    auto* password_page = CreatePage(Page::Password, "连接网络");
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
    connect_button_ = Button(password_page, "连接", [](lv_event_t* event) {
        static_cast<SpeakerWifiView*>(lv_event_get_user_data(event))->Connect("");
    }, this);
    lv_obj_set_pos(connect_button_, 45, 230);
    lv_obj_set_size(connect_button_, 710, 76);

    auto* phone_page = CreatePage(Page::Phone, "用手机配网");
    TextLine(phone_page, "手机连接热点", 46, 94, 706, kMuted);
    hotspot_name_ = TextLine(DetailCard(phone_page, 129), "", 25, 19, 654);
    TextLine(phone_page, "浏览器打开", 46, 222, 706, kMuted);
    hotspot_url_ = TextLine(DetailCard(phone_page, 258), "", 25, 19, 654);
    phone_status_ = TextLine(phone_page, "", 46, 357, 706, kMuted);

    gestures_.Create(root_, [](void* context) { static_cast<SpeakerWifiView*>(context)->Back(true); }, this);
    navigation.Register(SpeakerNavigation::Page::Wifi, [this] { Show(Page::Summary); }, [] {},
                        [this] { return !configuring_ && !Busy(); });
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
    if (Busy()) {
        return;
    }
    message_ = "";
    refresh_ = page == Page::Networks;
    if (page == Page::Phone && !configuring_) {
        operation_ = SpeakerWifiSetup::Request{Action::OpenHotspot, {}, {}};
    }
    last_poll_ = lv_tick_get() - 800;
    Show(page);
}

bool SpeakerWifiView::Busy() const {
    return operation_ || pending_action_ || starting_network_;
}

void SpeakerWifiView::Back(bool to_launcher) {
    if (Busy()) {
        return;
    }
    if (page_ == Page::Password && !to_launcher) {
        message_ = "";
        Show(Page::Networks);
        return;
    }
    if (page_ == Page::Phone && !configuring_) {
        operation_ = SpeakerWifiSetup::Request{Action::CloseHotspot, {}, {}};
        return_home_ = to_launcher;
        message_ = "正在关闭配网热点…";
        UpdateLabels();
        return;
    }
    if (to_launcher && !configuring_) {
        navigation_->Home();
    } else if (page_ != Page::Summary) {
        message_ = "";
        Show(Page::Summary);
    } else if (!configuring_) {
        navigation_->Back();
    }
}

void SpeakerWifiView::SelectNetwork(size_t index) {
    if (index >= networks_.size() || Busy()) {
        return;
    }
    const auto& selected = networks_[index];
    if (selected.connected) {
        return;
    }
    target_ = selected.ssid;
    secured_ = selected.secured;
    use_saved_ = selected.saved;
    lv_textarea_set_text(password_, "");
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
    message_ = "";
    Show(Page::Password);
}

void SpeakerWifiView::Connect(const char* password) {
    if (Busy()) {
        return;
    }
    if (use_saved_) {
        operation_ = SpeakerWifiSetup::Request{Action::ConnectSaved, target_, {}, secured_};
        message_ = "正在连接…";
    } else if (std::string secret = password; secured_ && (secret.empty() || secret.size() > 64)) {
        message_ = "请输入 Wi-Fi 密码，最多 64 字节";
    } else {
        operation_ = SpeakerWifiSetup::Request{Action::Connect, target_, std::move(secret)};
        message_ = "正在连接…";
        lv_textarea_set_text(password_, "");
    }
    UpdateLabels();
}

void SpeakerWifiView::ReadResult(SpeakerWifiSetup::Result result) {
    const bool hotspot_closed = !hotspot_.empty() && result.hotspot.empty();
    connected_ = std::move(result.connected);
    hotspot_ = std::move(result.hotspot);
    portal_url_ = std::move(result.portal_url);
    switch (result.action) {
        case Action::Inspect:
            if (hotspot_closed && page_ == Page::Phone && !configuring_ && !Busy()) {
                Show(Page::Summary);
            }
            break;
        case Action::OpenHotspot:
            if (!result.ok) {
                message_ = "暂未开启热点，请返回重试";
            }
            break;
        case Action::CloseHotspot:
            message_ = "";
            Show(Page::Summary);
            if (std::exchange(return_home_, false)) {
                navigation_->Home();
            }
            break;
        case Action::Scan:
        case Action::Refresh:
            if (result.ok && page_ == Page::Networks) {
                scan_seen_ = true;
                if (networks_ != result.networks) {
                    networks_ = std::move(result.networks);
                    RebuildNetworks();
                }
            }
            break;
        case Action::Connect:
        case Action::ConnectSaved:
            if (result.password_required) {
                use_saved_ = false;
                message_ = "无法使用已保存的密码，请重新输入";
            } else if (!result.ok) {
                message_ = result.action == Action::ConnectSaved
                    ? "连接失败，请检查网络后重试"
                    : "连接失败，请检查密码或信号后重试";
            } else if (configuring_) {
                starting_network_ = true;
                startup_connect_at_ = lv_tick_get();
                message_ = "网络已验证，正在连接…";
            } else {
                message_ = "";
                Show(Page::Summary);
            }
            break;
    }
}

void SpeakerWifiView::Update(bool visible, bool configuring) {
    auto& setup = SpeakerWifiSetup::Get();
    const bool startup_finished = configuring_ && !configuring;
    configuring_ = configuring;
    if (startup_finished) {
        starting_network_ = refresh_ = scan_seen_ = false;
        operation_.reset();
        networks_.clear();
        RebuildNetworks();
        message_ = "";
        Show(Page::Summary);
    }
    if (auto result = setup.TakeResult()) {
        pending_action_.reset();
        ReadResult(std::move(*result));
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
                pending_action_ = action;
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

    if (starting_network_ && lv_tick_elaps(startup_connect_at_) >= 65000) {
        starting_network_ = false;
        message_ = "未能联网，请重试或用手机配网";
    }
    if (!Busy() && page_ != Page::Password && (lv_tick_elaps(last_poll_) >= 800 || refresh_)) {
        const bool scan = page_ == Page::Networks;
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
    const bool needs_password = secured_ && !use_saved_;
    lv_obj_set_flag(keyboard_, LV_OBJ_FLAG_HIDDEN, !needs_password);
    lv_obj_set_flag(password_, LV_OBJ_FLAG_HIDDEN, !needs_password);
    lv_obj_set_flag(connect_button_, LV_OBJ_FLAG_HIDDEN, needs_password);
    for (auto* control : {keyboard_, password_, connect_button_}) {
        lv_obj_set_state(control, LV_STATE_DISABLED, Busy());
    }
    UpdateLabel(hotspot_name_, hotspot_.empty() ? "正在启动热点…" : hotspot_.c_str());
    UpdateLabel(hotspot_url_, portal_url_.c_str());
    UpdateLabel(phone_status_, *message_ ? message_ : "在手机网页上选网络并输入密码");
}

void SpeakerWifiView::RebuildNetworks() {
    constexpr int kRightInset = 22;
    constexpr int kLockWidth = 30;
    constexpr int kStatusGap = 16;
    const auto scroll = lv_obj_get_scroll_y(list_);
    lv_obj_clean(list_);
    for (size_t i = 0; i < networks_.size(); ++i) {
        const auto& network = networks_[i];
        if (i == 0 || network.saved != networks_[i - 1].saved) {
            auto* heading = Label(list_, network.saved ? "已保存" : "附近网络", kMuted);
            lv_obj_set_style_pad_left(heading, 21, LV_PART_MAIN);
        }
        auto* row = Control(list_, [](lv_event_t* event) {
            auto& view = *static_cast<SpeakerWifiView*>(lv_event_get_user_data(event));
            auto* button = lv_event_get_current_target_obj(event);
            view.SelectNetwork(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button)));
        }, this, network.connected ? kBlue : kCard);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(i));
        lv_obj_set_size(row, lv_pct(100), 65);
        auto* name = TextLine(row, network.ssid.c_str(), 21, 0, 560);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 21, 0);
        const auto status_color = network.connected ? kText : kMuted;
        if (network.secured) {
            lv_obj_align(Icon(row, MATERIAL_SYMBOLS_LOCK, status_color), LV_ALIGN_RIGHT_MID,
                         -kRightInset, 0);
        }
        if (const auto* signal = SignalIcon(network.signal)) {
            lv_obj_align(Icon(row, signal, status_color), LV_ALIGN_RIGHT_MID,
                         -(kRightInset + kLockWidth + kStatusGap), 0);
        }
    }
    lv_obj_scroll_to_y(list_, scroll, LV_ANIM_OFF);
}

#endif
