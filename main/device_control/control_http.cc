#include "control_http.h"
#include <algorithm>
#include <cctype>

namespace device_control {
HttpResponseParser::HttpResponseParser() {
    http_parser_init(&parser_, HTTP_RESPONSE);
    parser_.data = this;
    settings_.on_header_field = Field;
    settings_.on_header_value = Value;
    settings_.on_headers_complete = Headers;
    settings_.on_body = BodyData;
    settings_.on_message_complete = Done;
}
bool HttpResponseParser::CommitHeader() {
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    };
    auto value = lower(header_value_);
    const auto field = lower(header_field_);
    while (!value.empty() && value.front() == ' ')
        value.erase(value.begin());
    if (field == "content-encoding" && value != "identity")
        return false;
    if (field == "retry-after" || field == "date") {
        auto& seen = field == "retry-after" ? retry_after_seen_ : date_seen_;
        auto& destination = field == "retry-after" ? retry_after_ : date_;
        if (seen)
            return false;
        seen = true;
        destination = header_value_;
        while (!destination.empty() && (destination.front() == ' ' || destination.front() == '\t'))
            destination.erase(destination.begin());
        while (!destination.empty() && (destination.back() == ' ' || destination.back() == '\t'))
            destination.pop_back();
    }
    header_field_.clear();
    header_value_.clear();
    in_value_ = false;
    return true;
}
int HttpResponseParser::Field(http_parser* p, const char* s, size_t n) {
    auto& self = *static_cast<HttpResponseParser*>(p->data);
    if (self.in_value_ && !self.CommitHeader())
        return -1;
    self.header_parts_ += n;
    if (self.header_parts_ > MaxHeaders)
        return -1;
    self.header_field_.append(s, n);
    return 0;
}
int HttpResponseParser::Value(http_parser* p, const char* s, size_t n) {
    auto& self = *static_cast<HttpResponseParser*>(p->data);
    self.header_parts_ += n;
    if (self.header_parts_ > MaxHeaders)
        return -1;
    self.in_value_ = true;
    self.header_value_.append(s, n);
    return 0;
}
int HttpResponseParser::Headers(http_parser* p) {
    auto& self = *static_cast<HttpResponseParser*>(p->data);
    if (!self.CommitHeader() || p->status_code < 200 || p->status_code >= 600 ||
        (p->content_length != UINT64_MAX && p->content_length > MaxBody))
        return -1;
    return 0;
}
int HttpResponseParser::BodyData(http_parser* p, const char* s, size_t n) {
    auto& self = *static_cast<HttpResponseParser*>(p->data);
    if (n > MaxBody - self.body_.size())
        return -1;
    self.body_.append(s, n);
    return 0;
}
int HttpResponseParser::Done(http_parser* p) {
    auto& self = *static_cast<HttpResponseParser*>(p->data);
    if (!self.CommitHeader())
        return -1;
    self.complete_ = true;
    http_parser_pause(p, 1);
    return 0;
}
bool HttpResponseParser::Parse(const char* bytes, size_t size) {
    const auto n = http_parser_execute(&parser_, &settings_, bytes, size);
    const auto e = HTTP_PARSER_ERRNO(&parser_);
    if (n != size || (e != HPE_OK && !(complete_ && e == HPE_PAUSED))) {
        failed_ = true;
        return false;
    }
    return true;
}
bool HttpResponseParser::Feed(const char* bytes, size_t size) {
    if (failed_ || complete_ || size > MaxWire - wire_) {
        failed_ = true;
        return false;
    }
    wire_ += size;
    if (!prefix_done_) {
        // Count raw status/header bytes, including whitespace, before giving the parser data.
        size_t used = 0;
        for (; used < size; ++used) {
            prefix_.push_back(bytes[used]);
            if (prefix_.size() > MaxHeaders) {
                failed_ = true;
                return false;
            }
            if (prefix_.size() >= 4 && prefix_.compare(prefix_.size() - 4, 4, "\r\n\r\n") == 0) {
                ++used;
                prefix_done_ = true;
                break;
            }
        }
        if (!prefix_done_)
            return true;
        if (!Parse(prefix_.data(), prefix_.size()))
            return false;
        prefix_.clear();
        return used == size || Parse(bytes + used, size - used);
    }
    return Parse(bytes, size);
}
bool HttpResponseParser::Finish() {
    if (failed_ || !prefix_done_)
        return false;
    if (!complete_ && !Parse(nullptr, 0))
        return false;
    return complete_ && !failed_;
}
}  // namespace device_control
