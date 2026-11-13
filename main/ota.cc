#include "ota.h"
#include <mbedtls/sha256.h>
#include "assets/lang_config.h"
#include "device_control/control_model.h"
#include "device_control/delivery_bootstrap.h"
#include "device_control/device_control.h"
#include "device_control/device_identity.h"
#include "ota_image_writer.h"
#include "settings.h"
#include "system_info.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_heap_caps.h>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>

#define TAG "Ota"


Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // Read Serial Number from efuse user_data
    uint8_t serial_number[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        if (serial_number[0] == 0) {
            has_serial_number_ = false;
        } else {
            serial_number_ = std::string(reinterpret_cast<char*>(serial_number), 32);
            has_serial_number_ = true;
        }
    }
#endif
}

Ota::~Ota() {
}

std::string Ota::GetCheckVersionUrl() {
    if (DeviceControl::GetInstance().Enabled()) {
        device_control::IdentityConfig identity;
        return device_control::LoadDeviceIdentity(identity) ? identity.ota_url : "";
    }
    Settings settings("wifi", false);
    std::string url = settings.GetString("ota_url");
    if (url.empty()) {
        url = CONFIG_OTA_URL;
    }
    return url;
}

std::unique_ptr<Http> Ota::SetupHttp() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");

    return http;
}

/* 
 * Specification: https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */
