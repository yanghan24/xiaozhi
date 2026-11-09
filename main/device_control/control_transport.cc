#include "control_transport.h"
#include <esp_crt_bundle.h>
#include <esp_timer.h>
#include <esp_tls.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include "control_http.h"
#include "control_model.h"

namespace device_control {
namespace {
int64_t Now() { return esp_timer_get_time() / 1000; }
void Pause() { ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(5))); }

// DNS callbacks run on tcpip_thread and can outlive a cancelled request. One
// reference belongs to the caller and one to the queued/resolver callback.
struct DnsQuery {
    std::atomic<unsigned> references{2};
    std::mutex mutex;
    std::string host;
    ip_addr_t address{};
    bool done = false;
    bool success = false;
    void Release() {
        if (references.fetch_sub(1) == 1)
            delete this;
    }
};
void DnsDone(const char*, const ip_addr_t* address, void* arg) {
    auto q = static_cast<DnsQuery*>(arg);
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        if (address)
            q->address = *address;
        q->success = address != nullptr;
        q->done = true;
    }
    q->Release();
}
void StartDns(void* arg) {
    auto q = static_cast<DnsQuery*>(arg);
    ip_addr_t address;
    const err_t result =
        dns_gethostbyname_addrtype(q->host.c_str(), &address, DnsDone, q, LWIP_DNS_ADDRTYPE_IPV4);
    if (result != ERR_INPROGRESS)
        DnsDone(nullptr, result == ERR_OK ? &address : nullptr, q);
}
}  // namespace

HttpResult ControlRequest(const std::string& text, const std::string& method,
                          const std::string& device_id, const std::string& credential,
                          const std::string& body, const std::string& key,
                          const std::function<bool()>& cancelled) {
    const int64_t start = Now(), deadline = start + 25000;
    HttpResult result;
    auto stop = [&]() {
        if (cancelled()) {
            result.failure = HttpFailure::Cancelled;
            return true;
        }
        if (Now() >= deadline) {
            result.failure = HttpFailure::Timeout;
            return true;
        }
        return false;
    };
    auto finish = [&]() {
        result.elapsed_ms = Now() - start;
        return result;
    };
    Url url;
    if (!ParseUrl(text, url) || url.scheme != "https" || !HeaderValue(device_id) ||
        !HeaderValue(credential) || (!key.empty() && !HeaderValue(key)) ||
        (method != "GET" && method != "POST")) {
        result.failure = HttpFailure::Invalid;
        return finish();
    }
    std::string numeric;
    ip_addr_t direct;
    if (ipaddr_aton(url.host.c_str(), &direct)) {
        numeric = url.host;
    } else {
        auto q = new DnsQuery;
        q->host = url.host;
        if (tcpip_try_callback(StartDns, q) != ERR_OK) {
            q->Release();
            q->Release();
            return finish();
        }
        while (!stop()) {
            bool done, success;
            ip_addr_t address;
            {
                std::lock_guard<std::mutex> lock(q->mutex);
                done = q->done;
                success = q->success;
                address = q->address;
            }
            if (done) {
                if (success) {
                    char buffer[IPADDR_STRLEN_MAX];
                    if (ipaddr_ntoa_r(&address, buffer, sizeof(buffer)))
                        numeric = buffer;
                }
                break;
            }
            Pause();
        }
        q->Release();
        if (numeric.empty() || stop())
            return finish();
    }
    // Passing the already-resolved numeric address avoids the synchronous
    // getaddrinfo hidden inside esp_tls_conn_new_async. common_name preserves
    // the authorized domain for BOTH SNI and certificate hostname validation.
    std::unique_ptr<esp_tls_t, decltype(&esp_tls_conn_destroy)> tls(esp_tls_init(),
                                                                    esp_tls_conn_destroy);
    if (!tls)
        return finish();
    esp_tls_cfg_t config{};
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.common_name = url.host.c_str();
    config.non_block = true;
    config.timeout_ms = 1;  // bounded select slice, never an independent request timeout
    bool connected = false;
    while (!stop()) {
        const int r =
            esp_tls_conn_new_async(numeric.c_str(), numeric.size(), url.port, &config, tls.get());
        if (stop())
            return finish();
        if (r == 1) {
            connected = true;
            break;
        }
        if (r < 0) {
            esp_tls_error_handle_t error;
            if (esp_tls_get_error_handle(tls.get(), &error) == ESP_OK) {
                int tls_error = 0, flags = 0;
                esp_tls_get_and_clear_last_error(error, &tls_error, &flags);
                if (flags != 0)
                    result.failure = HttpFailure::Tls;
            }
            return finish();
        }
        Pause();
    }
    if (!connected)
        return finish();
    std::string request = method + " " + url.target + " HTTP/1.1\r\nHost: " + url.authority +
                          "\r\nAuthorization: Bearer " + credential +
                          "\r\nDevice-Id: " + device_id +
                          "\r\nAccept: application/json\r\nConnection: close\r\n";
    if (method == "POST")
        request +=
            "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\n";
    if (!key.empty())
        request += "Idempotency-Key: " + key + "\r\n";
    request += "\r\n" + body;
    size_t sent = 0;
    while (sent < request.size() && !stop()) {
        const int r = esp_tls_conn_write(tls.get(), request.data() + sent,
                                         std::min<size_t>(1024, request.size() - sent));
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            Pause();
            continue;
        }
        if (r <= 0)
            return finish();
        sent += r;
    }
    if (stop())
        return finish();
    HttpResponseParser parser;
    char buffer[1024];
    while (!stop()) {
        const int n = esp_tls_conn_read(tls.get(), buffer, sizeof(buffer));
        if (stop())
            return finish();
        if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) {
            Pause();
            continue;
        }
        if (n < 0)
            return finish();
        if (n == 0) {
            if (!parser.Finish()) {
                result.failure = HttpFailure::Invalid;
                return finish();
            }
        } else if (!parser.Feed(buffer, n)) {
            result.failure = HttpFailure::Invalid;
            return finish();
        }
        if (parser.Complete()) {
            result.status = parser.Status();
            // Redirects are terminal responses. No follow-up request can
            // forward this credential to another URI.
            if (result.status >= 300 && result.status < 400) {
                result.failure = HttpFailure::Invalid;
                return finish();
            }
            result.body = parser.Body();
            result.retry_after = parser.RetryAfter();
            result.date = parser.Date();
            result.failure = HttpFailure::None;
            return finish();
        }
    }
    return finish();
}
}  // namespace device_control
