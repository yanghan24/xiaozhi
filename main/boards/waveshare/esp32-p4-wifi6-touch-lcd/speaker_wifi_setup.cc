#include "speaker_wifi_setup.h"

#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI

#include <algorithm>
#include <esp_wifi_types_generic.h>
#include <utility>
#include <wifi_manager.h>

namespace {
std::vector<SpeakerWifiSetup::Network> ReadNetworks(const std::vector<wifi_ap_record_t>& records) {
    std::vector<SpeakerWifiSetup::Network> networks;
    for (const auto& record : records) {
        const auto* ssid = reinterpret_cast<const char*>(record.ssid);
        const bool secured = record.authmode != WIFI_AUTH_OPEN;
        if (!*ssid) {
            continue;
        }
        const auto entry = std::ranges::find_if(networks, [ssid, secured](const auto& network) {
            return network.ssid == ssid && network.secured == secured;
        });
        if (entry == networks.end()) {
            networks.push_back({ssid, record.rssi, secured});
        } else {
            entry->rssi = std::max(entry->rssi, static_cast<int>(record.rssi));
        }
    }
    std::ranges::sort(networks, [](const auto& a, const auto& b) {
        return a.rssi != b.rssi ? a.rssi > b.rssi : a.ssid < b.ssid;
    });
    return networks;
}
}  // namespace

SpeakerWifiSetup& SpeakerWifiSetup::Get() {
    static SpeakerWifiSetup setup;
    return setup;
}

SpeakerWifiSetup::Submission SpeakerWifiSetup::Submit(Request&& request) {
    std::lock_guard lock(mutex_);
    if (busy_) {
        return Submission::Busy;
    }
    if (!task_ && xTaskCreate([](void* context) { static_cast<SpeakerWifiSetup*>(context)->Run(); },
                              "speaker_wifi", 6144, this, 2, &task_) != pdPASS) {
        return Submission::Unavailable;
    }
    busy_ = true;
    pending_ = std::move(request);
    xTaskNotifyGive(task_);
    return Submission::Accepted;
}

std::optional<SpeakerWifiSetup::Result> SpeakerWifiSetup::TakeResult() {
    std::lock_guard lock(mutex_);
    if (!result_) {
        return std::nullopt;
    }
    busy_ = false;
    return std::exchange(result_, std::nullopt);
}

void SpeakerWifiSetup::Run() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        std::optional<Request> request;
        {
            std::lock_guard lock(mutex_);
            request = std::exchange(pending_, std::nullopt);
        }
        if (request) {
            auto result = Perform(*request);
            std::lock_guard lock(mutex_);
            result_ = std::move(result);
        }
    }
}

SpeakerWifiSetup::Result SpeakerWifiSetup::Perform(const Request& request) {
    Result result{request.action};
    auto& manager = WifiManager::GetInstance();
    result.ok = true;
    switch (request.action) {
        case Action::Inspect:
            break;
        case Action::OpenHotspot:
            result.ok = manager.StartSettingsHotspot();
            break;
        case Action::CloseHotspot:
            manager.StopSettingsHotspot();
            break;
        case Action::Scan:
        case Action::Refresh:
            result.networks = ReadNetworks(manager.SettingsNetworks(request.action == Action::Refresh));
            break;
        case Action::Connect:
            result.ok = manager.IsConfigMode()
                ? manager.SubmitConfigNetwork(request.ssid, request.password)
                : manager.TrySettingsNetwork(request.ssid, request.password);
            break;
    }
    if (manager.IsConnected()) {
        result.connected = manager.GetSsid();
    }
    result.hotspot = manager.GetApSsid();
    result.portal_url = manager.GetApWebUrl();
    return result;
}

#endif
