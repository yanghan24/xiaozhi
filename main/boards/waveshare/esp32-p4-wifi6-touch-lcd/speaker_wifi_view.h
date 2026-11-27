#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI

#include "speaker_navigation.h"
#include "speaker_wifi_setup.h"

class SpeakerWifiView {
public:
    void Create(lv_obj_t* parent, SpeakerNavigation& navigation);
    void Update(bool visible, bool configuring);

private:
    enum class Page { Summary, Networks, Password, Phone, Count };

    lv_obj_t* CreatePage(Page page, const char* title);
    void Show(Page page);
    void Back(bool to_launcher = false);
    void Begin(Page page);
    bool Busy() const;
    void SelectNetwork(size_t index);
    void Connect(const char* password);
    void ReadResult(SpeakerWifiSetup::Result result);
    void UpdateLabels();
    void RebuildNetworks();

    SpeakerNavigation* navigation_ = nullptr;
    SpeakerEdgeGesture gestures_;
    lv_obj_t* root_ = nullptr;
    std::array<lv_obj_t*, static_cast<size_t>(Page::Count)> pages_{};
    lv_obj_t* summary_back_ = nullptr;
    lv_obj_t* network_name_ = nullptr;
    lv_obj_t* network_status_ = nullptr;
    lv_obj_t* choose_label_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* list_status_ = nullptr;
    lv_obj_t* target_name_ = nullptr;
    lv_obj_t* password_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;
    lv_obj_t* connect_button_ = nullptr;
    lv_obj_t* password_status_ = nullptr;
    lv_obj_t* hotspot_name_ = nullptr;
    lv_obj_t* hotspot_url_ = nullptr;
    lv_obj_t* phone_status_ = nullptr;
    Page page_ = Page::Summary;
    bool shown_ = false;
    bool configuring_ = false;
    bool return_home_ = false;
    bool refresh_ = false;
    bool scan_seen_ = false;
    bool secured_ = false;
    bool use_saved_ = false;
    bool starting_network_ = false;
    uint32_t startup_connect_at_ = 0;
    uint32_t last_poll_ = 0;
    std::string connected_;
    std::string hotspot_;
    std::string portal_url_;
    std::string target_;
    const char* message_ = "";
    std::vector<SpeakerWifiSetup::Network> networks_;
    std::optional<SpeakerWifiSetup::Request> operation_;
    std::optional<SpeakerWifiSetup::Action> pending_action_;
};

#endif
