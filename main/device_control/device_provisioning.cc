#include "sdkconfig.h"

#if CONFIG_XIAOZHI_DEVICE_PROVISIONING
#include "device_identity.h"

#include <driver/uart.h>
#include <driver/usb_serial_jtag.h>
#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <memory>
#include <set>

namespace device_control {
namespace {
void Reply(const std::string& json) {
    const auto line = json + "\n";
#if CONFIG_ESP_CONSOLE_UART
    const auto port = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_write_bytes(port, line.data(), line.size());
    uart_wait_tx_done(port, pdMS_TO_TICKS(1000));
#else
    usb_serial_jtag_write_bytes(line.data(), line.size(), pdMS_TO_TICKS(1000));
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
#endif
}
void Ready() {
    Reply("{\"version\":1,\"mode\":\"device_provisioning\",\"hardware_id\":\"" +
          DeviceHardwareId() + "\"}");
}
void Apply(const std::string& line) {
    const char* result = "invalid_request";
    if (line.find('\0') != std::string::npos || line.find("\\u0000") != std::string::npos) {
        Reply("{\"version\":1,\"result\":\"invalid_request\"}");
        return;
    }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
        cJSON_ParseWithLengthOpts(line.c_str(), line.size() + 1, nullptr, true), cJSON_Delete);
    std::set<std::string> fields = {"version", "operation_id", "action", "identity"};
    bool valid = cJSON_IsObject(root.get());
    for (const auto* field = valid ? root->child : nullptr; field; field = field->next)
        valid = field->string && fields.erase(field->string) == 1 && valid;
    const auto* version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    const auto* operation = cJSON_GetObjectItemCaseSensitive(root.get(), "operation_id");
    const auto* action = cJSON_GetObjectItemCaseSensitive(root.get(), "action");
    const auto* identity = cJSON_GetObjectItemCaseSensitive(root.get(), "identity");
    std::string operation_id = cJSON_IsString(operation) ? operation->valuestring : "";
    if (operation_id.size() != 32 ||
        operation_id.find_first_not_of("0123456789abcdef") != std::string::npos) {
        Reply("{\"version\":1,\"result\":\"invalid_request\"}");
        return;
    }
    const std::string action_name = cJSON_IsString(action) ? action->valuestring : "";
    if (valid && fields.empty() && cJSON_IsNumber(version) && version->valuedouble == 1 &&
        (action_name == "write" || action_name == "repair") && cJSON_IsObject(identity)) {
        std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(identity),
                                                          cJSON_free);
        if (json) {
            const auto stored = StoreDeviceIdentity(json.get(), action_name == "repair");
            result = stored == ESP_OK                  ? "stored"
                     : stored == ESP_ERR_INVALID_ARG   ? "invalid_identity"
                     : stored == ESP_ERR_INVALID_STATE ? "identity_conflict"
                                                       : "storage_unavailable";
        }
    }
    Reply(std::string("{\"version\":1,\"operation_id\":\"") + operation_id + "\",\"result\":\"" +
          result + "\"}");
}
}  // namespace

void RunDeviceProvisioning() {
    // This is a separate bench image: app_main never starts WiFi, UI or audio.
    // Receive directly from the maintenance transport, without a console interpreter or echo.
    esp_log_level_set("*", ESP_LOG_NONE);
#if CONFIG_ESP_CONSOLE_UART
    const auto port = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_config_t config{};
    config.baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(port, &config));
#if CONFIG_ESP_CONSOLE_UART_CUSTOM
    ESP_ERROR_CHECK(uart_set_pin(port, CONFIG_ESP_CONSOLE_UART_TX_GPIO,
                                 CONFIG_ESP_CONSOLE_UART_RX_GPIO, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
#endif
    if (!uart_is_driver_installed(port))
        ESP_ERROR_CHECK(uart_driver_install(port, 4096, 1024, 0, nullptr, 0));
#else
    usb_serial_jtag_driver_config_t config{};
    config.rx_buffer_size = 4096;
    config.tx_buffer_size = 1024;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
#endif
    Ready();
    std::string line;
    bool oversized = false;
    int idle = 0;
    while (true) {
        char byte;
#if CONFIG_ESP_CONSOLE_UART
        const auto count = uart_read_bytes(port, &byte, 1, pdMS_TO_TICKS(500));
#else
        const auto count = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(500));
#endif
        if (count != 1) {
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
        } else if (line.size() < 3072 && !oversized) {
            line.push_back(byte);
        } else {
            oversized = true;
            line.clear();
        }
    }
}
}  // namespace device_control
#endif
