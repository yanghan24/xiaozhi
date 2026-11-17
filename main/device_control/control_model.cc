#include "control_model.h"
#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <set>
#include <utility>

namespace device_control {
namespace {
constexpr double kSafeInt = 9007199254740991.0;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
const cJSON* Item(const cJSON* j, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(j, key);
}
bool Object(const cJSON* j, std::initializer_list<const char*> keys) {
    if (!cJSON_IsObject(j))
        return false;
    std::set<std::string> allowed;
    for (auto k : keys)
        allowed.emplace(k);
    for (const cJSON* p = j->child; p; p = p->next) {
        if (!p->string || allowed.erase(p->string) != 1)
            return false;
    }
    return allowed.empty();
}
bool Int(const cJSON* j, int64_t& out, int64_t minimum = 0) {
    if (!cJSON_IsNumber(j) || !std::isfinite(j->valuedouble) || j->valuedouble < minimum ||
        j->valuedouble > kSafeInt || std::floor(j->valuedouble) != j->valuedouble)
        return false;
    out = static_cast<int64_t>(j->valuedouble);
    return true;
}
bool String(const cJSON* j, std::string& out, size_t maximum = 8192) {
    if (!cJSON_IsString(j) || !j->valuestring || !*j->valuestring)
        return false;
    out = j->valuestring;
    return out.size() <= maximum;
}
Json Parse(const std::string& text) {
    // cJSON represents strings as C strings, so embedded NUL must be rejected
    // before it can truncate an identity, token, field name or URL.
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\0')
            return Json(nullptr, cJSON_Delete);
        if (text[i] == '\\' && i + 1 < text.size()) {
            if (text.compare(i + 1, 5, "u0000") == 0)
                return Json(nullptr, cJSON_Delete);
            ++i;
        }
    }
    return Json(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true),
                cJSON_Delete);
}
bool Envelope(const cJSON* root, const cJSON*& data) {
    int64_t version;
    std::string code;
    if (!Object(root, {"version", "code", "data"}) || !Int(Item(root, "version"), version) ||
        version != 1 || !String(Item(root, "code"), code) || code != "OK")
        return false;
    data = Item(root, "data");
    return cJSON_IsObject(data);
}
void Part(std::string& s, const std::string& part) {
    s += std::to_string(part.size()) + ":" + part;
}
bool Request(const cJSON* req, int64_t server, int64_t now, int64_t elapsed,
             DeviceControlView& view, std::string& stable, std::string& request_id) {
    if (cJSON_IsNull(req))
        return true;
    int64_t created, expires, remaining;
    std::string id, purpose, uri;
    if (!Object(req, {"request_id", "purpose", "verification_uri", "created_at_ms", "expires_at_ms",
                      "expires_in_ms"}) ||
        !String(Item(req, "request_id"), id, 128) || !String(Item(req, "purpose"), purpose, 16) ||
        !String(Item(req, "verification_uri"), uri, 512) ||
        !Int(Item(req, "created_at_ms"), created) || !Int(Item(req, "expires_at_ms"), expires) ||
        !Int(Item(req, "expires_in_ms"), remaining, 1) || expires - created != 600000 ||
        created > server || expires <= server || remaining != expires - server ||
        remaining > 600000 || (purpose != "bind" && purpose != "resume" && purpose != "account"))
        return false;
    Url parsed;
    if (!ParseUrl(uri, parsed) || parsed.scheme != "https" ||
        parsed.target.find("/xiaozhi/bind/") != 0 || parsed.target.find('?') != std::string::npos)
        return false;
    view.verification_uri = uri;
    request_id = id;
    view.purpose = purpose;
    view.qr_deadline_ms = now + std::max<int64_t>(0, remaining - elapsed);
    Part(stable, id);
    Part(stable, purpose);
    Part(stable, uri);
    Part(stable, std::to_string(created));
    Part(stable, std::to_string(expires));
    return true;
}
int64_t HttpDateMs(const std::string& text) {
    if (text.size() != 29 || text.substr(3, 2) != ", " || text.substr(25) != " GMT")
        return -1;
    const std::string weekdays = "SunMonTueWedThuFriSat";
    const auto weekday = weekdays.find(text.substr(0, 3));
    if (weekday == std::string::npos || weekday % 3 != 0)
        return -1;
    auto number = [&](size_t pos, size_t length) {
        int n = 0;
        for (size_t i = pos; i < pos + length; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(text[i])))
                return -1;
            n = n * 10 + text[i] - '0';
        }
        return n;
    };
    const std::string months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const auto month_pos = months.find(text.substr(8, 3));
    const int month = month_pos == std::string::npos || month_pos % 3 != 0
                          ? -1
                          : static_cast<int>(month_pos / 3) + 1;
    int year = number(12, 4);
    const int day = number(5, 2), hour = number(17, 2), minute = number(20, 2),
              second = number(23, 2);
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const int days_in_month[] = {0, 31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > days_in_month[month] ||
        hour < 0 || hour >= 24 || minute < 0 || minute >= 60 || second < 0 || second >= 60 ||
        text[7] != ' ' || text[11] != ' ' || text[16] != ' ' || text[19] != ':' || text[22] != ':')
        return -1;
    year -= month <= 2;
    const int era = year / 400;
    const unsigned yoe = year - era * 400;
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t days =
        static_cast<int64_t>(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return ((days * 24 + hour) * 60 + minute) * 60000 + second * 1000;
}
}  // namespace

