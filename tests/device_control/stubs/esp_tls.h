#pragma once
#include <sys/select.h>
#include "esp_err.h"

enum esp_tls_conn_state_t {
    ESP_TLS_INIT,
    ESP_TLS_CONNECTING,
    ESP_TLS_HANDSHAKE,
    ESP_TLS_FAIL,
    ESP_TLS_DONE
};
struct esp_tls_cfg_t {
    bool is_plain_tcp = false;
    bool non_block = true;
    int timeout_ms = 1;
    const char* common_name = nullptr;
};
struct esp_tls_t {
    esp_tls_conn_state_t conn_state = ESP_TLS_INIT;
    int sockfd = -1;
    bool is_tls = false;
    fd_set rset{}, wset{};
    void* error_handle = nullptr;
    int read = 0, write = 0;
};
int esp_tls_conn_new_async(const char*, int, int, const esp_tls_cfg_t*, esp_tls_t*);
esp_err_t esp_tls_get_conn_sockfd(esp_tls_t*, int*);
esp_err_t esp_tls_get_conn_state(esp_tls_t*, esp_tls_conn_state_t*);
