#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_network.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <utility>

#include <font_awesome.h>
#include <wifi_manager.h>
#include <wifi_station.h>
#include <ssid_manager.h>
#include "afsk_demod.h"
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
#include "blufi.h"
#endif

static const char *TAG = "WifiBoard";
ESP_EVENT_DEFINE_BASE(WIFI_BOARD_STATION);

// Connection timeout in seconds
static constexpr int CONNECT_TIMEOUT_SEC = 60;

WifiBoard::WifiBoard() {
    // Create connection timeout timer
    esp_timer_create_args_t timer_args = {
        .callback = OnWifiConnectTimeout,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_connect_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&timer_args, &connect_timer_);
}

WifiBoard::~WifiBoard() {
    if (station_handler_)
        esp_event_handler_instance_unregister(WIFI_BOARD_STATION, ESP_EVENT_ANY_ID,
                                              station_handler_);
    if (connect_timer_) {
        esp_timer_stop(connect_timer_);
        esp_timer_delete(connect_timer_);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::StartNetwork() {
    auto& wifi_manager = WifiManager::GetInstance();

    // Initialize WiFi manager
    WifiManagerConfig config;
    config.ssid_prefix = "Xiaozhi";
    config.language = Lang::CODE;
    if (!wifi_manager.Initialize(config) && !wifi_manager.IsInitialized()) {
        SetWifiLink(WifiLinkState::Unsupported);
        return;
    }
    if (esp_event_handler_instance_register(
            WIFI_BOARD_STATION, ESP_EVENT_ANY_ID,
            [](void* arg, esp_event_base_t, int32_t action, void*) {
                static_cast<WifiBoard*>(arg)->RunStationAction(static_cast<StationAction>(action));
            },
            this, &station_handler_) != ESP_OK) {
        ESP_LOGE(TAG, "Could not register WiFi station actions");
        SetWifiLink(WifiLinkState::Unsupported);
        return;
    }

    // Set unified event callback - forward to NetworkEvent with SSID data
    wifi_manager.SetEventCallback([this](WifiEvent event, const std::string& data) {
        switch (event) {
            case WifiEvent::Scanning:
                OnNetworkEvent(NetworkEvent::Scanning);
                break;
            case WifiEvent::Connecting:
                OnNetworkEvent(NetworkEvent::Connecting, data);
                break;
            case WifiEvent::Connected:
                OnNetworkEvent(NetworkEvent::Connected, data);
                break;
            case WifiEvent::Disconnected:
                OnNetworkEvent(NetworkEvent::Disconnected);
                break;
            case WifiEvent::ConfigModeEnter:
                OnNetworkEvent(NetworkEvent::WifiConfigModeEnter);
                break;
            case WifiEvent::ConfigModeExit:
                OnNetworkEvent(NetworkEvent::WifiConfigModeExit);
                break;
        }
    });

    // Try to connect or enter config mode
    TryWifiConnect(true);
}

void WifiBoard::TryWifiConnect(bool boot) {
    if (!wifi_enabled_ || station_action_pending_)
        return;
    auto& ssid_manager = SsidManager::GetInstance();
    bool have_ssid = !ssid_manager.GetSsidList().empty();

    if (have_ssid) {
        // Start connection attempt with timeout
        ESP_LOGI(TAG, "Starting WiFi connection attempt");
        SetWifiLink(WifiLinkState::Connecting);
        PostStationAction(StationAction::Start);
    } else {
        // No SSID configured, enter config mode
        // Wait for the board version to be shown
        if (boot)
            vTaskDelay(pdMS_TO_TICKS(1500));
        StartWifiConfigMode();
    }
}

void WifiBoard::OnNetworkEvent(NetworkEvent event, const std::string& data) {
    if (!wifi_enabled_)
        return;
    switch (event) {
        case NetworkEvent::Connected:
            CancelConnectTimeout();
            SetWifiLink(WifiLinkState::Connected, data);
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
            // make sure blufi resources has been released
            Blufi::GetInstance().deinit();
#endif
            ESP_LOGI(TAG, "Connected to WiFi: %s", data.c_str());
            break;
        case NetworkEvent::Scanning:
            SetWifiLink(WifiLinkState::Connecting);
            ESP_LOGI(TAG, "WiFi scanning");
            break;
        case NetworkEvent::Connecting:
            SetWifiLink(WifiLinkState::Connecting, data);
            ESP_LOGI(TAG, "WiFi connecting to %s", data.c_str());
            break;
        case NetworkEvent::Disconnected:
            SetWifiLink(WifiLinkState::Connecting);
            ESP_LOGW(TAG, "WiFi disconnected");
            break;
        case NetworkEvent::WifiConfigModeEnter:
            ESP_LOGI(TAG, "WiFi config mode entered");
            break;
        case NetworkEvent::WifiConfigModeExit: {
            ESP_LOGI(TAG, "WiFi config mode exited");
            const auto session = wifi_session_.load();
            // The AP's HTTP task can finish provisioning while the user is
            // turning Wi-Fi off. Serialize the restart with the settings action.
            Application::GetInstance().Schedule([this, session]() {
                if (wifi_enabled_ && wifi_session_ == session &&
                    GetWifiLinkView().state == WifiLinkState::Provisioning)
                    TryWifiConnect();
            });
            break;
        }
        default:
            break;
    }

    // Notify external callback if set
    if (wifi_enabled_ && network_event_callback_) {
        network_event_callback_(event, data);
    }
}

void WifiBoard::SetNetworkEventCallback(NetworkEventCallback callback) {
    network_event_callback_ = std::move(callback);
}

void WifiBoard::OnWifiConnectTimeout(void* arg) {
    auto* board = static_cast<WifiBoard*>(arg);
    const auto deadline = board->connect_deadline_us_.load();
    if (deadline == 0 || esp_timer_get_time() < deadline)
        return;
    Application::GetInstance().Schedule([board, deadline]() {
        if (!board->wifi_enabled_ || board->connect_deadline_us_ != deadline ||
            WifiManager::GetInstance().IsConnected())
            return;
        ESP_LOGW(TAG, "WiFi connection timeout, entering config mode");
        board->ConfigureWifi();
    });
}

void WifiBoard::StartWifiConfigMode() {
    if (!wifi_enabled_)
        return;
    SetWifiLink(WifiLinkState::Provisioning);
    // Transition to wifi configuring state
    Application::GetInstance().SetDeviceState(kDeviceStateWifiConfiguring);
#ifdef CONFIG_USE_HOTSPOT_WIFI_PROVISIONING
    auto& wifi_manager = WifiManager::GetInstance();

    wifi_manager.StartConfigAp();
    SetWifiLink(WifiLinkState::Provisioning, wifi_manager.GetApSsid(), wifi_manager.GetApWebUrl());

    // Show config prompt after a short delay
    Application::GetInstance().Schedule([this, &wifi_manager]() {
        if (!wifi_enabled_ || GetWifiLinkView().state != WifiLinkState::Provisioning)
            return;
        std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
        hint += wifi_manager.GetApSsid();
        hint += Lang::Strings::ACCESS_VIA_BROWSER;
        hint += wifi_manager.GetApWebUrl();

        Application::GetInstance().Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_WIFICONFIG);
    });
#elif CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
    auto &blufi = Blufi::GetInstance();
    // initialize esp-blufi protocol
    blufi.init();
#endif
#if CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING
    // Start acoustic provisioning task
    auto codec = Board::GetInstance().GetAudioCodec();
    int channel = codec ? codec->input_channels() : 1;
    ESP_LOGI(TAG, "Starting acoustic WiFi provisioning, channels: %d", channel);

    xTaskCreate([](void* arg) {
        auto ch = reinterpret_cast<intptr_t>(arg);
        auto& app = Application::GetInstance();
        auto& wifi = WifiManager::GetInstance();
        auto disp = Board::GetInstance().GetDisplay();
        audio_wifi_config::ReceiveWifiCredentialsFromAudio(&app, &wifi, disp, ch);
        vTaskDelete(NULL);
    }, "acoustic_wifi", 4096, reinterpret_cast<void*>(channel), 2, NULL);
#endif
}

void WifiBoard::SetWifiLink(WifiLinkState state, const std::string& ssid,
                            const std::string& setup_url) {
    std::lock_guard<std::mutex> lock(wifi_link_mutex_);
    if (wifi_enabled_ == (state != WifiLinkState::Off))
        wifi_link_ = {state, ssid, setup_url};
}

WifiLinkView WifiBoard::GetWifiLinkView() const {
    std::lock_guard<std::mutex> lock(wifi_link_mutex_);
    return wifi_link_;
}

void WifiBoard::CancelConnectTimeout() {
    connect_deadline_us_ = 0;
    if (connect_timer_)
        esp_timer_stop(connect_timer_);
}

bool WifiBoard::PostStationAction(StationAction action) {
    if (station_action_pending_.exchange(true))
        return false;
    // WifiManager holds its lock when registering/unregistering station handlers.
    // Run those calls on the default event loop, which already owns the handler
    // lock, so a simultaneous got-IP/disconnect event cannot invert the locks.
    if (esp_event_post(WIFI_BOARD_STATION, static_cast<int32_t>(action), nullptr, 0,
                       pdMS_TO_TICKS(50)) == ESP_OK)
        return true;
    station_action_pending_ = false;
    ESP_LOGW(TAG, "Could not queue WiFi station action");
    return false;
}

void WifiBoard::RunStationAction(StationAction action) {
    auto& wifi = WifiManager::GetInstance();
    if (action == StationAction::Start) {
        constexpr int64_t delay = CONNECT_TIMEOUT_SEC * 1000000LL;
        CancelConnectTimeout();
        if (connect_timer_) {
            connect_deadline_us_ = esp_timer_get_time() + delay;
            esp_timer_start_once(connect_timer_, delay);
        }
        wifi.StartStation();
        station_action_pending_ = false;
        return;
    }
    CancelConnectTimeout();
    wifi.StopStation();
    if (action == StationAction::Reconfigure) {
        Application::GetInstance().Schedule([this]() {
            StartWifiConfigMode();
            station_action_pending_ = false;
        });
    } else {
        FinishWifiOff();
        station_action_pending_ = false;
    }
}

void WifiBoard::FinishWifiOff() {
    SetWifiLink(WifiLinkState::Off);
    if (network_event_callback_)
        network_event_callback_(NetworkEvent::Disconnected, "");
}

void WifiBoard::SetWifiEnabled(bool enabled) {
    auto& wifi = WifiManager::GetInstance();
    const auto state = GetWifiLinkView().state;
    if (state == WifiLinkState::Unsupported || station_action_pending_ ||
        wifi_enabled_.exchange(enabled) == enabled)
        return;
    ++wifi_session_;
    if (enabled) {
        TryWifiConnect();
        return;
    }
    if (state != WifiLinkState::Provisioning) {
        if (!PostStationAction(StationAction::Stop))
            wifi_enabled_ = true;
        return;
    }
    wifi.StopConfigAp();
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
    Blufi::GetInstance().deinit();
#endif
    FinishWifiOff();
}

void WifiBoard::ConfigureWifi() {
    const auto wifi_state = GetWifiLinkView().state;
    if (station_action_pending_ || wifi_state == WifiLinkState::Unsupported ||
        wifi_state == WifiLinkState::Provisioning)
        return;
    auto& app = Application::GetInstance();
    const auto state = app.GetDeviceState();
    if (state == kDeviceStateUpgrading || state == kDeviceStateActivating ||
        state == kDeviceStateAudioTesting)
        return;
    if (state == kDeviceStateSpeaking || state == kDeviceStateListening ||
        state == kDeviceStateConnecting)
        app.SetDeviceState(kDeviceStateIdle);
    const bool was_enabled = wifi_enabled_.exchange(true);
    ++wifi_session_;
    if (PostStationAction(StationAction::Reconfigure))
        app.ResetProtocol();
    else
        wifi_enabled_ = was_enabled;
}

void WifiBoard::EnterWifiConfigMode() {
    ESP_LOGI(TAG, "EnterWifiConfigMode called");
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);

    auto& app = Application::GetInstance();
    auto state = app.GetDeviceState();

    if (state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateIdle) {
        // Reset protocol (close audio channel, reset protocol)
        Application::GetInstance().ResetProtocol();

        xTaskCreate([](void* arg) {
            auto* board = static_cast<WifiBoard*>(arg);

            // Wait for 1 second to allow speaking to finish gracefully
            vTaskDelay(pdMS_TO_TICKS(1000));

            Application::GetInstance().Schedule([board]() { board->ConfigureWifi(); });

            vTaskDelete(NULL);
        }, "wifi_cfg_delay", 4096, this, 2, NULL);
        return;
    }

    if (state != kDeviceStateStarting) {
        ESP_LOGE(TAG, "EnterWifiConfigMode called but device state is not starting or speaking, device state: %d", state);
        return;
    }

    app.Schedule([this]() { ConfigureWifi(); });
}

