#include <cstdlib>
#include <iostream>
#include <string>
#include "control_http.h"
#include "control_model.h"

static void Check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

static const char* AllowedState =
    R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","revision":28,"permission_version":4,"server_time_ms":1000000,"binding_state":"bound","use_permission":"allowed","reason":"ready","binding_request":null,"lease":{"permission_version":4,"expires_at_ms":1060000,"expires_in_ms":60000},"voice":{"url":"wss://speaker.example:8443/xiaozhi/v1/","token":"voice-token","expires_at_ms":1300000,"protocol_version":1,"permission_version":4}}})";

static std::string Replace(std::string s, const std::string& from, const std::string& to) {
    const auto at = s.find(from);
    Check(at != std::string::npos, "test fixture replacement missing");
    s.replace(at, from.size(), to);
    return s;
}

int main() {
    device_control::Model model("speaker-a", "https://speaker.example:8443");
    Check(model.ApplyState(AllowedState, 11000, 1000) == device_control::ApplyResult::Applied,
          "valid state should apply");
    Check(model.Allowed(69999), "lease should remain valid before its conservative deadline");
    Check(!model.Allowed(70000), "lease must expire after deducting the entire request RTT");
    Check(model.Tick(70000), "lease expiry should publish a revocation");
    const auto expired_generation = model.View().generation;
    Check(model.ApplyState(Replace(AllowedState, "voice-token", "rotated-token"), 71000, 1000) ==
              device_control::ApplyResult::Applied,
          "same-revision heartbeats may rotate the voice token and renew the lease");
    DeviceVoiceConfig voice;
    Check(model.Voice(71000, voice) && voice.token == "rotated-token" &&
              voice.generation > expired_generation,
          "recovered permission must use the latest token and a fresh audio generation");
    Check(model.ApplyState(Replace(AllowedState, "\"revision\":28", "\"revision\":27"), 72000,
                           1000) == device_control::ApplyResult::Stale &&
              model.Allowed(72000),
          "a late older state must not replace valid permission");
    Check(model.ApplyState(Replace(AllowedState, "speaker.example:8443", "other.example:8443"),
                           73000, 1000) == device_control::ApplyResult::Invalid &&
              !model.Allowed(73000),
          "unapproved WSS origins must fail closed before disclosing a voice token");
    Check(
        device_control::TrustedDiscovery("https://SPEAKER.example:443/xiaozhi/ota/",
                                         "https://speaker.example/xiaozhi/device-control/v1/",
                                         "https://speaker.example", {}),
        "canonical host case and the default HTTPS port must identify the same authorized origin");
    Check(!device_control::TrustedDiscovery("http://speaker.example/xiaozhi/ota/",
                                            "https://speaker.example/xiaozhi/device-control/v1/",
                                            "https://speaker.example", {}),
          "an old HTTP OTA cannot authorize sending a device secret");
    Check(
        !device_control::TrustedDiscovery("https://speaker.example/xiaozhi/ota/",
                                          "https://speaker.example.evil/xiaozhi/device-control/v1/",
                                          "https://speaker.example", {}),
        "an origin suffix must not bypass the provisioned origin");
    device_control::HttpResponseParser http;
    std::string first =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: "
        "chunked\r\n\r\n3\r\n{\"x\r\n";
    Check(http.Feed(first.data(), first.size()),
          "a fragmented chunked response may remain incomplete");
    std::string second = "4\r\n\":1}\r\n0\r\n\r\n";
    Check(http.Feed(second.data(), second.size()) && http.Complete() && http.Body() == "{\"x\":1}",
          "chunked HTTP framing must deliver exactly the JSON body");
    device_control::PendingCreate create;
    Check(create.Select("auto", 4, "first-key") == "first-key",
          "a new logical operation takes a fresh idempotency key");
    create.Complete(503);
    Check(create.Select("auto", 4, "second-key") == "first-key",
          "a lost/failed response must retain the original key");
    create.Complete(410);
    Check(create.Select("auto", 4, "third-key") == "third-key",
          "a confirmed expired operation permits a new QR operation");
    device_control::HttpResponseParser excessive_header;
    std::string too_large_header = "HTTP/1.1 200 OK\r\nX: " + std::string(8192, ' ') + "\r\n\r\n";
    Check(!excessive_header.Feed(too_large_header.data(), too_large_header.size()),
          "raw header whitespace must count against the header limit");
    device_control::HttpResponseParser excessive_body;
    std::string large_body = "HTTP/1.1 200 OK\r\nContent-Length: 32769\r\n\r\n";
    Check(!excessive_body.Feed(large_body.data(), large_body.size()),
          "oversized declared JSON bodies must be rejected before allocation");
    device_control::HttpResponseParser truncated;
    std::string incomplete = "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\n{}";
    Check(truncated.Feed(incomplete.data(), incomplete.size()) && !truncated.Finish(),
          "EOF cannot turn a truncated response into a successful state");
    device_control::HttpResponseParser bad_chunk;
    std::string invalid_chunk = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nz\r\n";
    Check(!bad_chunk.Feed(invalid_chunk.data(), invalid_chunk.size()),
          "invalid chunk lengths must fail closed");
    const std::string unbound =
        R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","revision":1,"permission_version":1,"server_time_ms":1000000,"binding_state":"unbound","use_permission":"blocked","reason":"unbound","binding_request":{"request_id":"request-a","purpose":"bind","verification_uri":"https://speaker.example:8443/xiaozhi/bind/handle-a","created_at_ms":1000000,"expires_at_ms":1600000,"expires_in_ms":600000},"lease":null,"voice":null}})";
    device_control::Model qr("speaker-a", "https://speaker.example:8443");
    Check(qr.ApplyState(unbound, 1000, 1000) == device_control::ApplyResult::Applied &&
              qr.View().qr_deadline_ms == 600000,
          "QR countdown must deduct the full request time");
    qr.Tick(600000);
    Check(qr.View().verification_uri.empty(),
          "expired QR must hide independently of network progress");
    Check(qr.ApplyState(unbound, 600001, 1) == device_control::ApplyResult::Applied &&
              qr.View().verification_uri.empty(),
          "a replayed known request must never resurrect or extend an expired screen QR");
    device_control::Model discarded("speaker-a", "https://speaker.example:8443");
    discarded.ApplyState(unbound, 1000, 1);
    Check(
        discarded.DiscardRequest() && discarded.View().verification_uri.empty() &&
            !discarded.HasServerRequest(),
        "a rejected or replaced request must immediately hide and allow a fresh logical operation");
    Check(discarded.ApplyState(unbound, 2000, 1) == device_control::ApplyResult::Applied &&
              discarded.View().verification_uri.empty(),
          "a full state cannot redisplay an explicitly discarded request");
    auto fresh_request =
        Replace(Replace(Replace(unbound, "request-a", "request-b"), "handle-a", "handle-b"),
                "\"revision\":1", "\"revision\":2");
    Check(discarded.ApplyState(fresh_request, 3000, 1) == device_control::ApplyResult::Applied &&
              !discarded.View().verification_uri.empty(),
          "a new request identity must display normally after discarding an older request");
    auto discarded_again = Replace(unbound, "\"revision\":1", "\"revision\":3");
    Check(discarded.ApplyState(discarded_again, 4000, 1) == device_control::ApplyResult::Applied &&
              discarded.View().verification_uri.empty(),
          "a discarded identity stays hidden even after a different request was displayed");
    const std::string discarded_partial =
        R"({"version":1,"code":"OK","data":{"revision":3,"permission_version":1,"server_time_ms":1000000,"binding_request":{"request_id":"request-a","purpose":"bind","verification_uri":"https://speaker.example:8443/xiaozhi/bind/handle-a","created_at_ms":1000000,"expires_at_ms":1600000,"expires_in_ms":600000}}})";
    Check(discarded.ApplyRequest(discarded_partial, 5000, 1) ==
                  device_control::ApplyResult::Applied &&
              discarded.View().verification_uri.empty(),
          "a partial idempotency replay also cannot redisplay a discarded request");
    device_control::Model late_renewal("speaker-a", "https://speaker.example:8443");
    late_renewal.ApplyState(AllowedState, 11000, 1000);
    const auto old_generation = late_renewal.View().generation;
    Check(late_renewal.ApplyState(AllowedState, 70001, 1) == device_control::ApplyResult::Applied &&
              late_renewal.View().generation > old_generation,
          "renewing after an expired lease must fence old audio even when the timer has not run");
    const std::string changed_fact =
        R"({"version":1,"code":"OK","data":{"device_id":"speaker-a","revision":29,"permission_version":4,"server_time_ms":1000000,"binding_state":"bound","use_permission":"blocked","reason":"user_signed_out","binding_request":null,"lease":null,"voice":null}})";
    Check(late_renewal.ApplyState(changed_fact, 71000, 1) == device_control::ApplyResult::Invalid,
          "a new revision cannot change voice permission facts while keeping the same permission "
          "version");
    Check(
        device_control::RetryDelayMs(
            "90", R"({"version":1,"code":"RATE_LIMITED","message":"slow","retry_after_ms":120000})",
            1700000000000) == 120000,
        "rate limits must honor the longer HTTP or JSON delay beyond the network backoff cap");
    Check(
        device_control::RetryDelayMs("Tue, 14 Nov 2023 22:15:20 GMT", "", 1700000000000) == 120000,
        "HTTP-date Retry-After must be compared with trusted service time");
    Check(device_control::RetryDelayMs("Tue, 14 Nov 2023 22:15:20 GMT", "", 0) == 60000,
          "a first HTTP-date rate limit without trusted time must schedule a bounded reassessment");
    Check(device_control::RetryDelayMs("Tue, 14 Nov 2023 22:15:20 GMT", "", 0,
                                       "Tue, 14 Nov 2023 22:13:20 GMT") == 120000,
          "a first HTTP-date rate limit can use the authenticated response Date before state sync");
    Check(device_control::RetryDelayMs("Tue, 14 Nov 2023 22:15:20 GMT", "", 1600000000000,
                                       "Tue, 14 Nov 2023 22:14:20 GMT") == 60000,
          "the current response Date takes precedence over a stale local clock estimate");
    device_control::HttpResponseParser dated_rate;
    const std::string rate_response =
        "HTTP/1.1 429 Too Many Requests\r\nDate: Tue, 14 Nov 2023 22:13:20 GMT\r\nRetry-After: "
        "Tue, 14 Nov 2023 22:15:20 GMT\r\nContent-Length: 0\r\n\r\n";
    Check(dated_rate.Feed(rate_response.data(), rate_response.size()) && dated_rate.Complete() &&
              device_control::RetryDelayMs(dated_rate.RetryAfter(), dated_rate.Body(), 0,
                                           dated_rate.Date()) == 120000,
          "the HTTP boundary retains both server date headers for conservative relative retries");
    Check(
        !device_control::ValidateAckResponse(
            R"({"version":1,"code":"OK","data":{"accepted":true,"revision":29,"permission_version":5,"extra":true}})",
            29, 5),
        "ACK responses with unknown fields must be rejected");
    Check(
        !device_control::ValidateAckResponse(
            R"({"version":1,"code":"OK","data":{"accepted":true,"revision":29,"revision":29,"permission_version":5}})",
            29, 5),
        "ACK responses with duplicate fields must be rejected");
    device_control::Model embedded_nul("speaker-a", "https://speaker.example:8443");
    Check(embedded_nul.ApplyState(Replace(AllowedState, "speaker-a", R"(speaker-a\u0000other)"),
                                  1000, 1) == device_control::ApplyResult::Invalid,
          "escaped NUL cannot truncate a protocol identity into the expected device ID");
    Check(
        device_control::RetryDelayMs(
            "2",
            R"({"version":1,"code":"RATE_LIMITED","message":"slow","retry_after_ms":120000,"retry_after_ms":1})",
            1700000000000) == 2000,
        "duplicate JSON retry fields must not be trusted over the HTTP delay");
    std::cout << "device control host tests passed\n";
}