esp_err_t Ota::CheckVersion() {
    device_control_url_.clear();
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // Check if there is a new firmware version available
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    std::string url = GetCheckVersionUrl();
    const bool control_enabled = DeviceControl::GetInstance().Enabled();
    if (control_enabled) {
        device_control::IdentityConfig identity;
        if (!device_control::LoadDeviceIdentity(identity) ||
            !device_control::IsTrustedOtaUrl(url, identity.ota_origin)) {
            ESP_LOGE(TAG, "Device control requires a provisioned HTTPS OTA origin");
            return ESP_ERR_INVALID_ARG;
        }
        const auto delivery = device_control::CheckDeviceDelivery(identity);
        if (delivery != ESP_OK)
            return delivery;
    }
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return ESP_ERR_INVALID_ARG;
    }

    auto http = SetupHttp();

    std::string data = board.GetSystemInfoJson();
    if (control_enabled) {
        auto payload = cJSON_Parse(data.c_str());
        if (!cJSON_IsObject(payload)) {
            cJSON_Delete(payload);
            return ESP_ERR_INVALID_RESPONSE;
        }
        cJSON_DeleteItemFromObjectCaseSensitive(payload, "device_control_capabilities");
        auto capabilities = cJSON_AddArrayToObject(payload, "device_control_capabilities");
        cJSON_AddItemToArray(capabilities, cJSON_CreateNumber(1));
        auto encoded = cJSON_PrintUnformatted(payload);
        if (encoded == nullptr) {
            cJSON_Delete(payload);
            return ESP_ERR_NO_MEM;
        }
        data = encoded;
        cJSON_free(encoded);
        cJSON_Delete(payload);
    }
    std::string method = data.length() > 0 ? "POST" : "GET";
    http->SetContent(std::move(data));

    if (!http->Open(method, url)) {
        int last_error = http->GetLastError();
        ESP_LOGE(TAG, "Failed to open HTTP connection, code=0x%x", last_error);
        return last_error;
    }

    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", status_code);
        return status_code;
    }

    data = http->ReadAll();
    http->Close();

    // Response: { "firmware": { "version": "1.0.0", "url": "http://" } }
    // Parse the JSON response and check if the version is newer
    // If it is, set has_new_version_ to true and store the new version and URL
    
    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (!control_enabled && cJSON_IsObject(activation)) {
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
        }
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
        }
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
        }
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
        }
    }

    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (!control_enabled && cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (!control_enabled && cJSON_IsObject(websocket)) {
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_websocket_config_ = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found!");
    }

    auto control = cJSON_GetObjectItemCaseSensitive(root, "device_control");
    if (control_enabled && cJSON_IsObject(control)) {
        auto version = cJSON_GetObjectItemCaseSensitive(control, "version");
        auto base_url = cJSON_GetObjectItemCaseSensitive(control, "base_url");
        if (cJSON_IsNumber(version) && version->valuedouble == 1 && cJSON_IsString(base_url) &&
            base_url->valuestring != nullptr) {
            // DeviceControl validates the provisioned origins before using any credential.
            device_control_url_ = base_url->valuestring;
        }
    }

    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        
        if (cJSON_IsNumber(timestamp)) {
            // 设置系统时间
            struct timeval tv;
            double ts = timestamp->valuedouble;
            
            // 如果有时区偏移，计算本地时间
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (timezone_offset->valueint * 60 * 1000); // 转换分钟为毫秒
            }
            
            tv.tv_sec = (time_t)(ts / 1000);  // 转换毫秒为秒
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;  // 剩余的毫秒转换为微秒
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    has_new_version_ = false;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        cJSON *version = cJSON_GetObjectItem(firmware, "version");
        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        cJSON *url = cJSON_GetObjectItem(firmware, "url");
        if (cJSON_IsString(url)) {
            firmware_url_ = url->valuestring;
        }

        if (cJSON_IsString(version) && cJSON_IsString(url)) {
            // Check if the version is newer, for example, 0.1.0 is newer than 0.0.1
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // If the force flag is set to 1, the given version is forced to be installed
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void Ota::MarkCurrentVersionValid() {
    auto partition = esp_ota_get_running_partition();
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

bool Ota::Upgrade(const std::string& firmware_url,
                  std::function<void(int progress, size_t speed)> callback,
                  const Validation* validation) {
    static_assert(sizeof(esp_image_header_t) == 24 && sizeof(esp_image_segment_header_t) == 8);
    static_assert(sizeof(esp_app_desc_t) == 256 && offsetof(esp_app_desc_t, app_elf_sha256) == 144);
    auto partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition)
        return false;
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (!http->Open("GET", firmware_url) || http->GetStatusCode() != 200)
        return false;

    class Download final : public OtaImageIO {
    public:
        Download(Http& http, const esp_partition_t* partition, const Validation* validation,
                 std::function<void(int, size_t)> callback)
            : http_(http),
              partition_(partition),
              validation_(validation),
              callback_(std::move(callback)) {
            mbedtls_sha256_init(&hash_);
            hash_ok_ = mbedtls_sha256_starts(&hash_, 0) == 0;
        }
        ~Download() override {
            Abort();
            http_.Close();
            mbedtls_sha256_free(&hash_);
        }
        size_t Length() const override { return http_.GetBodyLength(); }
        size_t Capacity() const override { return partition_->size; }
        int Read(uint8_t* bytes, size_t capacity) override {
            return http_.Read(reinterpret_cast<char*>(bytes), capacity);
        }
        bool Hash(const uint8_t* bytes, size_t size) override {
            return hash_ok_ && mbedtls_sha256_update(&hash_, bytes, size) == 0;
        }
        std::string Digest() override {
            uint8_t digest[32];
            if (!hash_ok_ || mbedtls_sha256_finish(&hash_, digest) != 0)
                return "";
            char text[65];
            for (size_t i = 0; i < 32; ++i)
                snprintf(text + i * 2, 3, "%02x", digest[i]);
            return text;
        }
        bool CanStart() const override {
            return !validation_ ||
                   (esp_timer_get_time() / 1000 < validation_->start_deadline_ms && Connected());
        }
        bool Connected() const override {
            return !validation_ || DeviceControl::GetInstance().IsNetworkConnected();
        }
        bool Begin(size_t length) override {
            return esp_ota_begin(partition_, length, &handle_) == ESP_OK;
        }
        bool Write(const uint8_t* bytes, size_t size) override {
            return esp_ota_write(handle_, bytes, size) == ESP_OK;
        }
        bool End() override {
            auto handle = handle_;
            handle_ = 0;  // esp_ota_end frees the handle on success and failure.
            return esp_ota_end(handle) == ESP_OK;
        }
        bool Activate() override { return esp_ota_set_boot_partition(partition_) == ESP_OK; }
        void Abort() override {
            if (handle_) {
                esp_ota_abort(handle_);
                handle_ = 0;
            }
        }
        void Progress(size_t received, size_t length) override {
            auto now = esp_timer_get_time();
            if (now - last_time_ < 1000000 && received != length)
                return;
            if (callback_)
                callback_(received * 100 / length, received - last_received_);
            last_time_ = now;
            last_received_ = received;
        }

    private:
        Http& http_;
        const esp_partition_t* partition_;
        const Validation* validation_;
        std::function<void(int, size_t)> callback_;
        esp_ota_handle_t handle_ = 0;
        mbedtls_sha256_context hash_;
        bool hash_ok_ = false;
        int64_t last_time_ = esp_timer_get_time();
        size_t last_received_ = 0;
    } download(*http, partition, validation, std::move(callback));
    OtaImageTarget expected;
    if (validation)
        expected = {validation->artifact_sha256, validation->target_elf_sha256,
                    validation->image_size};
    bool installed = InstallOtaImage(download, validation ? &expected : nullptr);
    ESP_LOGI(TAG, "Firmware image installation %s", installed ? "verified" : "failed");
    return installed;
}

bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    return Upgrade(firmware_url_, callback);
}


std::vector<int> Ota::ParseVersion(const std::string& version) {
    std::vector<int> versionNumbers;
    std::stringstream ss(version);
    std::string segment;
    
    while (std::getline(ss, segment, '.')) {
        versionNumbers.push_back(std::stoi(segment));
    }
    
    return versionNumbers;
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    std::vector<int> current = ParseVersion(currentVersion);
    std::vector<int> newer = ParseVersion(newVersion);
    
    for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
        if (newer[i] > current[i]) {
            return true;
        } else if (newer[i] < current[i]) {
            return false;
        }
    }
    
    return newer.size() > current.size();
}

std::string Ota::GetActivationPayload() {
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32]; // SHA-256 输出为32字节
    
    // 使用Key0计算HMAC
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());
    return json;
}

esp_err_t Ota::Activate() {
    if (!has_activation_challenge_) {
        ESP_LOGW(TAG, "No activation challenge found");
        return ESP_FAIL;
    }

    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    auto http = SetupHttp();

    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return ESP_FAIL;
    }
    
    auto status_code = http->GetStatusCode();
    if (status_code == 202) {
        return ESP_ERR_TIMEOUT;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", status_code, http->ReadAll().c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation successful");
    return ESP_OK;
}
