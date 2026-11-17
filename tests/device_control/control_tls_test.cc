#include "control_tls.h"
#include <esp_tls.h>
#include <sys/socket.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>

namespace {
constexpr int kSocket = 3;
int selections, empty_sets, handles, handshakes;
bool delayed = true;
int socket_error = 0, select_error = 0;
const char* expected_name = "speaker.example";

void Reset() {
    selections = empty_sets = handles = handshakes = 0;
    delayed = true;
    socket_error = select_error = 0;
}
void _esp_tls_net_init(esp_tls_t*) {}
int tcp_connect(const char*, int, int, const esp_tls_cfg_t*, void*, int* fd) {
    *fd = kSocket;
    return ESP_OK;
}
constexpr int tcp_read = 1, tcp_write = 2, _esp_tls_read = 3, _esp_tls_write = 4;
int create_ssl_handle(const char*, int, const esp_tls_cfg_t* config, esp_tls_t*) {
    assert(config->common_name == expected_name);
    ++handles;
    return ESP_OK;
}
int esp_tls_handshake(esp_tls_t* tls, const esp_tls_cfg_t*) {
    if (++handshakes == 1)
        return 0;
    tls->conn_state = ESP_TLS_DONE;
    return 1;
}
void ms_to_timeval(int ms, timeval* tv) {
    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
}
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_INT_EVENT_TRACKER_CAPTURE(...) ((void)0)
// Compiled verbatim from the installed SDK, not a second implementation of its
// connection state machine. Only sockets and the TLS cryptography are replaced.
#include "sdk_tls_connection.inc"
}  // namespace

extern "C" int __wrap_select(int, fd_set* read, fd_set* write, fd_set*, timeval* timeout) {
    assert(timeout && timeout->tv_sec == 0 && timeout->tv_usec <= 1000);
    ++selections;
    if (select_error && selections > 1) {
        errno = select_error;
        return -1;
    }
    const bool watched = (read && FD_ISSET(kSocket, read)) || (write && FD_ISSET(kSocket, write));
    if (!watched)
        ++empty_sets;
    if ((delayed && selections == 1) || !watched) {
        if (read)
            FD_ZERO(read);
        if (write)
            FD_ZERO(write);
        return 0;
    }
    return 1;
}
extern "C" int __wrap_getsockopt(int fd, int, int, void* value, socklen_t*) {
    assert(fd == kSocket);
    std::memcpy(value, &socket_error, sizeof(socket_error));
    return 0;
}
int esp_tls_conn_new_async(const char* host, int length, int port, const esp_tls_cfg_t* cfg,
                           esp_tls_t* tls) {
    return esp_tls_low_level_conn(host, length, port, cfg, tls);
}
esp_err_t esp_tls_get_conn_sockfd(esp_tls_t* tls, int* fd) {
    *fd = tls->sockfd;
    return ESP_OK;
}
esp_err_t esp_tls_get_conn_state(esp_tls_t* tls, esp_tls_conn_state_t* state) {
    *state = tls->conn_state;
    return ESP_OK;
}

int Connect(esp_tls_t& tls) {
    esp_tls_cfg_t config;
    config.common_name = expected_name;
    int result = 0;
    for (int i = 0; i < 10 && result == 0; ++i) {
        result = device_control::PollTlsConnection("203.0.113.1", 11, 18443, config, &tls);
        assert(config.non_block);  // Preserve the caller's TLS settings.
    }
    return result;
}

int main() {
    Reset();
    esp_tls_t delayed_tls;
    const int result = Connect(delayed_tls);
    if (result != 1) {
        std::cerr << "delayed TCP connection stalled: result=" << result
                  << ", empty socket sets=" << empty_sets << ", handshakes=" << handshakes << '\n';
        return 1;
    }
    assert(handles == 1 && handshakes == 2 && empty_sets == 0);

    Reset();
    delayed = false;
    esp_tls_t immediate_tls;
    assert(Connect(immediate_tls) == 1 && handles == 1 && handshakes == 2);

    Reset();
    socket_error = ECONNREFUSED;
    esp_tls_t refused_tls;
    assert(Connect(refused_tls) == -1 && handles == 0 && handshakes == 0);

    Reset();
    select_error = EBADF;
    esp_tls_t invalid_tls;
    assert(Connect(invalid_tls) == -1 && handles == 0);

    Reset();
    select_error = EINTR;
    esp_tls_t interrupted_tls;
    assert(Connect(interrupted_tls) == 0 && handles == 0);
    select_error = 0;
    assert(Connect(interrupted_tls) == 1 && handles == 1);
    std::cout << "TLS connection polls: delayed/immediate/error/interruption passed\n";
}
