#pragma once
#include <esp_tls.h>

namespace device_control {
// One bounded connection/handshake step. Returns the ESP-TLS async result.
int PollTlsConnection(const char* host, int host_length, int port, const esp_tls_cfg_t& config,
                      esp_tls_t* tls);
}  // namespace device_control
