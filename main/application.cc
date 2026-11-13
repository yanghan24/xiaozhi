#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "device_control/delivery_bootstrap.h"
#include "device_control/device_identity.h"
#include "device_control/maintenance_runtime.h"
#include "display.h"
#include "features/audio_recorder.h"
#include "features/music_player.h"
#include "features/pomodoro_timer.h"
#include "mcp_server.h"
#include "mqtt_protocol.h"
#include "services/environment_service.h"
#include "services/iot_telemetry_service.h"
#include "settings.h"
#include "system_info.h"
#include "websocket_protocol.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"


Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    std::lock_guard<std::mutex> gate(maintenance_gate_);
    if (maintenance_reserved_ && state != kDeviceStateUpgrading)
        return false;
    if ((state == kDeviceStateConnecting || state == kDeviceStateListening ||
         state == kDeviceStateSpeaking) &&
        !VoiceAllowed()) {
        return false;
    }
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    auto& control = DeviceControl::GetInstance();
    if (control.Enabled()) {
        device_control::InitializeMaintenance();
        display->SetAccountAction([&control]() { control.RequestAccount(); });
        control.SetCallbacks(
            [this](const DeviceControlView&) {
                Schedule(
                    [this]() { ApplyDeviceControlView(DeviceControl::GetInstance().GetView()); });
            },
            [this](uint64_t generation) {
                audio_service_.RevokeControlledAccess(generation);
                xEventGroupSetBits(event_group_, MAIN_EVENT_CONTROL_REVOKED);
            });
    }

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    callbacks.on_playback_text = [this](const std::string& text, uint64_t generation,
                                        uint64_t connection_id) {
        ScheduleVoice(generation, connection_id, [this, text]() {
            DeviceState state = GetDeviceState();
            bool voice_screen_active = state == kDeviceStateConnecting ||
                                       state == kDeviceStateListening ||
                                       state == kDeviceStateSpeaking;
            if (standby_requested_.load() || !voice_screen_active) {
                return;
            }
            Board::GetInstance().GetDisplay()->SetChatMessage("assistant", text.c_str());
        });
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Start the independent IoT HTTP mirror before networking. It uses its own
    // low-priority worker and never replaces the Xiaozhi AI protocol connection.
    IotTelemetryService::GetInstance().Start();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                IotTelemetryService::GetInstance().SetNetworkConnected(true);
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                IotTelemetryService::GetInstance().SetNetworkConnected(false);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS =
        MAIN_EVENT_SCHEDULE | MAIN_EVENT_SEND_AUDIO | MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE | MAIN_EVENT_CLOCK_TICK | MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED | MAIN_EVENT_NETWORK_DISCONNECTED | MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING | MAIN_EVENT_STOP_LISTENING | MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED | MAIN_EVENT_CONTROL_REVOKED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_CONTROL_REVOKED) {
            HandleControlRevoked();
        }
        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (!VoiceGenerationCurrent(packet->generation)) {
                    continue;
                }
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
            if (DeviceControl::GetInstance().Enabled()) {
                ApplyDeviceControlView(DeviceControl::GetInstance().GetView());
            }

            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    const auto wifi = Board::GetInstance().GetWifiLinkView().state;
    if (wifi != WifiLinkState::Unsupported && wifi != WifiLinkState::Connected)
        return;  // An earlier got-IP event can be queued when Wi-Fi is turned off.
    ESP_LOGI(TAG, "Network connected");
    device_control::ReportDeliveryHardware();
    DeviceControl::GetInstance().NotifyNetwork(true);
    EnvironmentService::GetInstance().RefreshAsync(true);
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    DeviceControl::GetInstance().NotifyNetwork(false);
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        if (protocol_) {
            protocol_->CloseAudioChannel();
        }
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    if (!DeviceControl::GetInstance().Enabled()) {
        display->SetChatMessage("system", "");
    }

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    if (DeviceControl::GetInstance().Enabled()) {
        ApplyDeviceControlView(DeviceControl::GetInstance().GetView());
    } else {
        Schedule([this]() { audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS); });
    }
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Protocol ownership stays on the main task, including asynchronous OTA completion.
    Schedule([this]() {
        InitializeProtocol();
        xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
    });
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [this, display](int progress, size_t speed) -> void {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            Schedule([display, message = std::string(buffer)]() {
                display->SetChatMessage("system", message.c_str());
            });
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            if (DeviceControl::GetInstance().Enabled() && err == ESP_ERR_INVALID_ARG) {
                // Missing/invalid delivery configuration needs maintenance, not
                // the upstream OTA retry loop before the blocked screen appears.
                DeviceControl::GetInstance().Start(ota_->GetCheckVersionUrl(),
                                                   ota_->GetDeviceControlUrl());
                return;
            }
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (DeviceControl::GetInstance().Enabled()) {
            DeviceControl::GetInstance().Start(ota_->GetCheckVersionUrl(),
                                               ota_->GetDeviceControlUrl());
            break;  // Product devices use the authenticated QR contract, never numeric activation.
        }
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (DeviceControl::GetInstance().Enabled()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else if (ota_->HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_->HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    const std::string voice_transport =
        dynamic_cast<WebsocketProtocol*>(protocol_.get()) != nullptr ? "websocket" : "mqtt_udp";

    protocol_->OnConnected([this]() {
        if (VoiceAllowed()) {
            DismissAlert();
        }
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });

    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (VoiceGenerationCurrent(packet->generation) &&
            packet->connection_id == GetVoiceConnectionId() &&
            GetDeviceState() == kDeviceStateSpeaking) {
            {
                std::lock_guard<std::mutex> lock(pending_tts_text_mutex_);
                if (!pending_tts_text_.empty() && pending_tts_generation_ == packet->generation &&
                    pending_tts_connection_ == packet->connection_id) {
                    packet->playback_text = std::move(pending_tts_text_);
                }
                pending_tts_text_.clear();
            }
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });

    protocol_->OnAudioChannelOpened([this, codec, &board, voice_transport]() {
        if (!VoiceAllowed()) {
            return;
        }
        audio_service_.SetVoiceConnection(GetVoiceConnectionId());
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        const std::string session_id = protocol_ != nullptr ? protocol_->session_id() : "";
        IotTelemetryService::GetInstance().ReportVoiceConnectionChanged(true, voice_transport,
                                                                        session_id);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });

    protocol_->OnAudioChannelClosed(
        [this, &board, voice_transport](uint64_t generation, uint64_t connection_id) {
            audio_service_.SetVoiceConnection(0);
            ScheduleVoice(generation, connection_id, [this, &board, voice_transport]() {
                const std::string session = protocol_ ? protocol_->session_id() : "";
                IotTelemetryService::GetInstance().ReportVoiceConnectionChanged(
                    false, voice_transport, session, "audio_channel_closed");
                board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
                auto display = Board::GetInstance().GetDisplay();
                display->SetChatMessage("system", "");
                SetDeviceState(kDeviceStateIdle);
            });
        });

    protocol_->OnIncomingJson([this, display](const cJSON* root, uint64_t generation,
                                              uint64_t connection_id) {
        if (!VoiceGenerationCurrent(generation)) {
            return;
        }
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            return;
        }
        if (standby_requested_.load() &&
            (strcmp(type->valuestring, "tts") == 0 || strcmp(type->valuestring, "stt") == 0 ||
             strcmp(type->valuestring, "llm") == 0)) {
            ESP_LOGI(TAG, "Ignoring stale %s message after entering standby", type->valuestring);
            return;
        }
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (!cJSON_IsString(state)) {
                return;
            }
            auto tts_text = cJSON_GetObjectItem(root, "text");
            const std::string session_id = protocol_ != nullptr ? protocol_->session_id() : "";
            IotTelemetryService::GetInstance().ReportTtsState(
                state->valuestring, cJSON_IsString(tts_text) ? tts_text->valuestring : "",
                session_id);
            if (strcmp(state->valuestring, "start") == 0) {
                {
                    std::lock_guard<std::mutex> lock(pending_tts_text_mutex_);
                    pending_tts_text_.clear();
                }
                ScheduleVoice(generation, connection_id, [this]() {
                    if (standby_requested_.load()) {
                        return;
                    }
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                ScheduleVoice(generation, connection_id, [this]() {
                    if (standby_requested_.load()) {
                        return;
                    }
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                if (cJSON_IsString(tts_text)) {
                    ESP_LOGI(TAG, "<< %s", tts_text->valuestring);
                    std::lock_guard<std::mutex> lock(pending_tts_text_mutex_);
                    pending_tts_text_ = tts_text->valuestring;
                    pending_tts_generation_ = generation;
                    pending_tts_connection_ = connection_id;
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                const std::string session_id = protocol_ != nullptr ? protocol_->session_id() : "";
                IotTelemetryService::GetInstance().ReportQuestion(text->valuestring, session_id);
                ScheduleVoice(generation, connection_id,
                              [this, display, message = std::string(text->valuestring)]() {
                                  if (standby_requested_.load()) {
                                      return;
                                  }
                                  display->SetChatMessage("user", message.c_str());
                              });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                ScheduleVoice(generation, connection_id,
                              [this, display, emotion_str = std::string(emotion->valuestring)]() {
                                  if (standby_requested_.load()) {
                                      return;
                                  }
                                  display->SetEmotion(emotion_str.c_str());
                              });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                auto payload_json = cJSON_PrintUnformatted(payload);
                if (payload_json != nullptr) {
                    ScheduleVoice(
                        generation, connection_id,
                        [generation, connection_id, message = std::string(payload_json)]() {
                            McpServer::GetInstance().ParseMessage(message, generation,
                                                                  connection_id);
                        });
                    cJSON_free(payload_json);
                }
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    ScheduleVoice(generation, connection_id, [this]() { Reboot(); });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                ScheduleVoice(generation, connection_id,
                              [this, status = std::string(status->valuestring),
                               message = std::string(message->valuestring),
                               emotion = std::string(emotion->valuestring)]() {
                                  Alert(status.c_str(), message.c_str(), emotion.c_str(),
                                        DeviceControl::GetInstance().Enabled()
                                            ? std::string_view{}
                                            : Lang::Sounds::OGG_VIBRATION);
                              });
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                ScheduleVoice(
                    generation, connection_id,
                    [this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                        display->SetChatMessage("system", payload_str.c_str());
                    });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    standby_requested_.store(false);
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    standby_requested_.store(false);
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    if (!VoiceAllowed() && GetDeviceState() != kDeviceStateWifiConfiguring &&
        GetDeviceState() != kDeviceStateAudioTesting) {
        DeviceControl::GetInstance().Refresh();
        Board::GetInstance().GetDisplay()->ShowDeviceBinding(
            DeviceControl::GetInstance().GetView());
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            ScheduleVoice(DeviceControl::GetInstance().GetGeneration(),
                          [this, mode]() { ContinueOpenAudioChannel(mode); });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    if (!VoiceAllowed()) {
        DeviceControl::GetInstance().Refresh();
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    if (!VoiceAllowed() && GetDeviceState() != kDeviceStateWifiConfiguring &&
        GetDeviceState() != kDeviceStateAudioTesting) {
        DeviceControl::GetInstance().Refresh();
        Board::GetInstance().GetDisplay()->ShowDeviceBinding(
            DeviceControl::GetInstance().GetView());
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            ScheduleVoice(DeviceControl::GetInstance().GetGeneration(),
                          [this]() { ContinueOpenAudioChannel(kListeningModeManualStop); });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!VoiceAllowed() && GetDeviceState() != kDeviceStateWifiConfiguring &&
        GetDeviceState() != kDeviceStateAudioTesting) {
        DeviceControl::GetInstance().Refresh();
        Board::GetInstance().GetDisplay()->ShowDeviceBinding(
            DeviceControl::GetInstance().GetView());
        return;
    }

    standby_requested_.store(false);
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), (int)state);

    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();
        auto wake_word = audio_service_.GetLastWakeWord();

        if (!SetDeviceState(kDeviceStateConnecting)) {
            audio_service_.EnableWakeWordDetection(VoiceAllowed());
            return;
        }
        // Always continue after entering Connecting. This is also required when
        // standby kept the existing audio channel open for the MCP tool result.
        ScheduleVoice(DeviceControl::GetInstance().GetGeneration(),
                      [this, wake_word]() { ContinueWakeWordInvoke(wake_word); });
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Drop the interrupted answer before AutoStop starts the next turn.
        // Otherwise Listening waits for the old playback queue to drain.
        audio_service_.ResetDecoder();
        // Clear send queue to avoid sending residues to server
        while (audio_service_.PopPacketFromSendQueue());

        if (state == kDeviceStateListening) {
            protocol_->SendStartListening(GetDefaultListeningMode());
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // An interruption opens the next turn silently. Only an idle wake
            // sends listen/detect and requests the server's spoken greeting.
            play_popup_on_listening_ = false;
            SetListeningMode(GetDefaultListeningMode());
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    if (!VoiceAllowed()) {
        DeviceControl::GetInstance().Refresh();
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            SetDeviceState(kDeviceStateIdle);
            audio_service_.EnableWakeWordDetection(VoiceAllowed());
            return;
        }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
    IotTelemetryService::GetInstance().ReportWakeWord(
        wake_word, protocol_ != nullptr ? protocol_->session_id() : "");
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();

    if (!VoiceAllowed() && new_state != kDeviceStateWifiConfiguring &&
        new_state != kDeviceStateAudioTesting && new_state != kDeviceStateUpgrading &&
        new_state != kDeviceStateStarting) {
        audio_service_.EnableVoiceProcessing(false);
        audio_service_.EnableWakeWordDetection(false);
        display->ShowDeviceBinding(DeviceControl::GetInstance().GetView());
        return;
    }
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle: {
            std::lock_guard<std::mutex> lock(pending_tts_text_mutex_);
            pending_tts_text_.clear();
        }
            audio_service_.NotifyVoiceInteractionStopped();
            display->SetStatus(Lang::Strings::STANDBY);
            display->ClearChatMessages();  // Clear messages first
            display->SetEmotion("neutral"); // Then set emotion (wechat mode checks child count)
            audio_service_.EnableVoiceProcessing(false);
            // Local music only needs the speaker. Keep the microphone/AFE stopped
            // while it owns output focus to avoid filling the wake-word ringbuffer.
            audio_service_.EnableWakeWordDetection(!audio_service_.HasAudioFocus(
                kAudioFocusOwnerLocalMusic, kAudioFocusResourceOutput));
            break;
        case kDeviceStateConnecting:
            audio_service_.NotifyVoiceInteractionStarted();
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            audio_service_.NotifyVoiceInteractionStarted();
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                // For auto mode, wait for playback queue to be empty before enabling voice processing
                // This prevents audio truncation when STOP arrives late due to network jitter
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }
                
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            // Enable wake word detection in listening mode (configured via Kconfig)
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
            // Disable wake word detection in listening mode
            audio_service_.EnableWakeWordDetection(false);
#endif
            
            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP, true);
            }
            break;
        case kDeviceStateSpeaking:
            audio_service_.NotifyVoiceInteractionStarted();
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
        case kDeviceStateAudioTesting:
            audio_service_.NotifyVoiceInteractionStopped();
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::EndVoiceInteraction() {
    auto state = GetDeviceState();
    if (state != kDeviceStateConnecting && state != kDeviceStateListening &&
        state != kDeviceStateSpeaking) {
        return;
    }

    ESP_LOGI(TAG, "End voice interaction");
    if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening && protocol_) {
        protocol_->SendStopListening();
    }

    audio_service_.ResetDecoder();
    audio_service_.NotifyVoiceInteractionStopped();
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(VoiceAllowed());
    SetDeviceState(kDeviceStateIdle);
}

bool Application::EnterStandby() {
    standby_requested_.store(true);
    ESP_LOGI(TAG, "Entering standby and stopping active application features");

    MusicPlayer::GetInstance().Stop();
    AudioRecorder::GetInstance().Stop();
    PomodoroTimer::GetInstance().Cancel();

    auto state = GetDeviceState();
    if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening && protocol_) {
        protocol_->SendStopListening();
    }

    audio_service_.ResetDecoder();
    audio_service_.NotifyVoiceInteractionStopped();
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(VoiceAllowed());

    if (state != kDeviceStateIdle && !SetDeviceState(kDeviceStateIdle)) {
        standby_requested_.store(false);
        ESP_LOGW(TAG, "Unable to enter standby from state %s",
                 DeviceStateMachine::GetStateName(state));
        return false;
    }

    auto& board = Board::GetInstance();
    auto* display = board.GetDisplay();
    if (display != nullptr) {
        display->CloseStandbyFeatureViews();
        display->ClearPreviewImage();
        display->ClearChatMessages();
        display->SetEmotion("neutral");
        display->SetStatus(Lang::Strings::STANDBY);
    }
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
    EnvironmentService::GetInstance().RefreshAsync(false);
    return true;
}

void Application::SetListeningMode(ListeningMode mode) {
    if (!VoiceAllowed()) {
        return;
    }
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    // Keep echo cancellation independent of turn-taking. Auto-stop uses the
    // existing AFE wake-word path while ordinary voice processing is off during playback.
    return kListeningModeAutoStop;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::CanRestoreFactorySettings() const {
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    const auto state = GetDeviceState();
    return (state == kDeviceStateIdle || state == kDeviceStateConnecting ||
            state == kDeviceStateListening || state == kDeviceStateSpeaking) &&
           !DeviceControl::GetInstance().GetView().sign_out_pending;
#else
    return false;
#endif
}

bool Application::RestoreFactorySettings() {
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    // The settings dialog schedules this on the main task and confirms locally.
    if (!CanRestoreFactorySettings()) {
        Alert("暂时无法恢复", "请等待当前操作完成后重试", "circle_xmark");
        return false;
    }
    MusicPlayer::GetInstance().Stop();
    AudioRecorder::GetInstance().Stop();
    PomodoroTimer::GetInstance().Cancel();
    EndVoiceInteraction();
    if (protocol_ && protocol_->IsAudioChannelOpened())
        protocol_->CloseAudioChannel();
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(false);
    const auto result = device_control::ResetUserSettings();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Local settings reset failed: %s", esp_err_to_name(result));
        audio_service_.EnableWakeWordDetection(VoiceAllowed());
        Alert("恢复失败", "本机设置恢复失败，请重试", "circle_xmark");
        return false;
    }
    // Keep the installed application and all cloud ownership/permission state.
    Reboot();
    return true;
#else
    return false;
#endif
}

bool Application::MaintenanceIdle() {
    std::lock_guard<std::mutex> gate(maintenance_gate_);
    auto state = GetDeviceState();
    return !upgrade_active_ &&
           ((maintenance_reserved_ && state == kDeviceStateUpgrading) ||
            state == kDeviceStateIdle) &&
           audio_service_.IsIdle() &&
           MusicPlayer::GetInstance().GetStatus().state != MusicPlayerState::kPlaying &&
           AudioRecorder::GetInstance().GetStatus().state != AudioRecorderState::kRecording;
}

bool Application::ReserveMaintenance() {
    std::lock_guard<std::mutex> gate(maintenance_gate_);
    auto state = GetDeviceState();
    if (maintenance_reserved_ || upgrade_active_ || state != kDeviceStateIdle ||
        !audio_service_.IsIdle() ||
        MusicPlayer::GetInstance().GetStatus().state == MusicPlayerState::kPlaying ||
        AudioRecorder::GetInstance().GetStatus().state == AudioRecorderState::kRecording)
        return false;
    maintenance_previous_state_ = state;
    maintenance_reserved_ = true;
    DeviceControl::GetInstance().SetMaintenanceActive(true);
    if (!state_machine_.TransitionTo(kDeviceStateUpgrading)) {
        maintenance_reserved_ = false;
        DeviceControl::GetInstance().SetMaintenanceActive(false);
        return false;
    }
    return true;
}

void Application::ReleaseMaintenance() {
    std::lock_guard<std::mutex> gate(maintenance_gate_);
    if (!maintenance_reserved_)
        return;
    maintenance_reserved_ = false;
    DeviceControl::GetInstance().SetMaintenanceActive(false);
    state_machine_.TransitionTo(maintenance_previous_state_);
}

bool Application::UpgradeMaintenance(const device_control::MaintenanceCommand& command) {
    // Executed on the main application task after the server confirmed durable
    // acceptance. Recheck both the exclusive idle gate and the monotonic lease.
    {
        std::lock_guard<std::mutex> gate(maintenance_gate_);
        if (!maintenance_reserved_ || GetDeviceState() != kDeviceStateUpgrading ||
            !audio_service_.IsIdle() || !DeviceControl::GetInstance().IsNetworkConnected() ||
            esp_timer_get_time() / 1000 >= command.deadline_ms)
            return false;
    }
    Ota::Validation expected{command.artifact_sha256, command.target_elf_sha256, command.image_size,
                             command.deadline_ms};
    return UpgradeFirmware(command.url, "", &expected);
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version,
                                  const Ota::Validation* validation) {
    {
        std::lock_guard<std::mutex> gate(maintenance_gate_);
        if (upgrade_active_ || (maintenance_reserved_ && !validation))
            return false;
        // Reserve before the alert/network work: an older MCP upgrade and a
        // management offer must never both acquire the inactive flash slot.
        upgrade_active_ = true;
    }
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade");

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(
        upgrade_url,
        [this, display](int progress, size_t speed) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            Schedule([display, message = std::string(buffer)]() {
                display->SetChatMessage("system", message.c_str());
            });
        },
        validation);

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        {
            std::lock_guard<std::mutex> gate(maintenance_gate_);
            upgrade_active_ = false;
            if (!maintenance_reserved_)
                state_machine_.TransitionTo(kDeviceStateIdle);
        }
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!VoiceAllowed()) {
        DeviceControl::GetInstance().Refresh();
        return;
    }
    standby_requested_.store(false);
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!SetDeviceState(kDeviceStateConnecting)) {
            audio_service_.EnableWakeWordDetection(VoiceAllowed());
            return;
        }
        ScheduleVoice(DeviceControl::GetInstance().GetGeneration(),
                      [this, wake_word]() { ContinueWakeWordInvoke(wake_word); });
    } else if (state == kDeviceStateSpeaking) {
        ScheduleVoice(DeviceControl::GetInstance().GetGeneration(),
                      [this]() { AbortSpeaking(kAbortReasonNone); });
    } else if (state == kDeviceStateListening) {
        ScheduleVoice(DeviceControl::GetInstance().GetGeneration(), [this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback) {
    mcp_broadcast_callback_ = std::move(callback);
}

void Application::SendMcpMessage(const std::string& payload, uint64_t generation,
                                 uint64_t connection_id) {
    // Preserve the request's source through the final queued reply.
    ScheduleVoice(generation, connection_id, [this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
        if (mcp_broadcast_callback_) {
            mcp_broadcast_callback_(payload);
        }
    });
}

bool Application::VoiceAllowed() const {
    auto& control = DeviceControl::GetInstance();
    return !control.Enabled() || (control.IsVoiceAllowed() &&
                                  applied_control_generation_.load() == control.GetGeneration());
}

bool Application::VoiceGenerationCurrent(uint64_t generation) const {
    auto& control = DeviceControl::GetInstance();
    return !control.Enabled() ||
           (control.IsVoiceAllowed() && control.IsCurrentGeneration(generation));
}

void Application::ScheduleVoice(uint64_t generation, std::function<void()>&& callback) {
    ScheduleVoice(generation, GetVoiceConnectionId(), std::move(callback));
}

void Application::ScheduleVoice(uint64_t generation, uint64_t connection_id,
                                std::function<void()>&& callback) {
    Schedule([this, generation, connection_id, callback = std::move(callback)]() {
        if (VoiceGenerationCurrent(generation) && GetVoiceConnectionId() == connection_id) {
            callback();
        }
    });
}

void Application::HandleControlRevoked() {
    if (VoiceAllowed()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(pending_tts_text_mutex_);
        pending_tts_text_.clear();
    }
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(false);
    // These local producers must not resume their old work after re-login.
    MusicPlayer::GetInstance().Stop();
    AudioRecorder::GetInstance().Stop();
    if (protocol_) {
        protocol_->CloseAudioChannel();
    }
    if (GetDeviceState() == kDeviceStateConnecting || GetDeviceState() == kDeviceStateListening ||
        GetDeviceState() == kDeviceStateSpeaking) {
        SetDeviceState(kDeviceStateIdle);
    }
    audio_service_.StopControlledAudio();
}

void Application::ApplyDeviceControlView(const DeviceControlView& view) {
    auto& control = DeviceControl::GetInstance();
    if (!control.Enabled() || view.generation != control.GetGeneration()) {
        return;
    }
    const bool allowed = view.voice_allowed && control.IsVoiceAllowed();
    if (view.generation != applied_control_generation_) {
        if (!allowed) {
            HandleControlRevoked();
        } else {
            // Finish stopping the old context before allowing any work for this login.
            audio_service_.RevokeControlledAccess(view.generation);
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            MusicPlayer::GetInstance().Stop();
            AudioRecorder::GetInstance().Stop();
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
            if (!audio_service_.StopControlledAudio()) {
                return;  // The next control/clock event retries the driver barrier.
            }
            SetDeviceState(kDeviceStateIdle);
            audio_service_.EnableWakeWordDetection(true);
        }
        applied_control_generation_ = view.generation;
    }
    audio_service_.ApplyControlledAccess(view.generation, allowed);
    Board::GetInstance().GetDisplay()->ShowDeviceBinding(view);
    if (view.sign_out_pending || view.permission_version == 0 ||
        view.permission_version == acknowledged_permission_version_) {
        return;
    }
    if (allowed || audio_service_.StopControlledAudio()) {
        control.AcknowledgeState(view.revision, view.permission_version, !allowed);
        acknowledged_permission_version_ = view.permission_version;
    }
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::ResetProtocol() {
    Schedule([this]() {
        audio_service_.SetVoiceConnection(0);
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}

