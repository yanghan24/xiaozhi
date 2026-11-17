#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace device_control {
enum class HttpFailure { None, Network, Timeout, Cancelled, Tls, Invalid };
struct HttpResult {
    int status = 0;
    std::string body;
    std::string retry_after;
    std::string date;
    HttpFailure failure = HttpFailure::Network;
    int64_t elapsed_ms = 0;
};
HttpResult ControlRequest(const std::string& url, const std::string& method,
                          const std::string& device_id, const std::string& credential,
                          const std::string& body, const std::string& idempotency_key,
                          const std::function<bool()>& cancelled);
}  // namespace device_control
