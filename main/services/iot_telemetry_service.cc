#include "iot_telemetry_service.h"

#include "application.h"
#include "audio/audio_codec.h"
#include "boards/common/board.h"
#include "settings.h"

#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <nvs.h>

#if CONFIG_IDF_TARGET_ESP32P4 && !CONFIG_XIAOZHI_USE_ETHERNET
#include <esp_wifi_remote.h>
#elif !CONFIG_XIAOZHI_USE_ETHERNET
#include <esp_wifi.h>
#endif

#include <sys/time.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define TAG "IotTelemetry"

namespace {
constexpr int64_t kMinimumValidUnixMs = 1577836800000LL;  // 2020-01-01 UTC
constexpr size_t kMaxPendingMessages = 16;
constexpr size_t kMaxQuestionTextBytes = 2000;
constexpr size_t kMaxAnswerTextBytes = 8000;
constexpr size_t kMaxEventTextBytes = 2000;
constexpr size_t kMaxJsonBodyBytes = 65536;
constexpr size_t kMaxPersistentEvents = 3;
constexpr size_t kMaxPersistentEnvelopeBytes = 900;
constexpr std::array<int, 7> kRetrySeconds = {1, 2, 4, 8, 16, 30, 60};

int64_t UnixTimeMs() {
    timeval now = {};
    gettimeofday(&now, nullptr);
    if (now.tv_sec < 1577836800) {
        return 0;
    }
    return static_cast<int64_t>(now.tv_sec) * 1000LL + now.tv_usec / 1000;
}

int GetWifiRssi() {
#if CONFIG_XIAOZHI_USE_ETHERNET
    return 0;
#elif CONFIG_IDF_TARGET_ESP32P4
    int rssi = 0;
    return esp_wifi_remote_sta_get_rssi(&rssi) == ESP_OK ? rssi : 0;
#else
    wifi_ap_record_t ap_info = {};
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK ? ap_info.rssi : 0;
#endif
}

std::string PrintJson(cJSON* root) {
    if (root == nullptr) {
        return {};
    }
    char* text = cJSON_PrintUnformatted(root);
    std::string result = text != nullptr ? text : "";
    cJSON_free(text);
    return result;
}

std::string LimitUtf8(const std::string& value, size_t max_bytes) {
    if (value.size() <= max_bytes) {
        return value;
    }
    size_t length = max_bytes;
    while (length > 0 && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80) {
        --length;
    }
    return value.substr(0, length);
}

std::string TrimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

int64_t JitteredDelayMs(int seconds) {
    const int64_t base_ms = static_cast<int64_t>(seconds) * 1000LL;
    const int64_t span_ms = base_ms / 5;
    const uint32_t range = static_cast<uint32_t>(span_ms * 2 + 1);
    const int64_t offset = static_cast<int64_t>(esp_random() % range) - span_ms;
    return base_ms + offset;
}
}  // namespace

IotTelemetryService& IotTelemetryService::GetInstance() {
    static IotTelemetryService instance;
    return instance;
}

void IotTelemetryService::Start() {
    if (started_.exchange(true)) {
        return;
    }

    Settings settings("iot_platform");
    enabled_ = settings.GetBool("enabled", CONFIG_IOT_PLATFORM_ENABLE);
    base_url_ = TrimTrailingSlash(settings.GetString("base_url", CONFIG_IOT_PLATFORM_BASE_URL));
    product_id_ = settings.GetString("product_id", CONFIG_IOT_PLATFORM_PRODUCT_ID);
    device_id_ = settings.GetString("device_id", CONFIG_IOT_PLATFORM_DEVICE_ID);
    token_ = settings.GetString("token", CONFIG_IOT_PLATFORM_HTTP_TOKEN);
    const int32_t configured_heartbeat =
        settings.GetInt("heartbeat", CONFIG_IOT_PLATFORM_HEARTBEAT_SECONDS);
    heartbeat_seconds_ = static_cast<int>(configured_heartbeat < 10 ? 10 : configured_heartbeat);

    if (!enabled_) {
        ESP_LOGI(TAG, "HTTP mirroring disabled");
        return;
    }
    if (base_url_.empty() || product_id_.empty() || device_id_.empty() || token_.empty()) {
        ESP_LOGW(
            TAG,
            "HTTP mirroring inactive: configure base URL, product ID, device ID and device token");
        enabled_ = false;
        return;
    }

    LoadPendingEvents();
    BaseType_t result = xTaskCreate(TaskEntry, "iot_telemetry", 6144, this, 2, &task_handle_);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry task");
        enabled_ = false;
        return;
    }
    ESP_LOGI(TAG, "HTTP mirroring enabled for product=%s device=%s", product_id_.c_str(),
             device_id_.c_str());
}

