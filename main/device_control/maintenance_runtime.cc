#include "maintenance_runtime.h"
#include "sdkconfig.h"
#if CONFIG_XIAOZHI_DEVICE_CONTROL
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <nvs.h>
#include <memory>
#include <mutex>
#include "application.h"
#include "control_transport.h"
#include "device_identity.h"
#include "maintenance_model.h"

namespace device_control {
namespace {
std::shared_ptr<Maintenance> maintenance;
std::mutex maintenance_mutex;
std::shared_ptr<Maintenance> Current() {
    std::lock_guard<std::mutex> lock(maintenance_mutex);
    return maintenance;
}
std::string Hex(const uint8_t* bytes, size_t length) {
    const char* digits = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < length; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}
bool Save(const std::string& record) {
    nvs_handle_t handle;
    if (record.empty() || nvs_open("ota_maint", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    auto error = nvs_set_str(handle, "operation", record.c_str());
    if (error == ESP_OK)
        error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}
std::string Load() {
    nvs_handle_t handle;
    auto error = nvs_open("ota_maint", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND)
        return "";
    if (error != ESP_OK)
        return "invalid";
    char buffer[1024];
    size_t size = sizeof(buffer);
    error = nvs_get_str(handle, "operation", buffer, &size);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND)
        return "";
    if (error != ESP_OK || size < 1 || size > sizeof(buffer))
        return "invalid";
    return std::string(buffer, size - 1);
}
const char* RunningState() {
    auto partition = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!partition || esp_ota_get_state_partition(partition, &state) != ESP_OK)
        return "unknown";
    if (state == ESP_OTA_IMG_VALID)
        return "valid";
    if (state == ESP_OTA_IMG_PENDING_VERIFY)
        return "pending_verify";
    return "unknown";
}
std::string SystemInfo() {
    auto text = Board::GetInstance().GetSystemInfoJson();
    cJSON* original = cJSON_Parse(text.c_str());
    if (!original)
        return "";
    auto report = cJSON_CreateObject();
    for (const char* field :
         {"chip_model_name", "chip_info", "flash_size", "application", "partition_table", "ota"}) {
        auto item = cJSON_DetachItemFromObjectCaseSensitive(original, field);
        if (item)
            cJSON_AddItemToObject(report, field, item);
    }
    auto board = cJSON_AddObjectToObject(report, "board");
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    cJSON_AddStringToObject(board, "type", "waveshare-esp32-p4-wifi6-touch-lcd-4.3");
#else
    auto type = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(original, "board"), "type");
    if (cJSON_IsString(type))
        cJSON_AddStringToObject(board, "type", type->valuestring);
#endif
    auto ota = cJSON_GetObjectItemCaseSensitive(report, "ota");
    if (cJSON_IsObject(ota))
        cJSON_AddStringToObject(ota, "state", RunningState());
    char* encoded = cJSON_PrintUnformatted(report);
    std::string result = encoded ? encoded : "";
    cJSON_free(encoded);
    cJSON_Delete(report);
    cJSON_Delete(original);
    return result;
}
std::string Request() {
    auto client = Current();
    if (!client) {
        IdentityConfig identity;
        if (!LoadDeviceIdentity(identity))
            return "";
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        client = std::make_shared<Maintenance>(
            Hex(random, sizeof(random)), identity.ota_origin, Load(),
            Hex(esp_app_get_description()->app_elf_sha256, 32),
            std::string(RunningState()) == "valid", Save,
            [] { return Application::GetInstance().ReserveMaintenance(); },
            [] {
                Application::GetInstance().Schedule(
                    [] { Application::GetInstance().ReleaseMaintenance(); });
            },
            [](const MaintenanceCommand& command) {
                Application::GetInstance().Schedule([command] {
                    if (!Application::GetInstance().UpgradeMaintenance(command))
                        Current()->Failed();
                });
            });
        std::lock_guard<std::mutex> lock(maintenance_mutex);
        maintenance = client;
    }
    client->SetRunningValid(std::string(RunningState()) == "valid");
    return client->Request(Application::GetInstance().MaintenanceIdle(), SystemInfo(),
                           esp_timer_get_time() / 1000);
}
void Response(const HttpResult& result) {
    auto client = Current();
    if (!client)
        return;
    if (result.failure == HttpFailure::None && result.status == 200)
        client->Apply(result.body, esp_timer_get_time() / 1000, result.elapsed_ms);
    else
        client->Disconnected();
}
}  // namespace
void InitializeMaintenance() {
    DeviceControl::GetInstance().SetMaintenanceCallbacks(Request, Response);
}
}  // namespace device_control
#else
namespace device_control {
void InitializeMaintenance() {}
}  // namespace device_control
#endif
