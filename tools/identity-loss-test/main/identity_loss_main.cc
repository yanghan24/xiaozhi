#include "sdkconfig.h"
#include "device_identity.h"

#include <cJSON.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Separate bench image. No Wi-Fi, normal provisioning, UI, or automatic erasure.
namespace {
constexpr auto kPort = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
constexpr const char* kNamespace = "device_ctrl";
constexpr const char* kIdentityKey = "identity_v1";
constexpr const char* kConfirmation = "erase-device-ctrl-identity-v1-only";
using Key = std::pair<std::string, std::string>;
struct Value {
    nvs_type_t type;
    std::vector<uint8_t> bytes;
    bool operator==(const Value& other) const {
        return type == other.type && bytes == other.bytes;
    }
};
using Snapshot = std::map<Key, Value>;
bool write_attempted = false;

void Reply(const std::string& value) {
    const auto line = value + "\n";
    uart_write_bytes(kPort, line.data(), line.size());
    uart_wait_tx_done(kPort, pdMS_TO_TICKS(1000));
}

void Ready() {
    Reply("{\"version\":1,\"mode\":\"controlled_identity_loss\",\"hardware_id\":\"" +
          device_control::DeviceHardwareId() + "\",\"write_attempted\":" +
          (write_attempted ? "true}" : "false}"));
}

template <typename T>
esp_err_t ReadNumber(nvs_handle_t handle, const char* key, Value& value,
                     esp_err_t (*get)(nvs_handle_t, const char*, T*)) {
    T number{};
    const auto result = get(handle, key, &number);
    if (result == ESP_OK) {
        const auto* start = reinterpret_cast<const uint8_t*>(&number);
        value.bytes.assign(start, start + sizeof(number));
    }
    return result;
}

esp_err_t ReadValue(nvs_handle_t handle, const nvs_entry_info_t& info, Value& value) {
    value.type = info.type;
    switch (info.type) {
        case NVS_TYPE_I8: return ReadNumber(handle, info.key, value, nvs_get_i8);
        case NVS_TYPE_U8: return ReadNumber(handle, info.key, value, nvs_get_u8);
        case NVS_TYPE_I16: return ReadNumber(handle, info.key, value, nvs_get_i16);
        case NVS_TYPE_U16: return ReadNumber(handle, info.key, value, nvs_get_u16);
        case NVS_TYPE_I32: return ReadNumber(handle, info.key, value, nvs_get_i32);
        case NVS_TYPE_U32: return ReadNumber(handle, info.key, value, nvs_get_u32);
        case NVS_TYPE_I64: return ReadNumber(handle, info.key, value, nvs_get_i64);
        case NVS_TYPE_U64: return ReadNumber(handle, info.key, value, nvs_get_u64);
        case NVS_TYPE_STR:
        case NVS_TYPE_BLOB: {
            size_t size = 0;
            auto result = info.type == NVS_TYPE_STR
                              ? nvs_get_str(handle, info.key, nullptr, &size)
                              : nvs_get_blob(handle, info.key, nullptr, &size);
            if (result != ESP_OK || size > 0x4000)
                return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
            value.bytes.resize(size);
            if (info.type == NVS_TYPE_STR)
                return nvs_get_str(handle, info.key,
                                   reinterpret_cast<char*>(value.bytes.data()), &size);
            return nvs_get_blob(handle, info.key, value.bytes.data(), &size);
        }
        default: return ESP_ERR_INVALID_STATE;
    }
}

bool Collect(Snapshot& snapshot) {
    snapshot.clear();
    nvs_iterator_t iterator = nullptr;
    auto result = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &iterator);
    bool valid = true;
    while (result == ESP_OK && valid) {
        nvs_entry_info_t info{};
        result = nvs_entry_info(iterator, &info);
        nvs_handle_t handle{};
        if (result == ESP_OK)
            result = nvs_open(info.namespace_name, NVS_READONLY, &handle);
        if (result != ESP_OK) {
            valid = false;
            break;
        }
        Value value{};
        result = ReadValue(handle, info, value);
        nvs_close(handle);
        valid = result == ESP_OK && snapshot.emplace(Key{info.namespace_name, info.key},
                                                      std::move(value)).second;
        if (valid)
            result = nvs_entry_next(&iterator);
    }
    nvs_release_iterator(iterator);
    return valid && result == ESP_ERR_NVS_NOT_FOUND;
}

bool SameIdentity(const device_control::IdentityConfig& a,
                  const device_control::IdentityConfig& b) {
    return a.hardware_id == b.hardware_id && a.device_id == b.device_id &&
           a.credential == b.credential && a.ota_url == b.ota_url &&
           a.ota_origin == b.ota_origin && a.control_origin == b.control_origin;
}