std::string Url::Origin() const { return scheme + "://" + authority; }

bool ParseUrl(const std::string& text, Url& out) {
    if (text.empty() || text.size() > 2048)
        return false;
    for (unsigned char c : text)
        if (c <= 0x20 || c >= 0x7f || c == '\\')
            return false;
    const auto split = text.find("://");
    if (split == std::string::npos)
        return false;
    Url u;
    u.scheme = text.substr(0, split);
    std::transform(u.scheme.begin(), u.scheme.end(), u.scheme.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (u.scheme != "https" && u.scheme != "wss")
        return false;
    const auto start = split + 3;
    auto path = text.find('/', start);
    const auto auth =
        text.substr(start, path == std::string::npos ? std::string::npos : path - start);
    if (auth.empty() || auth.find_first_of("@?#%[]") != std::string::npos)
        return false;
    const auto colon = auth.find(':');
    u.host = auth.substr(0, colon);
    if (u.host.empty() || u.host.size() > 253 || u.host.back() == '.')
        return false;
    for (unsigned char c : u.host)
        if (!std::isalnum(c) && c != '.' && c != '-')
            return false;
    if (u.host.front() == '.' || u.host.find("..") != std::string::npos)
        return false;
    std::transform(u.host.begin(), u.host.end(), u.host.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    u.port = 443;
    if (colon != std::string::npos) {
        const auto port = auth.substr(colon + 1);
        if (port.empty() || port.size() > 5)
            return false;
        int n = 0;
        for (unsigned char c : port) {
            if (!std::isdigit(c))
                return false;
            n = n * 10 + c - '0';
        }
        if (n < 1 || n > 65535)
            return false;
        u.port = n;
    }
    u.authority = u.host + (u.port == 443 ? "" : ":" + std::to_string(u.port));
    u.target = path == std::string::npos ? "/" : text.substr(path);
    if (u.target.find('#') != std::string::npos)
        return false;
    out = std::move(u);
    return true;
}

bool TrustedDiscovery(const std::string& ota_url, const std::string& base_url,
                      const std::string& ota_origin, const std::string& control_origin) {
    Url ota, base, trusted, control;
    if (!ParseUrl(ota_url, ota) || !ParseUrl(base_url, base) || !ParseUrl(ota_origin, trusted) ||
        ota.scheme != "https" || base.scheme != "https" || trusted.scheme != "https" ||
        trusted.target != "/" || ota.Origin() != trusted.Origin() ||
        base.target != "/xiaozhi/device-control/v1/")
        return false;
    if (control_origin.empty())
        return base.Origin() == ota.Origin();
    return ParseUrl(control_origin, control) && control.scheme == "https" &&
           control.target == "/" && base.Origin() == control.Origin();
}

bool IsTrustedOtaUrl(const std::string& url, const std::string& ota_origin) {
    Url parsed, trusted;
    return ParseUrl(url, parsed) && ParseUrl(ota_origin, trusted) && parsed.scheme == "https" &&
           trusted.scheme == "https" && trusted.target == "/" &&
           parsed.Origin() == trusted.Origin();
}

bool HeaderValue(const std::string& s) {
    if (s.empty())
        return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 0x21 && c <= 0x7e; });
}

