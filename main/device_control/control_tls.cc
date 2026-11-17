#include "control_tls.h"
#include <sys/select.h>
#include <sys/socket.h>
#include <cerrno>

namespace device_control {
int PollTlsConnection(const char* host, int host_length, int port, const esp_tls_cfg_t& config,
                      esp_tls_t* tls) {
    esp_tls_conn_state_t state;
    if (esp_tls_get_conn_state(tls, &state) != ESP_OK)
        return -1;
    if (config.non_block && state == ESP_TLS_CONNECTING) {
        int fd = -1;
        if (esp_tls_get_conn_sockfd(tls, &fd) != ESP_OK || fd < 0 || fd >= FD_SETSIZE)
            return -1;
        fd_set read, write;
        FD_ZERO(&read);
        FD_SET(fd, &read);
        write = read;
        timeval timeout{};
        const int ready = select(fd + 1, &read, &write, nullptr, &timeout);
        if (ready == 0 || (ready < 0 && errno == EINTR))
            return 0;
        if (ready < 0)
            return -1;
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0)
            return -1;
        // ESP-IDF 5.5 keeps select's modified fd_sets after an async timeout.
        // Poll fresh sets ourselves, then skip only its CONNECTING select. The
        // socket remains nonblocking: tcp_connect only changes flags in INIT.
        // Certificate validation, SNI and the nonblocking handshake are intact.
        auto ready_config = config;
        ready_config.non_block = false;
        return esp_tls_conn_new_async(host, host_length, port, &ready_config, tls);
    }
    return esp_tls_conn_new_async(host, host_length, port, &config, tls);
}
}  // namespace device_control
