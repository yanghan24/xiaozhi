#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
#include "device_identity.h"
#include "nvs.h"

using device_control::IdentityWrite;

using Entries = std::map<std::string, std::string>;
static std::map<std::string, Entries> flash;
struct Handle {
    std::string ns;
    Entries staged;
};
static std::map<unsigned, Handle> handles;
static unsigned next_handle = 1;
static bool fail_commit = false;
static int commits_before_failure = -1;
static std::vector<std::string> erased;
struct TestIterator {
    std::vector<std::string> names;
    size_t at = 0;
};

esp_err_t esp_efuse_mac_get_default(uint8_t* mac) {
    const uint8_t fixture[] = {2, 0, 0, 0, 0, 1};
    std::memcpy(mac, fixture, 6);
    return ESP_OK;
}
esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* handle) {
    if (mode == NVS_READONLY && !flash.count(ns))
        return ESP_ERR_NVS_NOT_FOUND;
    *handle = next_handle++;
    handles[*handle] = {ns, flash[ns]};
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { handles.erase(handle); }
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* data, size_t* size) {
    const auto& entries = handles.at(h).staged;
    if (!entries.count(key))
        return ESP_ERR_NVS_NOT_FOUND;
    const auto& value = entries.at(key);
    if (data && *size < value.size())
        return ESP_ERR_INVALID_SIZE;
    if (data)
        std::memcpy(data, value.data(), value.size());
    *size = value.size();
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* data, size_t* size) {
    return nvs_get_blob(h, key, data, size);
}
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* text) {
    return nvs_set_blob(h, key, text, std::strlen(text) + 1);
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* data, size_t size) {
    handles.at(h).staged[key] = std::string(static_cast<const char*>(data), size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    if (fail_commit || commits_before_failure == 0)
        return ESP_FAIL;
    if (commits_before_failure > 0)
        --commits_before_failure;
    const auto& handle = handles.at(h);
    flash[handle.ns] = handle.staged;
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t h) {
    erased.push_back(handles.at(h).ns);
    handles.at(h).staged.clear();
    return ESP_OK;
}
esp_err_t nvs_entry_find(const char*, const char*, int, nvs_iterator_t* it) {
    auto entry = new TestIterator;
    for (const auto& pair : flash)
        if (!pair.second.empty())
            entry->names.push_back(pair.first);
    if (entry->names.empty()) {
        delete entry;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *it = entry;
    return ESP_OK;
}
esp_err_t nvs_entry_next(nvs_iterator_t* it) {
    if (++(*it)->at < (*it)->names.size())
        return ESP_OK;
    delete *it;
    *it = nullptr;
    return ESP_ERR_NVS_NOT_FOUND;
}
void nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* info) {
    std::strncpy(info->namespace_name, it->names.at(it->at).c_str(), 16);
}
void nvs_release_iterator(nvs_iterator_t it) { delete it; }

static void Check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
static std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    Check(at != std::string::npos, "fixture missing");
    return text.replace(at, from.size(), to);
}
int main() {
    const std::string first =
        R"({"version":1,"hardware_id":"020000000001","device_id":"speaker-a","credential":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","ota_url":"https://speaker.example:18443/xiaozhi/ota/","control_origin":""})";
    device_control::IdentityConfig identity;
    Check(device_control::StoreDeviceIdentity(first) == ESP_OK, "initial private provisioning");
    Check(device_control::LoadDeviceIdentity(identity) && identity.device_id == "speaker-a" &&
              identity.ota_origin == "https://speaker.example:18443",
          "read back full atomic record");
    Check(device_control::StoreDeviceIdentity(
              Replace(first, std::string(43, 'A'), std::string(43, 'B'))) == ESP_ERR_INVALID_STATE,
          "ordinary delivery replay cannot rotate an existing credential");
    Check(device_control::StoreDeviceIdentity(Replace(first, "speaker-a", "speaker-b")) ==
              ESP_ERR_INVALID_STATE,
          "repair cannot change an existing device ID");
    Check(device_control::StoreDeviceIdentity(Replace(first, "020000000001", "020000000002")) ==
              ESP_ERR_INVALID_ARG,
          "manifest for another physical chip cannot be written");
    Check(!device_control::ParseIdentityConfig(Replace(first, "https://", "http://"),
                                               "020000000001", identity) &&
              identity.credential.empty(),
          "unsafe OTA cannot leave a usable partial identity");
    Check(!device_control::ParseIdentityConfig(
              Replace(first, "\"version\":1", "\"version\":1,\"version\":1"), "020000000001",
              identity),
          "duplicate fields rejected");
    Check(!device_control::ParseIdentityConfig(Replace(first, "speaker-a", "speaker-a\\u0000b"),
                                               "020000000001", identity),
          "NUL cannot truncate identity");
    auto rotated = Replace(first, std::string(43, 'A'), std::string(43, 'B'));
    fail_commit = true;
    Check(device_control::StoreDeviceIdentity(rotated, IdentityWrite::Recovery) == ESP_FAIL,
          "failed commit is never success");
    fail_commit = false;
    Check(
        device_control::LoadDeviceIdentity(identity) && identity.credential == std::string(43, 'A'),
        "failed replacement leaves the previous complete identity");
    Check(device_control::StoreDeviceIdentity(rotated, IdentityWrite::Recovery) == ESP_OK &&
              device_control::LoadDeviceIdentity(identity) &&
              identity.credential == std::string(43, 'B'),
          "credential rotation preserves device ID and replaces the full record");
    Check(device_control::StoreDeviceIdentity(first) == ESP_ERR_INVALID_STATE &&
              device_control::LoadDeviceIdentity(identity) &&
              identity.credential == std::string(43, 'B'),
          "an old delivery record cannot overwrite the recovered credential");
    flash["wifi"]["ssid"] = "test-network";
    flash["websocket"]["token"] = "obsolete-legacy-token";
    flash["ota_maint"]["operation"] = "accepted-operation-pending-reboot-report";
    Check(device_control::ResetUserSettings() == ESP_OK && flash["wifi"].empty() &&
              flash["websocket"].empty() && device_control::LoadDeviceIdentity(identity),
          "normal reset erases user settings and retains independent device identity");
    Check(flash["ota_maint"]["operation"] == "accepted-operation-pending-reboot-report",
          "normal reset must retain the OTA ledger needed to reconcile the original operation");
    for (const auto& ns : erased)
        Check(ns != "device_ctrl", "identity namespace must never be erased");
    flash["device_ctrl"]["identity_v1"] = "damaged record";
    Check(!device_control::LoadDeviceIdentity(identity) && identity.credential.empty(),
          "corruption closes product access and clears any previously loaded credential");
    Check(device_control::StoreDeviceIdentity(rotated, IdentityWrite::Recovery) == ESP_OK &&
              device_control::LoadDeviceIdentity(identity) && identity.device_id == "speaker-a" &&
              identity.credential == std::string(43, 'B'),
          "recovery restores a damaged blob without changing the surviving device anchor");
    flash["device_ctrl"]["identity_v1"] = "damaged record";
    Check(device_control::StoreDeviceIdentity(first) == ESP_ERR_INVALID_STATE,
          "normal writes never silently overwrite a damaged record");
    Check(device_control::StoreDeviceIdentity(Replace(first, "speaker-a", "speaker-b"),
                                              IdentityWrite::Repair) == ESP_ERR_INVALID_STATE,
          "repair requires the original immutable ID");
    Check(device_control::StoreDeviceIdentity(first, IdentityWrite::Repair) == ESP_OK &&
              device_control::LoadDeviceIdentity(identity),
          "explicit USB maintenance can restore a damaged blob from a matching private backup");
    Check(device_control::StoreDeviceIdentity(rotated, IdentityWrite::Repair) ==
              ESP_ERR_INVALID_STATE,
          "repair cannot override a healthy record; rotation uses normal writes");
    flash["device_ctrl"].erase("device_id");
    flash["device_ctrl"]["identity_v1"] = "damaged record";
    Check(
        device_control::StoreDeviceIdentity(first, IdentityWrite::Repair) == ESP_ERR_INVALID_STATE,
        "no repair without a surviving identity anchor");
    Check(device_control::StoreDeviceIdentity(rotated, IdentityWrite::Recovery) == ESP_OK &&
              device_control::LoadDeviceIdentity(identity) && identity.device_id == "speaker-a" &&
              identity.credential == std::string(43, 'B'),
          "authorized recovery rebuilds a missing anchor for a damaged identity blob");
    flash.clear();
    commits_before_failure = 1;
    Check(device_control::StoreDeviceIdentity(first) == ESP_FAIL &&
              !device_control::LoadDeviceIdentity(identity),
          "interrupted initial provisioning never activates a partial credential");
    commits_before_failure = -1;
    Check(device_control::StoreDeviceIdentity(Replace(first, "speaker-a", "speaker-b")) ==
              ESP_ERR_INVALID_STATE,
          "interrupted first write still anchors the registered ID");
    Check(device_control::StoreDeviceIdentity(first) == ESP_OK &&
              device_control::LoadDeviceIdentity(identity),
          "initial provisioning can be retried for the same registered device");
    flash.clear();
    Check(device_control::StoreDeviceIdentity(rotated, IdentityWrite::Recovery) == ESP_OK &&
              device_control::LoadDeviceIdentity(identity) && identity.device_id == "speaker-a",
          "authorized recovery after complete identity loss retains the server-issued stable ID");
    std::cout << "device identity and reset tests passed\n";
}
