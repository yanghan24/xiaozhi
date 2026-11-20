#include "speaker_account.h"

#include "application.h"
#include "board.h"
#include "cjson_utils.h"
#include "display.h"

#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <nvs.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {

constexpr int64_t kStateInterval = 4000;
constexpr int64_t kHeartbeatInterval = 20000;
constexpr int64_t kMaxLease = 60000;
constexpr size_t kResponseLimit = 16384;
constexpr size_t kEnrollmentIdLength = 36;
constexpr size_t kEnrollmentProofLength = 64;
#ifdef CONFIG_XIAOZHI_SPEAKER_ENROLLMENT_ORIGIN
constexpr std::string_view kEnrollmentOrigin = CONFIG_XIAOZHI_SPEAKER_ENROLLMENT_ORIGIN;
#else
constexpr std::string_view kEnrollmentOrigin;
#endif

int64_t Now() {
    return esp_timer_get_time() / 1000;
}

bool Token(std::string_view text, size_t min_size, size_t max_size, std::string_view extra) {
    return text.size() >= min_size && text.size() <= max_size &&
           std::all_of(text.begin(), text.end(), [extra](char ch) {
               return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || extra.find(ch) != std::string_view::npos;
           });
}

bool SecureUrl(std::string_view value, std::string_view scheme, bool origin_only = false) {
    if (!value.starts_with(scheme) || value.size() > 1024 ||
        std::any_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch <= 32 || ch >= 127 || ch == '\\';
        })) {
        return false;
    }
    const auto address = value.substr(scheme.size());
    const auto authority = address.substr(0, address.find_first_of("/?#"));
    return !authority.empty() && authority.find('@') == std::string_view::npos &&
           (!origin_only || address == authority);
}

std::string Text(const cJSON* object, const char* key, size_t limit = 1024) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || !value->valuestring) {
        return {};
    }
    const std::string_view text(value->valuestring);
    return text.size() <= limit ? std::string(text) : std::string();
}

int64_t Number(const cJSON* object, const char* key) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble < 0 || value->valuedouble > 9007199254740991.0 ||
        std::trunc(value->valuedouble) != value->valuedouble) {
        return -1;
    }
    return static_cast<int64_t>(value->valuedouble);
}

CJsonUniquePtr Parse(const std::string& body) {
    return CJsonUniquePtr(cJSON_ParseWithOpts(body.c_str(), nullptr, true));
}

std::string NewRequestKey() {
    std::array<uint8_t, 16> random{};
    esp_fill_random(random.data(), random.size());
    constexpr std::string_view digits = "0123456789abcdef";
    std::string key;
    key.reserve(32);
    for (uint8_t byte : random) {
        key += digits[byte >> 4];
        key += digits[byte & 15];
    }
    return key;
}

struct PendingUnbind {
    int64_t permission = 0;
    std::array<char, 33> key{};

    bool active() const {
        return permission > 0 && key.back() == '\0' && Token(key.data(), 32, 32, "");
    }
};

PendingUnbind ReadUnbind() {
    PendingUnbind value;
    nvs_handle_t handle;
    if (nvs_open("device_ctrl", NVS_READONLY, &handle) == ESP_OK) {
        auto size = sizeof(value);
        const auto result = nvs_get_blob(handle, "market_unbind", &value, &size);
        nvs_close(handle);
        if (result == ESP_OK && size == sizeof(value) && value.active()) {
            return value;
        }
    }
    return {};
}