const char* Erase(const device_control::IdentityConfig& expected, size_t& remaining) {
    if (write_attempted)
        return "write_already_attempted";
    device_control::IdentityConfig current;
    if (!device_control::LoadDeviceIdentity(current) || !SameIdentity(current, expected))
        return "identity_mismatch";
    Snapshot before;
    if (!Collect(before))
        return "snapshot_unavailable";
    const auto identity = before.find({kNamespace, kIdentityKey});
    const auto anchor = before.find({kNamespace, "device_id"});
    auto expected_anchor = std::vector<uint8_t>(expected.device_id.begin(), expected.device_id.end());
    expected_anchor.push_back(0);
    if (identity == before.end() || identity->second.type != NVS_TYPE_BLOB ||
        anchor == before.end() || anchor->second.type != NVS_TYPE_STR ||
        anchor->second.bytes != expected_anchor)
        return "anchor_or_identity_mismatch";

    // No automatic retry after reaching the only persistent action.
    write_attempted = true;
    nvs_handle_t handle{};
    auto result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return "storage_unverified";
    result = nvs_erase_key(handle, kIdentityKey);
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK)
        return "storage_unverified";

    Snapshot after;
    before.erase({kNamespace, kIdentityKey});
    if (!Collect(after) || before != after)
        return "postcondition_unverified";
    nvs_handle_t check{};
    if (nvs_open(kNamespace, NVS_READONLY, &check) != ESP_OK)
        return "postcondition_unverified";
    size_t size = 0;
    result = nvs_get_blob(check, kIdentityKey, nullptr, &size);
    nvs_close(check);
    device_control::IdentityConfig unavailable;
    if (result != ESP_ERR_NVS_NOT_FOUND || device_control::LoadDeviceIdentity(unavailable) ||
        !SameIdentity(unavailable, device_control::IdentityConfig{}))
        return "postcondition_unverified";
    remaining = after.size();
    return "identity_erased";
}

void Apply(const std::string& line) {
    if (line.find('\0') != std::string::npos || line.find("\\u0000") != std::string::npos) {
        Reply("{\"version\":1,\"result\":\"invalid_request\"}");
        return;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(line.c_str(), line.size() + 1, nullptr, true), cJSON_Delete);
    std::set<std::string> fields = {"version", "operation_id", "action", "identity", "confirmation"};
    bool valid = cJSON_IsObject(root.get());
    for (const auto* field = valid ? root->child : nullptr; field; field = field->next)
        valid = field->string && fields.erase(field->string) == 1 && valid;
    const auto* version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    const auto* operation = cJSON_GetObjectItemCaseSensitive(root.get(), "operation_id");
    const auto* action = cJSON_GetObjectItemCaseSensitive(root.get(), "action");
    const auto* confirmation = cJSON_GetObjectItemCaseSensitive(root.get(), "confirmation");
    const auto* identity = cJSON_GetObjectItemCaseSensitive(root.get(), "identity");
    const std::string id = cJSON_IsString(operation) ? operation->valuestring : "";
    if (!valid || !fields.empty() || !cJSON_IsNumber(version) || version->valuedouble != 1 ||
        id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos ||
        !cJSON_IsString(action) || std::string(action->valuestring) != "erase_identity_v1" ||
        !cJSON_IsString(confirmation) || std::string(confirmation->valuestring) != kConfirmation ||
        !cJSON_IsObject(identity)) {
        Reply("{\"version\":1,\"result\":\"invalid_request\"}");
        return;
    }
    std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(identity), cJSON_free);
    device_control::IdentityConfig expected;
    if (!json || !device_control::ParseIdentityConfig(json.get(), device_control::DeviceHardwareId(), expected)) {
        Reply("{\"version\":1,\"operation_id\":\"" + id + "\",\"result\":\"invalid_identity\"}");
        return;
    }
    size_t count = 0;
    const std::string result = Erase(expected, count);
    std::string response = "{\"version\":1,\"operation_id\":\"" + id + "\",\"result\":\"" + result + "\"";
    if (result == "identity_erased")
        response += ",\"identity_missing\":true,\"anchor_preserved\":true,\"other_nvs_preserved\":true,\"remaining_records\":" + std::to_string(count);
    Reply(response + "}");
}
}  // namespace

extern "C" void app_main() {
    esp_log_level_set("*", ESP_LOG_NONE);
    // Fail in place on corruption/version mismatch; never erase a partition.
    ESP_ERROR_CHECK(nvs_flash_init());
    uart_config_t config{};
    config.baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(kPort, &config));
    if (!uart_is_driver_installed(kPort))
        ESP_ERROR_CHECK(uart_driver_install(kPort, 4096, 1024, 0, nullptr, 0));
    Ready();
    std::string line;
    bool oversized = false;
    int idle = 0;
    while (true) {
        char byte;
        if (uart_read_bytes(kPort, &byte, 1, pdMS_TO_TICKS(500)) != 1) {
            if (line.empty() && ++idle >= 8) {
                Ready();
                idle = 0;
            }
            continue;
        }
        idle = 0;
        if (byte == '\n') {
            if (oversized)
                Reply("{\"version\":1,\"result\":\"invalid_request\"}");
            else
                Apply(line);
            line.clear();
            oversized = false;
        } else if (!oversized && line.size() < 3072) {
            line.push_back(byte);
        } else {
            line.clear();
            oversized = true;
        }
    }
}
