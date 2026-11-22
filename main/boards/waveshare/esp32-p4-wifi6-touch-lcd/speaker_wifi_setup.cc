#include "speaker_wifi_setup.h"

#if CONFIG_XIAOZHI_SPEAKER_TOUCH_WIFI

#include "application.h"
#include "wifi_board.h"

#include <algorithm>
#include <cJSON.h>
#include <esp_network.h>
#include <esp_wifi_types_generic.h>
#include <memory>
#include <utility>
#include <wifi_manager.h>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// The same HTTP task handles local touch requests and the phone portal. It owns
// the scan cache and serializes validation, connection, saving and exit.
Json PortalRequest(const char* path, const cJSON* payload = nullptr) {
    EspNetwork network;
    auto http = network.CreateHttp(0);
    if (!http) {
        return {nullptr, cJSON_Delete};
    }
    http->SetTimeout(payload ? 60000 : 5000);
    if (payload) {
        std::unique_ptr<char, decltype(&cJSON_free)> body(cJSON_PrintUnformatted(payload), cJSON_free);
        if (!body) {
            return {nullptr, cJSON_Delete};
        }
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::string(body.get()));
    }
    const auto url = std::string("http://127.0.0.1") + path;
    if (!http->Open(payload ? "POST" : "GET", url) || http->GetStatusCode().value_or(0) != 200) {
        return {nullptr, cJSON_Delete};
    }
    const auto body = http->ReadAll();
    http->Close();
    return {cJSON_ParseWithLength(body.c_str(), body.size() + 1), cJSON_Delete};
}

std::vector<SpeakerWifiSetup::Network> ReadNetworks(const cJSON* items) {
    std::vector<SpeakerWifiSetup::Network> networks;
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, items) {
        const char* ssid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "ssid"));
        const auto* signal = cJSON_GetObjectItemCaseSensitive(entry, "rssi");
        const auto* auth = cJSON_GetObjectItemCaseSensitive(entry, "authmode");
        if (ssid && *ssid && std::char_traits<char>::length(ssid) <= 32 && cJSON_IsNumber(signal) &&
            cJSON_IsNumber(auth)) {
            networks.push_back({ssid, signal->valueint, auth->valueint != WIFI_AUTH_OPEN});
        }
    }
    std::ranges::sort(networks, [](const auto& a, const auto& b) {
        if (a.ssid != b.ssid) {
            return a.ssid < b.ssid;
        }
        if (a.secured != b.secured) {
            return a.secured < b.secured;
        }
        return a.rssi > b.rssi;
    });
    const auto duplicates = std::ranges::unique(networks, [](const auto& a, const auto& b) {
        return a.ssid == b.ssid && a.secured == b.secured;
    });
    networks.erase(duplicates.begin(), duplicates.end());
    std::ranges::stable_sort(networks, std::greater{}, &SpeakerWifiSetup::Network::rssi);
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
    if (manager.IsConnected()) {
        result.connected = manager.GetSsid();
    }
    result.hotspot = manager.GetApSsid();
    result.portal_url = manager.GetApWebUrl();
    if (request.action == Action::Inspect) {
        result.ok = true;
        return result;
    }
    if (request.action == Action::Enter) {
        auto& app = Application::GetInstance();
        result.ok = !result.hotspot.empty() || app.GetDeviceState() == kDeviceStateIdle;
        if (result.ok && result.hotspot.empty()) {
            app.Schedule([] {
                if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
                    static_cast<WifiBoard&>(Board::GetInstance()).EnterWifiConfigMode();
                }
            });
        }
        return result;
    }
    if (result.hotspot.empty()) {
        return result;
    }
    if (request.action == Action::Scan || request.action == Action::Refresh) {
        const auto response = PortalRequest(request.action == Action::Refresh ? "/scan?refresh=1" : "/scan");
        const auto* items = cJSON_GetObjectItemCaseSensitive(response.get(), "aps");
        result.ok = cJSON_IsArray(items);
        if (result.ok) {
            result.networks = ReadNetworks(items);
        }
        return result;
    }

    Json form(cJSON_CreateObject(), cJSON_Delete);
    if (request.action == Action::Connect) {
        if (request.ssid.empty() || request.ssid.size() > 32 || request.password.size() > 64 ||
            !cJSON_AddStringToObject(form.get(), "ssid", request.ssid.c_str()) ||
            !cJSON_AddStringToObject(form.get(), "password", request.password.c_str()) ||
            !cJSON_AddBoolToObject(form.get(), "exit", true)) {
            return result;
        }
    } else if (!cJSON_AddStringToObject(form.get(), "resume", request.ssid.c_str())) {
        return result;
    }
    const auto response = PortalRequest(request.action == Action::Connect ? "/submit" : "/exit", form.get());
    result.ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response.get(), "success"));
    return result;
}

#endif