bool ValidateAckResponse(const std::string& json, uint64_t revision, uint64_t pv) {
    auto root = Parse(json);
    const cJSON* data = nullptr;
    int64_t r, p;
    return Envelope(root.get(), data) &&
           Object(data, {"accepted", "revision", "permission_version"}) &&
           cJSON_IsTrue(Item(data, "accepted")) && Int(Item(data, "revision"), r, 1) &&
           Int(Item(data, "permission_version"), p, 1) && static_cast<uint64_t>(r) == revision &&
           static_cast<uint64_t>(p) == pv;
}

bool ParseAccountDetails(const std::string& json, const std::string& device_id, uint64_t pv,
                         std::string& username, std::string& balance) {
    auto root = Parse(json);
    const cJSON* data = nullptr;
    std::string id, name, points;
    int64_t permission;
    if (!Envelope(root.get(), data) ||
        !Object(data, {"device_id", "permission_version", "username", "balance"}) ||
        !String(Item(data, "device_id"), id, 128) || id != device_id ||
        !Int(Item(data, "permission_version"), permission, 1) ||
        static_cast<uint64_t>(permission) != pv)
        return false;

    auto* user = Item(data, "username");
    if (!cJSON_IsNull(user) && (!String(user, name, 256) ||
                                std::any_of(name.begin(), name.end(),
                                            [](unsigned char ch) { return ch < 32 || ch == 127; })))
        return false;
    auto* amount = Item(data, "balance");
    if (!cJSON_IsNull(amount)) {
        if (!String(amount, points, 32))
            return false;
        size_t start = points.front() == '-' ? 1 : 0;
        const auto dot = points.find('.', start);
        if (start == points.size() || dot == start || dot == points.size() - 1)
            return false;
        for (size_t i = start; i < points.size(); ++i) {
            if (i != dot && (points[i] < '0' || points[i] > '9'))
                return false;
        }
    }
    username = std::move(name);
    balance = std::move(points);
    return true;
}

bool ValidateSignOutResponse(const std::string& json, uint64_t expected) {
    auto root = Parse(json);
    const cJSON* data = nullptr;
    int64_t permission;
    std::string binding;
    return expected < static_cast<uint64_t>(kSafeInt) && Envelope(root.get(), data) &&
           Object(data, {"permission_version", "binding_state"}) &&
           String(Item(data, "binding_state"), binding) && binding == "unbound" &&
           Int(Item(data, "permission_version"), permission, 1) &&
           static_cast<uint64_t>(permission) == expected + 1;
}

bool SignOutRequest::Begin(uint64_t permission) {
    if ((Pending() && (permission != expected_ || rejection_ == DeviceSignOutStatus::Idle)) ||
        permission == 0 || permission >= static_cast<uint64_t>(kSafeInt))
        return false;
    expected_ = permission;
    rejection_ = DeviceSignOutStatus::Idle;
    return true;
}

void SignOutRequest::Reject(DeviceSignOutStatus status) {
    if (Pending())
        rejection_ = status;
}

bool SignOutRequest::Observe(const DeviceControlView& state) {
    if (!Pending() || state.permission_version <= expected_)
        return false;
    status_ = state.reason == "unbound" && state.permission_version == expected_ + 1
                  ? DeviceSignOutStatus::Idle
                  : DeviceSignOutStatus::Failed;
    expected_ = 0;
    return true;
}

