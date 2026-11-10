#include "device_identity.h"

#include <esp_mac.h>
#include <nvs.h>
#include <cstdio>
#include <cstring>
#include <set>

namespace device_control {
namespace {
constexpr const char* kNamespace = "device_ctrl";
constexpr const char* kIdentityKey = "identity_v1";
constexpr const char* kDeviceIdKey = "device_id";
esp_err_t ReadDeviceId(nvs_handle_t handle, std::string& id) {
    char text[129] = {};
    size_t size = sizeof(text);
    const auto result = nvs_get_str(handle, kDeviceIdKey, text, &size);
    if (result == ESP_OK) {
        if (size < 2 || size > sizeof(text) || text[size - 1] != '\0' ||
            std::strlen(text) != size - 1)
            return ESP_ERR_INVALID_STATE;
        id.assign(text, size - 1);
    }
    return result;
}
esp_err_t Read(nvs_handle_t handle, std::string& json) {
    size_t size = 0;
    auto result = nvs_get_blob(handle, kIdentityKey, nullptr, &size);
    if (result != ESP_OK)
        return result;
    if (size == 0 || size > 2048)
        return ESP_ERR_INVALID_SIZE;
    json.resize(size);
    return nvs_get_blob(handle, kIdentityKey, json.data(), &size);
}
}  // namespace

std::string DeviceHardwareId() {
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK)
        return {};
    char text[13];
    std::snprintf(text, sizeof(text), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5]);
    return text;
}

bool LoadDeviceIdentity(IdentityConfig& config) {
    config = {};
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK)
        return false;
    std::string json;
    auto result = Read(handle, json);
    std::string id;
    const auto anchor_result = ReadDeviceId(handle, id);
    nvs_close(handle);
    IdentityConfig loaded;
    if (result != ESP_OK || !ParseIdentityConfig(json, DeviceHardwareId(), loaded) ||
        (anchor_result != ESP_ERR_NVS_NOT_FOUND &&
         (anchor_result != ESP_OK || loaded.device_id != id)))
        return false;
    config = std::move(loaded);
    return true;
}

esp_err_t StoreDeviceIdentity(const std::string& json, bool repair_corrupt) {
    IdentityConfig next;
    if (!ParseIdentityConfig(json, DeviceHardwareId(), next))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    auto result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return result;
    std::string id;
    const auto anchor_result = ReadDeviceId(handle, id);
    if ((anchor_result != ESP_OK && anchor_result != ESP_ERR_NVS_NOT_FOUND) ||
        (anchor_result == ESP_OK && id != next.device_id)) {
        nvs_close(handle);
        return ESP_ERR_INVALID_STATE;
    }
    std::string previous;
    const auto read_result = Read(handle, previous);
    bool damaged = read_result == ESP_ERR_INVALID_SIZE || read_result == ESP_ERR_NVS_TYPE_MISMATCH;
    if (read_result == ESP_OK) {
        IdentityConfig current;
        damaged = !ParseIdentityConfig(previous, next.hardware_id, current);
        if (!damaged && current.device_id != next.device_id)
            result = ESP_ERR_INVALID_STATE;
    } else if (read_result != ESP_ERR_NVS_NOT_FOUND && !damaged) {
        result = read_result;
    }
    if ((damaged && (!repair_corrupt || anchor_result != ESP_OK)) || (repair_corrupt && !damaged))
        result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK && anchor_result == ESP_ERR_NVS_NOT_FOUND) {
        // Persist the ID first. A power cut can leave an ID without a credential,
        // but can never leave a credential whose identity has no recovery anchor.
        result = nvs_set_str(handle, kDeviceIdKey, next.device_id.c_str());
        if (result == ESP_OK)
            result = nvs_commit(handle);
        if (result == ESP_OK)
            result = ReadDeviceId(handle, id);
        if (result == ESP_OK && id != next.device_id)
            result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK)
        result = nvs_set_blob(handle, kIdentityKey, json.data(), json.size());
    if (result == ESP_OK)
        result = nvs_commit(handle);
    std::string stored;
    if (result == ESP_OK)
        result = Read(handle, stored);
    if (result == ESP_OK && stored != json)
        result = ESP_ERR_INVALID_STATE;
    nvs_close(handle);
    return result;
}

esp_err_t ResetUserSettings() {
    std::set<std::string> namespaces;
    nvs_iterator_t it = nullptr;
    auto result = nvs_entry_find("nvs", nullptr, NVS_TYPE_ANY, &it);
    while (result == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (std::string(info.namespace_name) != kNamespace)
            namespaces.emplace(info.namespace_name);
        result = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (result != ESP_ERR_NVS_NOT_FOUND)
        return result;
    for (const auto& ns : namespaces) {
        nvs_handle_t handle;
        result = nvs_open(ns.c_str(), NVS_READWRITE, &handle);
        if (result != ESP_OK)
            return result;
        result = nvs_erase_all(handle);
        if (result == ESP_OK)
            result = nvs_commit(handle);
        nvs_close(handle);
        if (result != ESP_OK)
            return result;
    }
    return ESP_OK;
}
}  // namespace device_control