void IotTelemetryService::SetNetworkConnected(bool connected) {
    network_connected_.store(connected);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected) {
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                        [](const PendingMessage& item) { return item.heartbeat; }),
                         queue_.end());
        } else {
            next_heartbeat_ms_ = 0;
        }
    }
    WakeTask();
}

void IotTelemetryService::ReportWakeWord(const std::string& word, const std::string& session_id) {
    if (!enabled_ || word.empty()) {
        return;
    }
    const int64_t timestamp_ms = UnixTimeMs();
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "word", LimitUtf8(word, 128).c_str());
    cJSON_AddNumberToObject(data, "timestamp", static_cast<double>(timestamp_ms));
    if (!session_id.empty()) {
        cJSON_AddStringToObject(data, "session_id", LimitUtf8(session_id, 128).c_str());
    }
    QueueEvent("wake_word", data, timestamp_ms);
}

void IotTelemetryService::ReportQuestion(const std::string& text, const std::string& session_id) {
    if (!enabled_ || text.empty() || session_id.empty()) {
        return;
    }
    const int64_t timestamp_ms = UnixTimeMs();
    const std::string limited_text = LimitUtf8(text, kMaxQuestionTextBytes);
    const bool truncated = limited_text.size() != text.size();
    const std::string turn_id = MakeMessageId("turn", timestamp_ms);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_question_ = limited_text;
        current_turn_id_ = turn_id;
        pending_answer_.clear();
        pending_answer_truncated_ = false;
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "text", limited_text.c_str());
    cJSON_AddStringToObject(data, "session_id", LimitUtf8(session_id, 128).c_str());
    cJSON_AddNumberToObject(data, "timestamp", static_cast<double>(timestamp_ms));
    cJSON_AddStringToObject(data, "turn_id", turn_id.c_str());
    cJSON_AddStringToObject(data, "source", "asr");
    if (truncated) {
        cJSON_AddBoolToObject(data, "text_truncated", true);
    }
    QueueEvent("question", data, timestamp_ms);
}

void IotTelemetryService::ReportTtsState(const std::string& state, const std::string& text,
                                         const std::string& session_id) {
    if (!enabled_ || session_id.empty() ||
        (state != "start" && state != "sentence_start" && state != "stop")) {
        return;
    }
    const int64_t timestamp_ms = UnixTimeMs();
    const std::string sentence = LimitUtf8(text, kMaxEventTextBytes);
    std::string turn_id;
    std::string completed_answer;
    bool answer_truncated = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tts_state_ = state;
        turn_id = current_turn_id_;
        if (state == "start") {
            pending_answer_.clear();
            pending_answer_truncated_ = false;
        } else if (state == "sentence_start" && !text.empty()) {
            const size_t remaining = pending_answer_.size() < kMaxAnswerTextBytes
                                         ? kMaxAnswerTextBytes - pending_answer_.size()
                                         : 0;
            if (remaining > 0) {
                pending_answer_ += LimitUtf8(text, remaining);
            }
            if (sentence.size() != text.size() || pending_answer_.size() >= kMaxAnswerTextBytes) {
                pending_answer_truncated_ = true;
            }
        } else if (state == "stop") {
            completed_answer = pending_answer_;
            answer_truncated = pending_answer_truncated_;
            pending_answer_.clear();
            pending_answer_truncated_ = false;
            if (!completed_answer.empty()) {
                last_answer_ = completed_answer;
            }
        }
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "state", state.c_str());
    cJSON_AddStringToObject(data, "session_id", LimitUtf8(session_id, 128).c_str());
    cJSON_AddNumberToObject(data, "timestamp", static_cast<double>(timestamp_ms));
    if (!turn_id.empty()) {
        cJSON_AddStringToObject(data, "turn_id", turn_id.c_str());
    }
    if (state == "sentence_start" && !sentence.empty()) {
        cJSON_AddStringToObject(data, "text", sentence.c_str());
    }
    QueueEvent("tts_state_changed", data, timestamp_ms);

    if (state == "stop" && !completed_answer.empty()) {
        cJSON* answer = cJSON_CreateObject();
        cJSON_AddStringToObject(answer, "text", completed_answer.c_str());
        cJSON_AddStringToObject(answer, "session_id", LimitUtf8(session_id, 128).c_str());
        cJSON_AddNumberToObject(answer, "timestamp", static_cast<double>(timestamp_ms));
        if (!turn_id.empty()) {
            cJSON_AddStringToObject(answer, "turn_id", turn_id.c_str());
        }
        cJSON_AddStringToObject(answer, "finish_reason", "completed");
        if (answer_truncated) {
            cJSON_AddBoolToObject(answer, "text_truncated", true);
        }
        QueueEvent("answer", answer, timestamp_ms);
    }
}