int64_t RetryDelayMs(const std::string& header, const std::string& json, int64_t unix_ms,
                     const std::string& response_date) {
    constexpr int64_t maximum = 9007199254740991LL;
    int64_t delay = 0;
    if (!header.empty() && std::all_of(header.begin(), header.end(),
                                       [](unsigned char c) { return std::isdigit(c); })) {
        for (unsigned char c : header) {
            if (delay > (maximum / 1000 - (c - '0')) / 10) {
                delay = maximum / 1000;
                break;
            }
            delay = delay * 10 + c - '0';
        }
        delay = std::min(maximum, delay * 1000);
    } else {
        const auto target = HttpDateMs(header);
        if (target >= 0) {
            const auto dated = HttpDateMs(response_date);
            if (dated >= 0)
                unix_ms = dated;
            // A response Date supplies a relative interval even before the first
            // state sync. Without either clock, reassess after a bounded delay;
            // do not mistake an unknown absolute date for a permanent lockout.
            delay = unix_ms > 0 ? std::max<int64_t>(0, target - unix_ms) : 60000;
        }
    }
    auto root = Parse(json);
    if (Object(root.get(), {"version", "code", "message", "retry_after_ms"})) {
        int64_t version, body_delay;
        std::string code, message;
        if (Int(Item(root.get(), "version"), version) && version == 1 &&
            String(Item(root.get(), "code"), code) && code == "RATE_LIMITED" &&
            String(Item(root.get(), "message"), message) &&
            Int(Item(root.get(), "retry_after_ms"), body_delay))
            delay = std::max(delay, body_delay);
    }
    return delay;
}

Model::Model(std::string device_id, std::string origin)
    : device_id_(std::move(device_id)), origin_(std::move(origin)) {
    view_.generation = 1;
}

std::string PendingCreate::Select(const std::string& intent, uint64_t pv,
                                  const std::string& fresh_key) {
    if (key_.empty() || intent_ != intent || permission_version_ != pv) {
        intent_ = intent;
        permission_version_ = pv;
        key_ = fresh_key;
    }
    return key_;
}
void PendingCreate::Complete(int status) {
    if ((status >= 200 && status < 300) || status == 409 || status == 410)
        Clear();
}

void PendingAck::Set(uint64_t revision, uint64_t permission, bool audio_stopped) {
    pending_ = true;
    revision_ = revision;
    permission_ = permission;
    audio_stopped_ = audio_stopped;
}
void PendingAck::Rebase(const DeviceControlView& view) {
    if (!pending_)
        return;
    if (permission_ != view.permission_version) {
        Clear();  // The application must first apply and stop the new permission version.
        return;
    }
    revision_ = view.revision;
}
void PendingAck::Confirm(uint64_t revision, uint64_t permission, bool audio_stopped) {
    if (revision_ == revision && permission_ == permission && audio_stopped_ == audio_stopped)
        Clear();
}

