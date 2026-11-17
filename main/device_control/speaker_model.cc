#include "speaker_model.h"
#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace device_control {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json Parse(const std::string& text) {
    if (text.empty() || text.size() > 16384 || text.find('\0') != std::string::npos ||
        text.find("\\u0000") != std::string::npos)
        return Json(nullptr, cJSON_Delete);
    return Json(cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true), cJSON_Delete);
}
bool Fields(const cJSON* object, std::set<std::string> allowed) {
    if (!cJSON_IsObject(object)) return false;
    for (auto item = object->child; item; item = item->next)
        if (!item->string || allowed.erase(item->string) != 1) return false;
    return true;
}
const cJSON* Get(const cJSON* object, const char* name) { return cJSON_GetObjectItemCaseSensitive(object, name); }
std::string Text(const cJSON* object, const char* name) {
    auto item = Get(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}
int64_t Number(const cJSON* object, const char* name) {
    auto item = Get(object, name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > 9007199254740991.0 || std::floor(item->valuedouble) != item->valuedouble)
        return -1;
    return static_cast<int64_t>(item->valuedouble);
}
bool Safe(const std::string& value, size_t min, size_t max, const std::string& alphabet) {
    return value.size() >= min && value.size() <= max && value.find_first_not_of(alphabet) == std::string::npos;
}
const std::string kToken = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";
std::string Reason(const std::string& status) {
    if (status == "signed_out") return "user_signed_out";
    if (status == "authorization_required") return "market_auth_required";
    if (status == "admin_disabled") return "device_disabled";
    return status;
}
}
void SpeakerModel::ClearQr() {
    view_.verification_uri.clear(); view_.user_code.clear(); view_.purpose.clear(); view_.qr_deadline_ms = 0;
}
void SpeakerModel::AdvanceGeneration(uint64_t floor) {
    view_.generation = std::max(view_.generation + 1, floor);
    voice_.generation = view_.generation;
}
bool SpeakerModel::Revoke(const std::string& reason) {
    bool changed = view_.voice_allowed;
    if (changed) AdvanceGeneration();
    view_.voice_allowed = false; view_.reason = reason; voice_ = {}; deadline_ = 0; ClearQr();
    return changed;
}
bool SpeakerModel::Allowed(int64_t now) const { return view_.voice_allowed && now < deadline_; }
bool SpeakerModel::Voice(int64_t now, DeviceVoiceConfig& out) const {
    if (!Allowed(now) || now >= voice_.handshake_deadline_ms) return false;
    out = voice_; return true;
}
bool SpeakerModel::Tick(int64_t now) {
    bool changed = false;
    if (view_.voice_allowed && now >= deadline_) { Revoke("synchronizing"); changed = true; }
    if (!view_.verification_uri.empty() && now >= view_.qr_deadline_ms) { ClearQr(); view_.login_status = "expired"; terminal_ = true; changed = true; }
    return changed;
}
void SpeakerModel::RetryLogin() { terminal_ = false; login_id_.clear(); ClearQr(); }
bool SpeakerModel::CanLogin() const {
    return !terminal_ && login_id_.empty() && (view_.reason == "unbound" ||
        view_.reason == "user_signed_out" || view_.reason == "market_auth_required");
}
ApplyResult SpeakerModel::State(const std::string& json, int64_t now, int64_t elapsed) {
    auto root = Parse(json); auto data = root.get();
    if (!Fields(data, {"protocol_version","device_id","binding_version","permission_version","binding_state",
            "use_status","server_time_ms","lease_expires_in_ms","login_id","customer_name","voice"}) ||
        Number(data,"protocol_version") != 2 || Text(data,"device_id") != id_ || elapsed < 0)
        return ApplyResult::Invalid;
    const auto pv = Number(data,"permission_version"), binding = Number(data,"binding_version"), server = Number(data,"server_time_ms");
    const auto status = Text(data,"use_status"), bound = Text(data,"binding_state");
    const std::set<std::string> statuses = {"ready","unbound","signed_out","authorization_required","admin_disabled","default_agent_required","voice_not_configured"};
    if (pv < 0 || binding < 0 || server <= 0 || !statuses.count(status) ||
        (bound != "bound" && bound != "unbound") || (status == "ready" && bound != "bound"))
        return ApplyResult::Invalid;
    if (server < server_ms_ || static_cast<uint64_t>(pv) < view_.permission_version || static_cast<uint64_t>(binding) < binding_)
        return ApplyResult::Stale;
    DeviceVoiceConfig next;
    int64_t lease = Number(data,"lease_expires_in_ms");
    if (status == "ready") {
        auto voice = Get(data,"voice"); Url url;
        auto address = Text(voice,"url"), token = Text(voice,"token");
        const auto expires = Number(voice,"expires_at_ms");
        if (!Fields(voice,{"url","token","expires_at_ms","protocol_version","permission_version"}) ||
            !ParseUrl(address,url) || url.scheme != "wss" || url.target != "/xiaozhi/v1/" ||
            !Safe(token,43,43,kToken) || Number(voice,"protocol_version") != 1 || Number(voice,"permission_version") != pv ||
            expires <= server + elapsed || expires - server > 300000 || lease < 1000 || lease > 60000 || lease <= elapsed)
            return ApplyResult::Invalid;
        next.url = address; next.token = token; next.device_id = id_; next.permission_version = pv;
        next.protocol_version = 1; next.handshake_deadline_ms = now + expires - server - elapsed;
    } else if (!cJSON_IsNull(Get(data,"voice")) || lease != 0) return ApplyResult::Invalid;
    const auto login = Text(data,"login_id"), name = Text(data,"customer_name");
    if ((!login.empty() && !Safe(login,36,36,kToken + "-")) || name.size() > 512) return ApplyResult::Invalid;
    bool changed = static_cast<uint64_t>(pv) != view_.permission_version || static_cast<uint64_t>(binding) != binding_ ||
        view_.voice_allowed != (status == "ready");
    if (changed) { AdvanceGeneration(); ClearQr(); terminal_ = false; }
    if (login != login_id_) ClearQr();
    login_id_ = login; customer_name_ = name; binding_ = binding; server_ms_ = server;
    view_.permission_version = pv; view_.revision++; view_.reason = Reason(status); view_.voice_allowed = status == "ready";
    voice_ = next; voice_.generation = view_.generation; deadline_ = status == "ready" ? now + lease - elapsed : 0;
    return ApplyResult::Applied;
}
ApplyResult SpeakerModel::Login(const std::string& json, int64_t now, int64_t elapsed) {
    auto root = Parse(json); auto data = root.get();
    if (!Fields(data,{"id","status","verification_uri","user_code","expires_at_ms","error_code","server_time_ms"}) || elapsed < 0)
        return ApplyResult::Invalid;
    auto id = Text(data,"id"), status = Text(data,"status");
    auto server = Number(data,"server_time_ms"), expires = Number(data,"expires_at_ms");
    if (!Safe(id,36,36,kToken + "-") || (!login_id_.empty() && login_id_ != id) || server <= 0) return ApplyResult::Invalid;
    if (server < server_ms_) return ApplyResult::Stale;
    if (status == "completed" || status == "denied" || status == "expired" || status == "failed" || status == "cancelled") {
        ClearQr(); login_id_.clear(); terminal_ = true;
        view_.login_status = status; view_.revision++; return ApplyResult::Applied;
    }
    if (status != "pending" && status != "polling" && status != "authorized" && status != "requesting" && status != "preparing")
        return ApplyResult::Invalid;
    auto uri = Text(data,"verification_uri"), code = Text(data,"user_code"); Url parsed;
    if (!uri.empty() && (!ParseUrl(uri,parsed) || parsed.scheme != "https" || uri.size() > 512 ||
        !Safe(code,1,64,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789- ") ||
        expires - server > 600000)) return ApplyResult::Invalid;
    login_id_ = id; view_.login_status = status; view_.revision++;
    if (!uri.empty() && expires > server + elapsed) {
        view_.verification_uri = uri; view_.user_code = code; view_.purpose = "device_login";
        view_.qr_deadline_ms = now + expires - server - elapsed;
    } else ClearQr();
    return ApplyResult::Applied;
}
}