void IotTelemetryService::ReportVoiceConnectionChanged(bool connected, const std::string& transport,
                                                       const std::string& session_id,
                                                       const std::string& reason) {
    if (!enabled_ || (transport != "websocket" && transport != "mqtt_udp")) {
        return;
    }
    const int64_t timestamp_ms = UnixTimeMs();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        voice_connected_ = connected;
        voice_transport_ = transport;
        if (!session_id.empty()) {
            voice_session_id_ = LimitUtf8(session_id, 128);
        }
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "connected", connected);
    cJSON_AddStringToObject(data, "transport", transport.c_str());
    cJSON_AddNumberToObject(data, "timestamp", static_cast<double>(timestamp_ms));
    if (!session_id.empty()) {
        cJSON_AddStringToObject(data, "session_id", LimitUtf8(session_id, 128).c_str());
    }
    if (!reason.empty()) {
        cJSON_AddStringToObject(data, "reason", LimitUtf8(reason, 256).c_str());
    }
    QueueEvent("voice_connection_changed", data, timestamp_ms);
}

void IotTelemetryService::QueueEvent(const char* event_id, cJSON* data, int64_t timestamp_ms) {
    if (data == nullptr) {
        return;
    }
    if (!enabled_ || timestamp_ms < kMinimumValidUnixMs) {
        ESP_LOGW(TAG, "Skip %s event until telemetry is enabled and system time is synchronized",
                 event_id);
        cJSON_Delete(data);
        return;
    }

    PendingMessage message;
    message.path = "event/" + std::string(event_id);
    message.message_id = MakeMessageId(event_id, timestamp_ms);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "messageId", message.message_id.c_str());
    cJSON_AddNumberToObject(root, "timestamp", static_cast<double>(timestamp_ms));
    cJSON_AddItemToObject(root, "data", data);
    message.body = PrintJson(root);
    cJSON_Delete(root);
    if (message.body.empty() || message.body.size() > kMaxJsonBodyBytes) {
        ESP_LOGW(TAG, "Drop oversized or empty %s event body", event_id);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= kMaxPendingMessages) {
            auto heartbeat =
                std::find_if(queue_.begin(), queue_.end(),
                             [](const PendingMessage& item) { return item.heartbeat; });
            if (heartbeat != queue_.end()) {
                queue_.erase(heartbeat);
            } else {
                ESP_LOGW(TAG, "Telemetry queue full; dropping newest %s event", event_id);
                return;
            }
        }
        auto heartbeat = std::find_if(queue_.begin(), queue_.end(),
                                      [](const PendingMessage& item) { return item.heartbeat; });
        if (heartbeat != queue_.end()) {
            queue_.insert(heartbeat, std::move(message));
        } else {
            queue_.push_back(std::move(message));
        }
        event_queue_dirty_.store(true);
    }
    WakeTask();
}