bool WifiBoard::IsInWifiConfigMode() const {
    return WifiManager::GetInstance().IsConfigMode();
}

NetworkInterface* WifiBoard::GetNetwork() {
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon() {
    auto& wifi = WifiManager::GetInstance();

    if (wifi.IsConfigMode()) {
        return FONT_AWESOME_WIFI;
    }
    if (!wifi.IsConnected()) {
        return FONT_AWESOME_WIFI_SLASH;
    }

    int rssi = wifi.GetRssi();
    if (rssi >= -65) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -75) {
        return FONT_AWESOME_WIFI_FAIR;
    }
    return FONT_AWESOME_WIFI_WEAK;
}

std::string WifiBoard::GetBoardJson() {
    auto& wifi = WifiManager::GetInstance();
    std::string json = R"({"type":")" + std::string(BOARD_TYPE) + R"(",)";
    json += R"("name":")" + std::string(BOARD_NAME) + R"(",)";

    if (!wifi.IsConfigMode()) {
        json += R"("ssid":")" + wifi.GetSsid() + R"(",)";
        json += R"("rssi":)" + std::to_string(wifi.GetRssi()) + R"(,)";
        json += R"("channel":)" + std::to_string(wifi.GetChannel()) + R"(,)";
        json += R"("ip":")" + wifi.GetIpAddress() + R"(",)";
    }

    json += R"("mac":")" + SystemInfo::GetMacAddress() + R"("})";
    return json;
}

