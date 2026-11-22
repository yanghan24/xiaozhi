#include "speaker_wifi_setup.h"

#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI

#include <algorithm>
#include <cJSON.h>
#include <esp_network.h>
#include <esp_wifi_types_generic.h>
#include <memory>
#include <utility>
#include <wifi_manager.h>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// First-boot touch setup shares the existing submission handler with the phone.
bool SubmitToStartupPortal(const SpeakerWifiSetup::Request& request) {
    Json payload(cJSON_CreateObject(), cJSON_Delete);
    if (!cJSON_AddStringToObject(payload.get(), "ssid", request.ssid.c_str()) ||
        !cJSON_AddStringToObject(payload.get(), "password", request.password.c_str()) ||
        !cJSON_AddBoolToObject(payload.get(), "exit", true)) {
        return false;
    }
    std::unique_ptr<char, decltype(&cJSON_free)> body(cJSON_PrintUnformatted(payload.get()), cJSON_free);
    if (!body) {
        return false;
    }
    EspNetwork network;
    auto http = network.CreateHttp(0);
    if (!http) {
        return false;
    }
    http->SetTimeout(60000);
    http->SetHeader("Content-Type", "application/json");
    http->SetContent(std::string(body.get()));
    if (!http->Open("POST", "http://127.0.0.1/submit") || http->GetStatusCode().value_or(0) != 200) {
        return false;
    }
    const auto response = http->ReadAll();
    http->Close();
    Json result(cJSON_ParseWithLength(response.c_str(), response.size() + 1), cJSON_Delete);
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result.get(), "success"));
}

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
            result.ok = !request.ssid.empty() && request.ssid.size() <= 32 && request.password.size() <= 64 &&
                (manager.IsConfigMode() ? SubmitToStartupPortal(request) :
                                           manager.TrySettingsNetwork(request.ssid, request.password));
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
