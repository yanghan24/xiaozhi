#include "delivery_bootstrap.h"
#include "sdkconfig.h"

#if CONFIG_XIAOZHI_ONLINE_DELIVERY
#include <esp_app_desc.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <cstdio>
#include <memory>
#include "control_transport.h"
#include "delivery_model.h"
#include "device_identity.h"

namespace device_control {
namespace {
std::string NetworkHardwareId() {
    uint8_t mac[6]{};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) != ESP_OK)
        return {};
    char result[13];
    std::snprintf(result, sizeof(result), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    return result;
}
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
std::string Encode(cJSON* value) {
    std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_PrintUnformatted(value), cJSON_free);
    return text ? text.get() : "";
}
}  // namespace

void ReportDeliveryHardware() {
    const auto hardware = DeviceHardwareId();
    const auto network = NetworkHardwareId();
    if (hardware.empty() || network.empty() || hardware == network)
        return;
    Json report(cJSON_CreateObject(), cJSON_Delete);
    if (!report)
        return;
    cJSON_AddNumberToObject(report.get(), "version", 1);
    cJSON_AddStringToObject(report.get(), "mode", "device_hardware");
    cJSON_AddStringToObject(report.get(), "hardware_id", hardware.c_str());
    cJSON_AddStringToObject(report.get(), "network_hardware_id", network.c_str());
    cJSON_AddBoolToObject(report.get(), "network_connected", true);
    IdentityConfig identity;
    if (LoadDeviceIdentity(identity))
        cJSON_AddStringToObject(report.get(), "device_id", identity.device_id.c_str());
    else
        cJSON_AddNullToObject(report.get(), "device_id");
    const auto app = esp_app_get_description();
    char digest[65]{};
    for (size_t i = 0; i < 32; ++i)
        std::snprintf(digest + 2 * i, 3, "%02x", app->app_elf_sha256[i]);
    cJSON_AddStringToObject(report.get(), "firmware_elf_sha256", digest);
    // A structured local UART report for the delivery tool; no secret is printed.
    const auto encoded = Encode(report.get());
    if (!encoded.empty()) {
        std::printf("%s\n", encoded.c_str());
        std::fflush(stdout);
    }
}

esp_err_t CheckDeviceDelivery(const IdentityConfig& identity) {
    const auto network = NetworkHardwareId();
    if (network.empty())
        return ESP_ERR_INVALID_STATE;
    Json body(cJSON_CreateObject(), cJSON_Delete);
    if (!body)
        return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(body.get(), "version", 1);
    cJSON_AddStringToObject(body.get(), "hardware_id", identity.hardware_id.c_str());
    cJSON_AddStringToObject(body.get(), "network_hardware_id", network.c_str());
    const auto encoded = Encode(body.get());
    if (encoded.empty())
        return ESP_ERR_NO_MEM;
    const auto response =
        ControlRequest(identity.ota_origin + "/xiaozhi/device-delivery/v1/device-check", "POST",
                       identity.device_id, identity.credential, encoded, "", [] { return false; });
    if (response.failure != HttpFailure::None || response.status != 200)
        return ESP_FAIL;
    const auto status = ParseDeliveryCheck(response.body, identity.device_id);
    if (status == DeliveryStatus::Delivered)
        return ESP_OK;
    return status == DeliveryStatus::Pending ? ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_RESPONSE;
}
}  // namespace device_control
#else
namespace device_control {
void ReportDeliveryHardware() {}
esp_err_t CheckDeviceDelivery(const IdentityConfig&) { return ESP_OK; }
}  // namespace device_control
#endif