void IotTelemetryService::QueueHeartbeatIfDue(int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (now_ms < next_heartbeat_ms_) {
        return;
    }
    const bool already_queued = std::any_of(
        queue_.begin(), queue_.end(), [](const PendingMessage& item) { return item.heartbeat; });
    if (!already_queued && queue_.size() < kMaxPendingMessages) {
        queue_.push_back(BuildHeartbeat(now_ms));
    }
    next_heartbeat_ms_ = now_ms + JitteredDelayMs(heartbeat_seconds_);
}

IotTelemetryService::PendingMessage IotTelemetryService::BuildHeartbeat(int64_t timestamp_ms) {
    PendingMessage message;
    message.path = "properties/report";
    message.message_id = MakeMessageId("status", timestamp_ms);
    message.heartbeat = true;

    auto& board = Board::GetInstance();
    auto* codec = board.GetAudioCodec();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "messageId", message.message_id.c_str());
    cJSON_AddNumberToObject(root, "timestamp", static_cast<double>(timestamp_ms));
    cJSON* properties = cJSON_AddObjectToObject(root, "properties");
    if (codec != nullptr) {
        cJSON_AddNumberToObject(properties, "volume", codec->output_volume());
    }

    const int rssi = GetWifiRssi();
    if (rssi >= -127 && rssi < 0) {
        cJSON_AddNumberToObject(properties, "wifi_rssi", rssi);
    }
    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        cJSON_AddNumberToObject(properties, "battery_level", battery_level);
        cJSON_AddBoolToObject(properties, "charging", charging);
    }
    cJSON_AddNumberToObject(properties, "uptime_seconds",
                            static_cast<double>(esp_timer_get_time() / 1000000LL));
    cJSON_AddStringToObject(properties, "firmware_version", esp_app_get_description()->version);
    cJSON_AddBoolToObject(properties, "voice_connected", voice_connected_);
    if (!voice_transport_.empty()) {
        cJSON_AddStringToObject(properties, "voice_transport", voice_transport_.c_str());
    }
    if (!voice_session_id_.empty()) {
        cJSON_AddStringToObject(properties, "voice_session_id", voice_session_id_.c_str());
    }
    if (!tts_state_.empty()) {
        cJSON_AddStringToObject(properties, "tts_state", tts_state_.c_str());
    }
    if (!last_question_.empty()) {
        cJSON_AddStringToObject(properties, "last_question", last_question_.c_str());
    }
    if (!last_answer_.empty()) {
        cJSON_AddStringToObject(properties, "last_answer", last_answer_.c_str());
    }

    cJSON* system = cJSON_Parse(board.GetSystemInfoJson().c_str());
    if (system != nullptr) {
        auto copy_string = [properties](cJSON* source, const char* source_name,
                                        const char* target_name, size_t max_bytes) {
            cJSON* item = source != nullptr ? cJSON_GetObjectItem(source, source_name) : nullptr;
            if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
                const std::string value = LimitUtf8(item->valuestring, max_bytes);
                cJSON_AddStringToObject(properties, target_name, value.c_str());
            }
        };
        auto copy_number = [properties](cJSON* source, const char* source_name,
                                        const char* target_name) {
            cJSON* item = source != nullptr ? cJSON_GetObjectItem(source, source_name) : nullptr;
            if (cJSON_IsNumber(item)) {
                cJSON_AddNumberToObject(properties, target_name, item->valuedouble);
            }
        };

        copy_string(system, "mac_address", "mac_address", 17);
        copy_string(system, "uuid", "device_uuid", 128);
        copy_string(system, "chip_model_name", "chip_model", 128);
        copy_number(system, "flash_size", "flash_size");
        cJSON* minimum_heap = cJSON_GetObjectItem(system, "minimum_free_heap_size");
        if (cJSON_IsNumber(minimum_heap)) {
            cJSON_AddNumberToObject(properties, "minimum_free_heap_size",
                                    minimum_heap->valuedouble);
        } else if (cJSON_IsString(minimum_heap)) {
            const unsigned long long value = std::strtoull(minimum_heap->valuestring, nullptr, 10);
            cJSON_AddNumberToObject(properties, "minimum_free_heap_size",
                                    static_cast<double>(value));
        }

        cJSON* board_info = cJSON_GetObjectItem(system, "board");
        if (cJSON_IsObject(board_info)) {
            copy_string(board_info, "type", "board_type", 128);
            copy_string(board_info, "ip", "ip_address", 64);
            copy_string(board_info, "ssid", "wifi_ssid", 32);
            copy_number(board_info, "channel", "wifi_channel");
        }
        cJSON_Delete(system);
    }

    message.body = PrintJson(root);
    cJSON_Delete(root);
    return message;
}

