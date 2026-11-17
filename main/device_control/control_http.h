#pragma once
#include <http_parser.h>
#include <cstddef>
#include <string>

namespace device_control {
// Bounded HTTP response parsing, shared by the embedded transport and host tests.
class HttpResponseParser {
public:
    static constexpr size_t MaxHeaders = 8192;
    static constexpr size_t MaxBody = 32768;
    static constexpr size_t MaxWire = 65536;
    HttpResponseParser();
    bool Feed(const char* bytes, size_t size);
    bool Finish();
    bool Complete() const { return complete_; }
    int Status() const { return parser_.status_code; }
    const std::string& Body() const { return body_; }
    bool Failed() const { return failed_; }
    const std::string& RetryAfter() const { return retry_after_; }
    const std::string& Date() const { return date_; }

private:
    http_parser parser_{};
    http_parser_settings settings_{};
    std::string prefix_;
    std::string body_;
    std::string header_field_;
    std::string header_value_;
    std::string retry_after_;
    std::string date_;
    bool retry_after_seen_ = false;
    bool date_seen_ = false;
    size_t wire_ = 0;
    size_t header_parts_ = 0;
    bool prefix_done_ = false;
    bool complete_ = false;
    bool failed_ = false;
    bool in_value_ = false;
    bool CommitHeader();
    bool Parse(const char* bytes, size_t size);
    static int Field(http_parser*, const char*, size_t);
    static int Value(http_parser*, const char*, size_t);
    static int Headers(http_parser*);
    static int BodyData(http_parser*, const char*, size_t);
    static int Done(http_parser*);
};
}  // namespace device_control