ApplyResult Model::ApplyState(const std::string& json, int64_t now, int64_t elapsed) {
    // Expiry is a generation boundary even if the timer task has not run yet.
    Tick(now);
    auto root = Parse(json);
    const cJSON* data = nullptr;
    DeviceControlView next;
    DeviceVoiceConfig voice;
    int64_t revision, pv, server;
    std::string id, binding, permission, reason;
    auto invalid = [this]() {
        Revoke("invalid_state");
        return ApplyResult::Invalid;
    };
    if (elapsed < 0 || elapsed >= 25000 || !Envelope(root.get(), data) ||
        !Object(data,
                {"device_id", "revision", "permission_version", "server_time_ms", "binding_state",
                 "use_permission", "reason", "binding_request", "lease", "voice"}) ||
        !String(Item(data, "device_id"), id, 128) || id != device_id_ ||
        !Int(Item(data, "revision"), revision, 1) ||
        !Int(Item(data, "permission_version"), pv, 1) ||
        !Int(Item(data, "server_time_ms"), server) ||
        !String(Item(data, "binding_state"), binding) ||
        !String(Item(data, "use_permission"), permission) || !String(Item(data, "reason"), reason))
        return invalid();
    next.revision = revision;
    next.permission_version = pv;
    next.reason = reason;
    next.voice_allowed = permission == "allowed";
    bool valid = (binding == "unbound" && permission == "blocked" && reason == "unbound") ||
                 (binding == "bound" && permission == "allowed" && reason == "ready") ||
                 (binding == "bound" && permission == "blocked" &&
                  (reason == "user_signed_out" || reason == "market_auth_required" ||
                   reason == "market_auth_pending")) ||
                 ((binding == "bound" || binding == "unbound") && permission == "blocked" &&
                  reason == "device_disabled");
    std::string stable, request_id;
    Part(stable, id);
    Part(stable, std::to_string(pv));
    Part(stable, binding);
    Part(stable, permission);
    Part(stable, reason);
    std::string permission_stable;
    Part(permission_stable, binding);
    Part(permission_stable, permission);
    Part(permission_stable, reason);
    if (!valid ||
        !Request(Item(data, "binding_request"), server, now, elapsed, next, stable, request_id))
        return invalid();
    if ((!next.purpose.empty() && binding == "unbound" && next.purpose != "bind") ||
        (binding == "bound" && next.purpose == "bind") ||
        (reason == "device_disabled" && !next.purpose.empty()))
        return invalid();
    int64_t lease_deadline = 0;
    const auto lease = Item(data, "lease");
    const auto v = Item(data, "voice");
    if (next.voice_allowed) {
        int64_t lease_pv, expires, remaining, voice_pv, version, voice_expires;
        if (!Object(lease, {"permission_version", "expires_at_ms", "expires_in_ms"}) ||
            !Int(Item(lease, "permission_version"), lease_pv, 1) || lease_pv != pv ||
            !Int(Item(lease, "expires_at_ms"), expires) ||
            !Int(Item(lease, "expires_in_ms"), remaining) || remaining != 60000 ||
            expires - server != 60000 ||
            !Object(v,
                    {"url", "token", "expires_at_ms", "protocol_version", "permission_version"}) ||
            !String(Item(v, "url"), voice.url, 2048) ||
            !String(Item(v, "token"), voice.token, 8192) || !HeaderValue(voice.token) ||
            !Int(Item(v, "expires_at_ms"), voice_expires) || voice_expires <= server ||
            voice_expires - server > 300000 || !Int(Item(v, "protocol_version"), version) ||
            version != 1 || !Int(Item(v, "permission_version"), voice_pv, 1) || voice_pv != pv)
            return invalid();
        Url vu, origin;
        if (!ParseUrl(voice.url, vu) || vu.scheme != "wss" || !ParseUrl(origin_, origin) ||
            vu.host != origin.host || vu.port != origin.port || vu.target != "/xiaozhi/v1/")
            return invalid();
        lease_deadline = now + 60000 - elapsed;
        voice.device_id = device_id_;
        voice.permission_version = pv;
        voice.protocol_version = 1;
        voice.handshake_deadline_ms = now + std::max<int64_t>(0, voice_expires - server - elapsed);
        Part(stable, voice.url);
        Part(stable, "1");
        Part(permission_stable, voice.url);
        Part(permission_stable, "1");
    } else if (!cJSON_IsNull(lease) || !cJSON_IsNull(v))
        return invalid();
    if (have_state_ &&
        (next.revision < view_.revision || next.permission_version < view_.permission_version))
        return ApplyResult::Stale;
    if (have_state_ && next.permission_version == view_.permission_version &&
        permission_stable != permission_stable_)
        return invalid();
    if (have_state_ && next.revision == view_.revision && stable != stable_)
        return invalid();
    if (have_state_ && next.permission_version != view_.permission_version &&
        next.revision <= view_.revision)
        return invalid();
    if (!request_id.empty()) {
        if (request_id == known_request_id_)
            next.qr_deadline_ms = std::min(next.qr_deadline_ms, known_request_deadline_ms_);
        if (request_id == discarded_request_id_)
            next.qr_deadline_ms = 0;
        known_request_id_ = request_id;
        known_request_deadline_ms_ = next.qr_deadline_ms;
    }
    server_request_active_ = !request_id.empty() && request_id != discarded_request_id_;
    next.generation = view_.generation;
    if (!have_state_ || next.permission_version != view_.permission_version ||
        next.voice_allowed != view_.voice_allowed)
        ++next.generation;
    voice.generation = next.generation;
    view_ = std::move(next);
    voice_ = std::move(voice);
    stable_ = std::move(stable);
    permission_stable_ = std::move(permission_stable);
    lease_deadline_ms_ = lease_deadline;
    have_state_ = true;
    last_server_ms_ = server;
    last_received_ms_ = now;
    Tick(now);
    return ApplyResult::Applied;
}