IotTelemetryService::SendResult IotTelemetryService::Send(const PendingMessage& message) {
    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return SendResult::kRetry;
    }

    auto http = network->CreateHttp();
    if (http == nullptr) {
        return SendResult::kRetry;
    }
    http->SetTimeout(10000);
    http->SetKeepAlive(false);
    http->SetHeader("Authorization", "Bearer " + token_);
    http->SetHeader("X-Integration-Version", "1.0");
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Accept", "application/json");
    http->SetContent(std::string(message.body));

    const std::string url = base_url_ + "/" + product_id_ + "/" + device_id_ + "/" + message.path;
    if (!http->Open("POST", url)) {
        ESP_LOGW(TAG, "IoT HTTP result: url=%s, status=0, body=<transport error 0x%x>", url.c_str(),
                 http->GetLastError());
        ESP_LOGW(TAG, "POST failed for %s", message.message_id.c_str());
        return SendResult::kRetry;
    }

    const int status = http->GetStatusCode();
    const std::string response = http->ReadAll();
    http->Close();

    // Keep this at INFO level so the latest gateway response is visible in
    // `idf.py monitor`, including successful requests. Never log auth headers.
    ESP_LOGI(TAG, "IoT HTTP result: url=%s, status=%d, body=%s", url.c_str(), status,
             response.empty() ? "<empty>" : response.c_str());

    cJSON* root = response.empty() ? nullptr : cJSON_Parse(response.c_str());
    cJSON* accepted = root != nullptr ? cJSON_GetObjectItem(root, "accepted") : nullptr;
    cJSON* response_id = root != nullptr ? cJSON_GetObjectItem(root, "message_id") : nullptr;
    cJSON* duplicate = root != nullptr ? cJSON_GetObjectItem(root, "duplicate") : nullptr;
    cJSON* retryable = root != nullptr ? cJSON_GetObjectItem(root, "retryable") : nullptr;
    const bool id_matches =
        cJSON_IsString(response_id) && message.message_id == response_id->valuestring;
    const bool is_accepted =
        status == 200 && cJSON_IsTrue(accepted) && id_matches && cJSON_IsBool(duplicate);
    const bool may_retry = status == 0 || status == 200 || status == 408 || status == 429 ||
                           status >= 500 || (cJSON_IsBool(retryable) && cJSON_IsTrue(retryable));
    cJSON_Delete(root);

    if (is_accepted) {
        ESP_LOGD(TAG, "Accepted %s", message.message_id.c_str());
        return SendResult::kAccepted;
    }
    ESP_LOGW(TAG, "Rejected %s: HTTP %d, response=%s", message.message_id.c_str(), status,
             response.c_str());
    if (status == 401 || status == 403) {
        return SendResult::kAuthFailure;
    }
    return may_retry ? SendResult::kRetry : SendResult::kRejected;
}

std::string IotTelemetryService::MakeMessageId(const char* kind, int64_t timestamp_ms) {
    const uint32_t sequence = message_counter_.fetch_add(1) + 1;
    return "xiaozhi-" + std::string(kind) + "-" + std::to_string(timestamp_ms) + "-" +
           std::to_string(sequence);
}