bool StoreUnbind(const PendingUnbind& value) {
    nvs_handle_t handle;
    if (nvs_open("device_ctrl", NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const auto written = value.active() ?
        nvs_set_blob(handle, "market_unbind", &value, sizeof(value)) :
        nvs_erase_key(handle, "market_unbind");
    const bool saved = (written == ESP_OK || written == ESP_ERR_NVS_NOT_FOUND) &&
                       nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
}

// An empty value means a fresh device. A read error must not replace an existing identity.
std::optional<std::string> ReadStored(const char* key) {
    nvs_handle_t handle;
    const auto opened = nvs_open("device_ctrl", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) {
        return std::string();
    }
    if (opened != ESP_OK) {
        return std::nullopt;
    }
    size_t length = 0;
    const auto sized = nvs_get_blob(handle, key, nullptr, &length);
    if (sized != ESP_OK || length == 0 || length > 2048) {
        nvs_close(handle);
        return sized == ESP_ERR_NVS_NOT_FOUND ? std::optional<std::string>(std::string()) : std::nullopt;
    }
    std::string data(length, '\0');
    const auto result = nvs_get_blob(handle, key, data.data(), &length);
    nvs_close(handle);
    if (result != ESP_OK || length != data.size()) {
        return std::nullopt;
    }
    return data;
}

bool StoreValue(const char* key, std::string_view value) {
    nvs_handle_t handle;
    if (nvs_open("device_ctrl", NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const auto result = value.empty() ? nvs_erase_key(handle, key)
                                      : nvs_set_blob(handle, key, value.data(), value.size());
    const bool saved = (result == ESP_OK || (value.empty() && result == ESP_ERR_NVS_NOT_FOUND)) &&
                       nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
}

bool SaveIdentity(const SpeakerAccount::Identity& identity, const std::string& hardware_id) {
    CJsonUniquePtr document(cJSON_CreateObject());
    if (!document || !cJSON_AddNumberToObject(document.get(), "version", 1) ||
        !cJSON_AddStringToObject(document.get(), "hardware_id", hardware_id.c_str()) ||
        !cJSON_AddStringToObject(document.get(), "device_id", identity.id.c_str()) ||
        !cJSON_AddStringToObject(document.get(), "credential", identity.credential.c_str()) ||
        !cJSON_AddStringToObject(document.get(), "control_origin", identity.origin.c_str()) ||
        !cJSON_AddStringToObject(document.get(), "ota_url", identity.OtaUrl().c_str())) {
        return false;
    }
    char* serialized = cJSON_PrintUnformatted(document.get());
    if (!serialized) {
        return false;
    }
    const bool saved = StoreValue("identity_v1", serialized);
    cJSON_free(serialized);
    return saved;
}

bool LowerHex(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

std::optional<SpeakerAccount::Identity> LoadOrCreateEnrollment() {
    auto stored = ReadStored("enrollment_v1");
    if (!stored) {
        return std::nullopt;
    }
    if (stored->empty()) {
        *stored = "enr-" + NewRequestKey() + NewRequestKey() + NewRequestKey();
        if (!StoreValue("enrollment_v1", *stored)) {
            return std::nullopt;
        }
    }
    if (stored->size() != kEnrollmentIdLength + kEnrollmentProofLength ||
        !stored->starts_with("enr-") || !LowerHex(std::string_view(*stored).substr(4))) {
        return std::nullopt;
    }
    return SpeakerAccount::Identity{stored->substr(0, kEnrollmentIdLength),
                                    stored->substr(kEnrollmentIdLength), std::string(kEnrollmentOrigin)};
}

}  // namespace

SpeakerAccount& SpeakerAccount::Get() {
    static SpeakerAccount account;
    return account;
}

std::string SpeakerAccount::Identity::OtaUrl() const {
    return origin.empty() ? std::string() : origin + "/xiaozhi/ota/";
}

SpeakerAccount::Identity SpeakerAccount::identity() const {
    std::lock_guard lock(mutex_);
    return identity_;
}

void SpeakerAccount::LoadIdentity() {
    identity_ = {};
    enrollment_allowed_ = false;
    std::array<uint8_t, 6> mac{};
    if (esp_efuse_mac_get_default(mac.data()) != ESP_OK) {
        return;
    }
    char hardware[13];
    std::snprintf(hardware, sizeof(hardware), "%02x%02x%02x%02x%02x%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    hardware_id_ = hardware;
    const auto data = ReadStored("identity_v1");
    if (!data || data->empty()) {
        enrollment_allowed_ = data.has_value() && SecureUrl(kEnrollmentOrigin, "https://", true);
        return;
    }
    const auto document = Parse(*data);
    if (!document || Number(document.get(), "version") != 1) {
        return;
    }
    Identity identity{Text(document.get(), "device_id", 128), Text(document.get(), "credential", 128),
                      Text(document.get(), "control_origin", 512)};
    if (Text(document.get(), "hardware_id", 12) != hardware_id_ ||
        !Token(identity.id, 1, 128, "_.:-") || !Token(identity.credential, 43, 128, "_-") ||
        !SecureUrl(identity.origin, "https://", true) ||
        Text(document.get(), "ota_url", 1024) != identity.OtaUrl()) {
        return;
    }
    identity_ = std::move(identity);
}

void SpeakerAccount::Start() {
    std::lock_guard lock(mutex_);
    if (worker_ == nullptr && (!identity_.id.empty() || enrollment_allowed_)) {
        xTaskCreate([](void* context) { static_cast<SpeakerAccount*>(context)->Run(); },
                    "speaker_account", 8192, this, 2, &worker_);
    }
}

void SpeakerAccount::NetworkChanged(bool connected) {
    {
        std::lock_guard lock(mutex_);
        if (connected_ == connected) {
            return;
        }
        connected_ = connected;
        ++network_epoch_;
        if (!connected) {
            voice_.reset();
            voice_until_ = 0;
        }
        if (identity_.id.empty() && !enrollment_allowed_) {
            view_ = {Page::Unprovisioned};
        } else if (!connected) {
            view_ = {Page::Offline};
        } else {
            view_ = {Page::Connecting};
        }
    }
    Notify();
    Application::GetInstance().Schedule([] {
        RenderSpeakerAccount(SpeakerAccount::Get().GetView());
    });
}

void SpeakerAccount::SetUpgrading(bool upgrading) {
    {
        std::lock_guard lock(mutex_);
        upgrading_ = upgrading;
        ++network_epoch_;
        if (upgrading) {
            voice_.reset();
            voice_until_ = 0;
        }
    }
    Notify();
}

std::optional<SpeakerAccount::VoiceAccess> SpeakerAccount::Voice() const {
    std::lock_guard lock(mutex_);
    return connected_ && !upgrading_ && !unbind_ && !view_.unbind_pending && Now() < voice_until_
               ? voice_ : std::nullopt;
}

SpeakerAccount::View SpeakerAccount::GetView() const {
    std::lock_guard lock(mutex_);
    return view_;
}

void SpeakerAccount::RefreshCode() {
    {
        std::lock_guard lock(mutex_);
        refresh_ = true;
    }
    Notify();
}

void SpeakerAccount::Unbind() {
    {
        std::lock_guard lock(mutex_);
        if (!view_.can_unbind || view_.unbind_pending || permission_version_ <= 0) {
            return;
        }
        unbind_ = true;
        view_.unbind_pending = true;
        voice_.reset();
        voice_until_ = 0;
    }
    Application::GetInstance().Schedule([] {
        auto& app = Application::GetInstance();
        app.OnSpeakerPermissionRevoked();
        RenderSpeakerAccount(SpeakerAccount::Get().GetView());
    });
    Notify();
}

void SpeakerAccount::Notify() {
    std::lock_guard lock(mutex_);
    if (worker_ != nullptr) {
        xTaskNotifyGive(worker_);
    }
}

SpeakerAccount::Response SpeakerAccount::Request(const char* method, const std::string& path,
                                                 std::string body) {
    return RequestWithIdentity(identity_, method, path, std::move(body));
}

SpeakerAccount::Response SpeakerAccount::RequestWithIdentity(const Identity& identity, const char* method,
                                                             const std::string& path, std::string body) {
    {
        std::lock_guard lock(mutex_);
        if (!connected_ || upgrading_) {
            return {};
        }
    }
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
    if (!http) {
        return {};
    }
    http->SetTimeout(5000);
    http->SetHeader("Device-Id", identity.id);
    http->SetHeader("Authorization", "Bearer " + identity.credential);
    http->SetHeader("Content-Type", "application/json");
    // An absent POST body opens a chunked upload in HttpClient. These requests send a complete body.
    if (!body.empty() || std::string_view(method) == "POST") {
        http->SetContent(std::move(body));
    }
    if (!http->Open(method, identity.origin + "/xiaozhi/speaker/v1" + path)) {
        return {};
    }
    const auto status = http->GetStatusCode();
    Response response{status ? *status : 0, {}};
    std::array<char, 512> buffer;
    while (true) {
        const auto received = http->Read(buffer.data(), buffer.size());
        if (!received || *received < 0 || static_cast<size_t>(*received) > buffer.size() ||
            response.body.size() + *received > kResponseLimit) {
            http->Close();
            return {};
        }
        if (*received == 0) {
            break;
        }
        response.body.append(buffer.data(), *received);
    }
    http->Close();
    return response;
}

void SpeakerAccount::Publish(View view, std::optional<VoiceAccess> access, int64_t lease_until,
                             int64_t permission, bool revoke_if_changed, uint32_t network_epoch) {
    bool changed;
    bool revoke;
    {
        std::lock_guard lock(mutex_);
        if (network_epoch_ != network_epoch || upgrading_ || unbind_) {
            return;
        }
        changed = view_ != view;
        revoke = voice_.has_value() && (!access ||
                 (revoke_if_changed && permission_version_ != permission));
        view_ = std::move(view);
        voice_ = std::move(access);
        voice_until_ = lease_until;
        permission_version_ = permission;
    }
    if (changed || revoke) {
        Application::GetInstance().Schedule([revoke] {
            if (revoke) {
                Application::GetInstance().OnSpeakerPermissionRevoked();
            }
            RenderSpeakerAccount(SpeakerAccount::Get().GetView());
        });
    }
}

SpeakerAccount::LoginResult SpeakerAccount::PollLogin(const Identity& identity, std::string& login_id,
                                                       bool& retry_login, const std::string& customer,
                                                       bool bound, int64_t permission, uint32_t epoch) {
    if (retry_login) {
        Publish({Page::Retry, {}, {}, customer, bound}, {}, 0, permission, true, epoch);
        return LoginResult::InProgress;
    }
    const auto login = login_id.empty() ? RequestWithIdentity(identity, "POST", "/login")
                                         : RequestWithIdentity(identity, "GET", "/login/" + login_id);
    const auto result = Parse(login.body);
    if ((login.status != 200 && login.status != 201) || !result) {
        retry_login = login.status == 404 || login.status == 409;
        Publish({retry_login ? Page::Retry : Page::Connecting, {}, {}, customer, bound},
                {}, 0, permission, true, epoch);
        return login.status == 401 ? LoginResult::InvalidCredential : LoginResult::InProgress;
    }
    const auto transaction = Text(result.get(), "id", 128);
    if (Token(transaction, 1, 128, "-_")) {
        login_id = transaction;
    }
    const auto status = Text(result.get(), "status", 32);
    if (status == "completed") {
        login_id.clear();
        return LoginResult::Completed;
    }
    auto qr_url = Text(result.get(), "verification_uri");
    auto code = Text(result.get(), "user_code", 32);
    if (!login_id.empty() && SecureUrl(qr_url, "https://") && Token(code, 1, 32, "- ")) {
        Publish({Page::Scan, std::move(qr_url), std::move(code), customer, bound},
                {}, 0, permission, true, epoch);
    } else {
        retry_login = status == "expired" || status == "denied" || status == "cancelled" || status == "failed";
        Publish({retry_login ? Page::Retry : Page::Connecting, {}, {}, customer, bound},
                {}, 0, permission, true, epoch);
    }
    return LoginResult::InProgress;
}

void SpeakerAccount::Run() {
    uint32_t epoch = 0;
    int64_t next_heartbeat = 0;
    int64_t next_state = 0;
    std::string login_id;
    bool retry_login = false;
    auto pending = ReadUnbind();
    std::optional<Identity> enrollment;
    bool enrollment_registered = false;
    bool enrollment_notice_shown = false;
    const auto saved_enrollment = ReadStored("enrollment_v1");
    bool acknowledge_enrollment = identity_.id.starts_with("enr-") && saved_enrollment && !saved_enrollment->empty();

    while (true) {
        bool enabled, refresh, new_unbind;
        int64_t permission;
        uint32_t current_epoch;
        {
            std::lock_guard lock(mutex_);
            enabled = connected_ && !upgrading_;
            current_epoch = network_epoch_;
            refresh = enabled && std::exchange(refresh_, false);
            new_unbind = enabled && std::exchange(unbind_, false);
            permission = permission_version_;
        }
        const auto now = Now();
        if (!enabled) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
        if (epoch != current_epoch || refresh) {
            epoch = current_epoch;
            next_state = 0;
            login_id.clear();
            retry_login = false;
            if (!refresh) {
                next_heartbeat = 0;
                enrollment_registered = false;
            }
        }
        if (identity_.id.empty()) {
            if (now < next_state) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
                continue;
            }
            next_state = now + kStateInterval;
            if (!enrollment) {
                enrollment = LoadOrCreateEnrollment();
                if (!enrollment) {
                    Publish({Page::Unprovisioned}, {}, 0, 0, true, epoch);
                    continue;
                }
            }
            if (!enrollment_registered) {
                Publish({Page::Connecting}, {}, 0, 0, true, epoch);
                if (!enrollment_notice_shown) {
                    enrollment_notice_shown = true;
                    Application::GetInstance().Schedule([] {
                        if (SpeakerAccount::Get().GetView().page == Page::Connecting) {
                            Board::GetInstance().GetDisplay()->ShowNotification("正在准备登录");
                        }
                    });
                }
                const std::string report = "{\"board_type\":\"" + std::string(RegisteredBoard()) +
                    "\",\"firmware_version\":\"" + esp_app_get_description()->version + "\"}";
                const auto response = RequestWithIdentity(*enrollment, "POST", "/enrollment", report);
                const auto document = Parse(response.body);
                if (response.status != 200 || !document || Text(document.get(), "device_id") != enrollment->id) {
                    if (response.status == 401 || response.status == 403) {
                        Publish({Page::Disabled}, {}, 0, 0, true, epoch);
                    }
                    continue;
                }
                const auto status = Text(document.get(), "status");
                if (status == "issued") {
                    auto credential = Text(document.get(), "credential", 128);
                    Identity assigned{enrollment->id, std::move(credential), enrollment->origin};
                    if (!Token(assigned.credential, 43, 128, "_-") || !SaveIdentity(assigned, hardware_id_)) {
                        continue;
                    }
                    {
                        std::lock_guard lock(mutex_);
                        identity_ = std::move(assigned);
                    }
                    acknowledge_enrollment = true;
                    enrollment.reset();
                    login_id.clear();
                    next_state = 0;
                    next_heartbeat = 0;
                    continue;
                }
                if (status != "pending") {
                    continue;
                }
                enrollment_registered = true;
            }
            const auto result = PollLogin(*enrollment, login_id, retry_login, {}, false, 0, epoch);
            if (result != LoginResult::InProgress) {
                enrollment_registered = false;
                login_id.clear();
                next_state = 0;
            }
            continue;
        }
        if (new_unbind && !pending.active()) {
            pending.permission = permission;
            const auto key = NewRequestKey();
            std::copy(key.begin(), key.end(), pending.key.begin());
            if (!StoreUnbind(pending)) {
                pending = {};
                auto view = GetView();
                view.unbind_pending = false;
                Publish(std::move(view), {}, 0, permission, true, epoch);
                ESP_LOGW("SpeakerAccount", "Could not persist unbind request");
                continue;
            }
            next_state = 0;
        }
        if (now >= next_heartbeat) {
            std::string report = "{\"board_type\":\"" + std::string(RegisteredBoard()) +
                                 "\",\"firmware_version\":\"" + esp_app_get_description()->version + "\"}";
            const auto heartbeat = Request("POST", "/heartbeat", std::move(report));
            if (heartbeat.status == 200 && acknowledge_enrollment &&
                Request("POST", "/enrollment/ack").status == 204) {
                acknowledge_enrollment = !StoreValue("enrollment_v1", {});
            }
            next_heartbeat = Now() + kHeartbeatInterval;
        }
        if (now < next_state) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
            continue;
        }
        next_state = Now() + kStateInterval;
        if (pending.active()) {
            const auto response = Request("POST", "/actions/unbind",
                "{\"expected_permission_version\":" + std::to_string(pending.permission) +
                ",\"request_key\":\"" + pending.key.data() + "\"}");
            if (response.status != 200 && response.status != 409) {
                auto view = GetView();
                view.unbind_pending = true;
                Publish(std::move(view), {}, 0, permission, true, epoch);
                continue;
            }
            if (!StoreUnbind({})) {
                ESP_LOGW("SpeakerAccount", "Could not clear completed unbind request");
                continue;
            }
            pending = {};
            login_id.clear();
            retry_login = false;
        }
        const auto response = Request("GET", "/state");
        const auto document = Parse(response.body);
        if (response.status != 200 || !document || Number(document.get(), "protocol_version") != 2 ||
            Text(document.get(), "device_id", 128) != identity_.id) {
            if (response.status == 401 || response.status == 403) {
                Publish({Page::Disabled}, {}, 0, 0, true, epoch);
            } else if (!Voice()) {
                Publish({Page::Offline}, {}, 0, 0, true, epoch);
            }
            continue;
        }
        const auto status = Text(document.get(), "use_status", 48);
        const auto permission_version = Number(document.get(), "permission_version");
        const auto customer = Text(document.get(), "customer_name", 128);
        const bool bound = Text(document.get(), "binding_state") == "bound" && permission_version > 0;
        if (status == "ready") {
            const auto* info = cJSON_GetObjectItemCaseSensitive(document.get(), "voice");
            VoiceAccess access{Text(info, "url"), Text(info, "token"), 1};
            const auto server_time = Number(document.get(), "server_time_ms");
            const auto expiry = Number(info, "expires_at_ms");
            const auto lease = Number(document.get(), "lease_expires_in_ms");
            if (bound && server_time >= 0 && expiry > server_time && lease > 0 &&
                Number(info, "protocol_version") == 1 &&
                Number(info, "permission_version") == permission_version &&
                SecureUrl(access.url, "wss://") && Token(access.token, 43, 128, "_-")) {
                Publish({Page::Ready, {}, {}, customer, true}, std::move(access),
                        Now() + std::min({lease, expiry - server_time, kMaxLease}),
                        permission_version, true, epoch);
            } else {
                Publish({Page::Disabled}, {}, 0, permission_version, true, epoch);
            }
            login_id.clear();
            retry_login = false;
            continue;
        }
        if (status != "unbound" && status != "signed_out" && status != "authorization_required") {
            Publish({Page::Disabled, {}, {}, customer, bound}, {}, 0,
                    permission_version, true, epoch);
            login_id.clear();
            continue;
        }
        if (Voice()) {
            Publish({Page::Connecting, {}, {}, customer, bound}, {}, 0,
                    permission_version, true, epoch);
        }
        if (PollLogin(identity_, login_id, retry_login, customer, bound, permission_version, epoch) ==
            LoginResult::Completed) {
            next_state = 0;
        }
    }
}