void WifiBoard::SetPowerSaveLevel(PowerSaveLevel level) {
    WifiPowerSaveLevel wifi_level;
    switch (level) {
        case PowerSaveLevel::LOW_POWER:
            wifi_level = WifiPowerSaveLevel::LOW_POWER;
            break;
        case PowerSaveLevel::BALANCED:
            wifi_level = WifiPowerSaveLevel::BALANCED;
            break;
        case PowerSaveLevel::PERFORMANCE:
        default:
            wifi_level = WifiPowerSaveLevel::PERFORMANCE;
            break;
    }
    WifiManager::GetInstance().SetPowerSaveLevel(wifi_level);
}

std::string WifiBoard::GetDeviceStatusJson() {
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    // Audio speaker
    auto audio_speaker = cJSON_CreateObject();
    if (auto codec = board.GetAudioCodec()) {
        cJSON_AddNumberToObject(audio_speaker, "volume", codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    // Screen
    auto screen = cJSON_CreateObject();
    if (auto backlight = board.GetBacklight()) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    if (auto display = board.GetDisplay(); display && display->height() > 64) {
        if (auto theme = display->GetTheme()) {
            cJSON_AddStringToObject(screen, "theme", theme->name().c_str());
        }
    }
    cJSON_AddItemToObject(root, "screen", screen);

    // Battery
    int level = 0;
    bool charging = false, discharging = false;
    if (board.GetBatteryLevel(level, charging, discharging)) {
        auto battery = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery, "level", level);
        cJSON_AddBoolToObject(battery, "charging", charging);
        cJSON_AddItemToObject(root, "battery", battery);
    }

    // Network
    auto& wifi = WifiManager::GetInstance();
    auto network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi.GetSsid().c_str());
    int rssi = wifi.GetRssi();
    const char* signal = rssi >= -60 ? "strong" : (rssi >= -70 ? "medium" : "weak");
    cJSON_AddStringToObject(network, "signal", signal);
    cJSON_AddItemToObject(root, "network", network);

    // Chip temperature
    float temp = 0.0f;
    if (board.GetTemperature(temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    auto str = cJSON_PrintUnformatted(root);
    std::string result(str);
    cJSON_free(str);
    cJSON_Delete(root);
    return result;
}