void IotTelemetryService::LoadPendingEvents() {
    nvs_handle_t handle = 0;
    if (nvs_open("iot_events", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    int32_t count = 0;
    if (nvs_get_i32(handle, "count", &count) != ESP_OK) {
        nvs_close(handle);
        return;
    }
    if (count < 0) {
        count = 0;
    } else if (count > static_cast<int32_t>(kMaxPersistentEvents)) {
        count = static_cast<int32_t>(kMaxPersistentEvents);
    }
    for (int32_t i = 0; i < count; ++i) {
        const std::string key = "p" + std::to_string(i);
        size_t length = 0;
        if (nvs_get_str(handle, key.c_str(), nullptr, &length) != ESP_OK || length < 2 ||
            length > kMaxPersistentEnvelopeBytes + 1) {
            continue;
        }
        std::string encoded(length, '\0');
        if (nvs_get_str(handle, key.c_str(), encoded.data(), &length) != ESP_OK) {
            continue;
        }
        while (!encoded.empty() && encoded.back() == '\0') {
            encoded.pop_back();
        }
        cJSON* root = cJSON_Parse(encoded.c_str());
        cJSON* path = root != nullptr ? cJSON_GetObjectItem(root, "path") : nullptr;
        cJSON* message_id = root != nullptr ? cJSON_GetObjectItem(root, "message_id") : nullptr;
        cJSON* body = root != nullptr ? cJSON_GetObjectItem(root, "body") : nullptr;
        if (cJSON_IsString(path) && cJSON_IsString(message_id) && cJSON_IsString(body)) {
            PendingMessage message;
            message.path = path->valuestring;
            message.message_id = message_id->valuestring;
            message.body = body->valuestring;
            queue_.push_back(std::move(message));
        }
        cJSON_Delete(root);
    }
    nvs_close(handle);
    if (!queue_.empty()) {
        ESP_LOGI(TAG, "Restored %u pending IoT events", static_cast<unsigned>(queue_.size()));
    }
}

void IotTelemetryService::PersistPendingEvents() {
    std::deque<PendingMessage> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& message : queue_) {
            if (!message.heartbeat && events.size() < kMaxPersistentEvents) {
                events.push_back(message);
            }
        }
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("iot_events", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot open persistent event queue: %s", esp_err_to_name(err));
        return;
    }

    int32_t saved = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "path", events[i].path.c_str());
        cJSON_AddStringToObject(root, "message_id", events[i].message_id.c_str());
        cJSON_AddStringToObject(root, "body", events[i].body.c_str());
        const std::string encoded = PrintJson(root);
        cJSON_Delete(root);
        if (encoded.empty() || encoded.size() > kMaxPersistentEnvelopeBytes) {
            ESP_LOGW(TAG, "Event %s is too large for the small NVS retry cache",
                     events[i].message_id.c_str());
            break;
        }
        const std::string key = "p" + std::to_string(i);
        err = nvs_set_str(handle, key.c_str(), encoded.c_str());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Cannot persist event %s: %s", events[i].message_id.c_str(),
                     esp_err_to_name(err));
            break;
        }
        saved++;
    }
    err = nvs_set_i32(handle, "count", saved);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot commit persistent event queue: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

void IotTelemetryService::TaskEntry(void* arg) { static_cast<IotTelemetryService*>(arg)->Run(); }

void IotTelemetryService::Run() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (event_queue_dirty_.exchange(false)) {
            PersistPendingEvents();
        }
        if (!enabled_ || !network_connected_.load()) {
            continue;
        }

        const int64_t now_ms = UnixTimeMs();
        if (now_ms < kMinimumValidUnixMs) {
            continue;
        }
        QueueHeartbeatIfDue(now_ms);

        PendingMessage message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty() || queue_.front().next_attempt_ms > now_ms) {
                continue;
            }
            message = queue_.front();
        }

        const SendResult result = Send(message);
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty() || queue_.front().message_id != message.message_id) {
            continue;
        }
        if (result == SendResult::kAccepted || result == SendResult::kRejected) {
            const bool was_event = !queue_.front().heartbeat;
            queue_.pop_front();
            if (was_event) {
                event_queue_dirty_.store(true);
            }
        } else {
            PendingMessage& pending = queue_.front();
            if (result == SendResult::kAuthFailure) {
                pending.next_attempt_ms = now_ms + 300000LL;
                ESP_LOGE(TAG,
                         "IoT authentication/authorization failed; retry paused for 5 minutes");
            } else {
                const size_t retry_index =
                    std::min<size_t>(pending.retry_step, kRetrySeconds.size() - 1);
                pending.next_attempt_ms = now_ms + JitteredDelayMs(kRetrySeconds[retry_index]);
                pending.retry_step++;
            }
        }
    }
}

void IotTelemetryService::WakeTask() {
    if (task_handle_ != nullptr) {
        xTaskNotifyGive(task_handle_);
    }
}
