#include "speaker_wifi_setup.h"

#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI

#include <algorithm>
#include <esp_wifi.h>
#include <ssid_manager.h>
#include <string_view>
#include <utility>
#include <wifi_manager.h>

namespace {
int SignalLevel(int rssi) {
    if (rssi >= -65) {
        return 3;
    }
    return rssi >= -75 ? 2 : 1;
}

auto FindSaved(const std::vector<SsidItem>& saved, std::string_view ssid, bool secured) {
    return std::ranges::find_if(saved, [&](const auto& item) {
        return item.ssid == ssid && (!item.password.empty()) == secured;
    });
}

std::vector<SpeakerWifiSetup::Network> ReadNetworks(const std::vector<wifi_ap_record_t>& records,
                                                   const std::string& connected) {
    const auto saved = SsidManager::GetInstance().GetSsidList();
    std::vector<SpeakerWifiSetup::Network> networks;
    if (!connected.empty()) {
        wifi_ap_record_t current{};
        if (esp_wifi_sta_get_ap_info(&current) == ESP_OK &&
            connected == reinterpret_cast<const char*>(current.ssid)) {
            const bool secured = current.authmode != WIFI_AUTH_OPEN;
            networks.push_back({connected, SignalLevel(current.rssi), secured, true, true});
        } else {
            const auto item = std::ranges::find(saved, connected, &SsidItem::ssid);
            const bool secured = item != saved.end() && !item->password.empty();
            networks.push_back({connected, 0, secured, true, true});
        }
    }
    for (const auto& record : records) {
        const auto* ssid = reinterpret_cast<const char*>(record.ssid);
        const bool secured = record.authmode != WIFI_AUTH_OPEN;
        if (!*ssid) {
            continue;
        }
        const auto entry = std::ranges::find_if(networks, [ssid, secured](const auto& network) {
            return network.ssid == ssid && network.secured == secured;
        });
        const int signal = SignalLevel(record.rssi);
        if (entry == networks.end()) {
            const bool known = FindSaved(saved, ssid, secured) != saved.end();
            networks.push_back({ssid, signal, secured, known, false});
        } else if (!entry->connected || entry->signal == 0) {
            entry->signal = std::max(entry->signal, signal);
        }
    }
    std::ranges::sort(networks, [](const auto& a, const auto& b) {
        if (a.connected != b.connected) {
            return a.connected;
        }
        if (a.saved != b.saved) {
            return a.saved;
        }
        if (a.signal != b.signal) {
            return a.signal > b.signal;
        }
        if (a.ssid != b.ssid) {
            return a.ssid < b.ssid;
        }
        return a.secured > b.secured;
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
    std::vector<wifi_ap_record_t> records;
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
            records = manager.SettingsNetworks(request.action == Action::Refresh);
            break;
        case Action::Connect:
            result.ok = manager.IsConfigMode()
                ? manager.SubmitConfigNetwork(request.ssid, request.password)
                : manager.TrySettingsNetwork(request.ssid, request.password);
            break;
        case Action::ConnectSaved: {
            wifi_ap_record_t current{};
            if (manager.IsConnected() && esp_wifi_sta_get_ap_info(&current) == ESP_OK &&
                request.ssid == reinterpret_cast<const char*>(current.ssid) &&
                request.secured == (current.authmode != WIFI_AUTH_OPEN)) {
                break;
            }
            const auto saved = SsidManager::GetInstance().GetSsidList();
            const auto item = FindSaved(saved, request.ssid, request.secured);
            if (item == saved.end()) {
                result.ok = false;
                result.password_required = request.secured;
                break;
            }
            uint8_t reason = 0;
            const auto minimum_auth = request.secured ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
            result.ok = manager.IsConfigMode()
                ? manager.SubmitConfigNetwork(request.ssid, item->password, &reason, minimum_auth)
                : manager.TrySettingsNetwork(request.ssid, item->password, &reason, minimum_auth);
            result.password_required = !result.ok && request.secured &&
                (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                 reason == WIFI_REASON_HANDSHAKE_TIMEOUT);
            break;
        }
    }
    if (manager.IsConnected()) {
        result.connected = manager.GetSsid();
    }
    if (request.action == Action::Scan || request.action == Action::Refresh) {
        result.networks = ReadNetworks(records, result.connected);
    }
    result.hotspot = manager.GetApSsid();
    result.portal_url = manager.GetApWebUrl();
    return result;
}

#endif