ApplyResult Model::ApplyRequest(const std::string& json, int64_t now, int64_t elapsed) {
    Tick(now);
    auto root = Parse(json);
    const cJSON* data = nullptr;
    int64_t revision, pv, server;
    if (!Envelope(root.get(), data) ||
        !Object(data, {"revision", "permission_version", "server_time_ms", "binding_request"}) ||
        !Int(Item(data, "revision"), revision, 1) ||
        !Int(Item(data, "permission_version"), pv, 1) ||
        !Int(Item(data, "server_time_ms"), server) || elapsed < 0 || elapsed >= 25000) {
        Revoke("invalid_state");
        return ApplyResult::Invalid;
    }
    if (!have_state_ || pv != static_cast<int64_t>(view_.permission_version) ||
        revision < static_cast<int64_t>(view_.revision))
        return ApplyResult::Stale;
    // Partial request responses never advance the full-state cursor or renew a lease.
    DeviceControlView next = view_;
    next.verification_uri.clear();
    next.purpose.clear();
    next.qr_deadline_ms = 0;
    std::string ignored, request_id;
    if (!Request(Item(data, "binding_request"), server, now, elapsed, next, ignored, request_id) ||
        next.verification_uri.empty()) {
        Revoke("invalid_state");
        return ApplyResult::Invalid;
    }
    if (request_id == known_request_id_)
        next.qr_deadline_ms = std::min(next.qr_deadline_ms, known_request_deadline_ms_);
    if (request_id == discarded_request_id_)
        next.qr_deadline_ms = 0;
    known_request_id_ = request_id;
    known_request_deadline_ms_ = next.qr_deadline_ms;
    server_request_active_ = request_id != discarded_request_id_;
    view_.verification_uri = next.verification_uri;
    view_.purpose = next.purpose;
    view_.qr_deadline_ms = next.qr_deadline_ms;
    Tick(now);
    return ApplyResult::Applied;
}

bool Model::Allowed(int64_t now) const { return view_.voice_allowed && now < lease_deadline_ms_; }
bool Model::DiscardRequest() {
    const bool visible = !view_.verification_uri.empty();
    if (!known_request_id_.empty())
        discarded_request_id_ = known_request_id_;
    known_request_deadline_ms_ = 0;
    server_request_active_ = false;
    view_.verification_uri.clear();
    view_.purpose.clear();
    view_.qr_deadline_ms = 0;
    return visible;
}
void Model::AdvanceGeneration(uint64_t floor) {
    view_.generation = std::max(view_.generation + 1, floor);
    voice_.generation = view_.generation;
}
bool Model::Voice(int64_t now, DeviceVoiceConfig& out) const {
    if (!Allowed(now) || voice_.handshake_deadline_ms - now < 10000)
        return false;
    out = voice_;
    return true;
}
bool Model::Revoke(const std::string& reason) {
    const bool changed = view_.voice_allowed;
    if (changed)
        ++view_.generation;
    view_.voice_allowed = false;
    view_.reason = reason;
    lease_deadline_ms_ = 0;
    voice_ = {};
    return changed;
}
bool Model::Tick(int64_t now) {
    bool changed = false;
    if (view_.voice_allowed && now >= lease_deadline_ms_)
        changed = Revoke("control_unavailable");
    if (!view_.verification_uri.empty() && now >= view_.qr_deadline_ms) {
        view_.verification_uri.clear();
        view_.purpose.clear();
        view_.qr_deadline_ms = 0;
        changed = true;
    }
    return changed;
}
std::string Model::AutoIntent() const {
    if (!have_state_ || server_request_active_)
        return {};
    if (view_.reason == "unbound" || view_.reason == "user_signed_out" ||
        view_.reason == "market_auth_required" || view_.reason == "market_auth_pending")
        return "auto";
    return {};
}
}  // namespace device_control
