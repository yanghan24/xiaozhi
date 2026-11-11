#include "device_control.h"
#include "sdkconfig.h"

#if CONFIG_XIAOZHI_DEVICE_CONTROL
#include <esp_random.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <mutex>
#include <utility>
#include "control_model.h"
#include "control_transport.h"
#include "device_identity.h"

namespace {
int64_t Now() { return esp_timer_get_time() / 1000; }
std::string RandomKey() {
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    const char* hex = "0123456789abcdef";
    std::string result;
    for (auto b : bytes) {
        result += hex[b >> 4];
        result += hex[b & 15];
    }
    return result;
}
}  // namespace

struct DeviceControl::Impl {
    enum class AccountLoad { Empty, Pending, Loaded, Failed };
    std::mutex mutex;
    device_control::Model model{"", ""};
    std::function<void(const DeviceControlView&)> on_changed;
    std::function<void(uint64_t)> on_revoked;
    TaskHandle_t task = nullptr;
    esp_timer_handle_t timer = nullptr;
    bool timer_ready = false;
    std::atomic<uint64_t> interrupt{0};
    bool configured = false;
    bool connected = false;
    bool refresh = true;
    bool dirty = true;
    bool account_mode = false;
    bool account_switch_pending = false;
    bool stopping = false;
    device_control::PendingAck pending_ack;
    std::string base;
    std::string device_id;
    std::string credential;
    int64_t rate_limit_until = 0;
    int64_t retry_until = 0;
    device_control::PendingCreate pending_create;
    bool account_details_visible = false;
    uint64_t account_ticket = 0;
    AccountLoad account_load = AccountLoad::Empty;
    std::string account_username;
    std::string account_balance;
    device_control::SignOutRequest sign_out;
    int64_t sign_out_retry_at = 0;

    Impl() {
        esp_timer_create_args_t args{};
        args.callback = [](void* arg) { static_cast<Impl*>(arg)->Timer(); };
        args.arg = this;
        args.name = "device_lease";
        if (esp_timer_create(&args, &timer) == ESP_OK)
            timer_ready = esp_timer_start_periodic(timer, 50000) == ESP_OK;
    }
    ~Impl() {
        // Singleton lives for the firmware lifetime; keeping ownership explicit
        // also makes an orderly shutdown possible without killing an in-flight TLS task.
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        Wake(true);
        if (timer) {
            esp_timer_stop(timer);
            esp_timer_delete(timer);
        }
    }
    void Wake(bool cancel) {
        if (cancel)
            interrupt.fetch_add(1);
        TaskHandle_t handle;
        {
            std::lock_guard<std::mutex> lock(mutex);
            handle = task;
        }
        if (handle)
            xTaskNotifyGive(handle);
    }
    void Timer() {
        std::function<void(uint64_t)> revoked;
        uint64_t generation = 0;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto old = model.View();
            changed = model.Tick(Now());
            if (changed) {
                dirty = true;
                const auto next = model.View();
                if (old.generation != next.generation) {
                    revoked = on_revoked;
                    generation = next.generation;
                }
            }
        }
        if (changed)
            Wake(true);
        // Application installs a non-blocking revocation callback; no network,
        // module lock, UI lock or audio draining is performed on the timer task.
        if (revoked)
            revoked(generation);
    }
    void Publish() {
        DeviceControlView view;
        std::function<void(const DeviceControlView&)> cb;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!dirty)
                return;
            dirty = false;
            view = View(Now());
            cb = on_changed;
        }
        if (cb)
            cb(view);
    }
    // Called with mutex held. Local account-menu visibility never changes the
    // server's request or permission and cannot dismiss a blocked-state screen.
    DeviceControlView View(int64_t now) const {
        auto view = model.View();
        if (!model.Allowed(now))
            view.voice_allowed = false;
        view.sign_out_pending = sign_out.Pending();
        if (view.sign_out_pending) {
            view.voice_allowed = false;
            view.reason = "signing_out";
        }
        if (!connected || view.sign_out_pending ||
            (!view.verification_uri.empty() && now >= view.qr_deadline_ms) ||
            (view.voice_allowed && view.purpose == "account" && !account_mode)) {
            view.verification_uri.clear();
            view.purpose.clear();
            view.qr_deadline_ms = 0;
        }
        return view;
    }
    // Called with mutex held. A canceled account lookup may still finish; its
    // ticket can never publish details after the panel or login has changed.
    void ResetAccount() {
        ++account_ticket;
        account_username.clear();
        account_balance.clear();
        account_load = account_details_visible && configured && connected && !sign_out.Pending() &&
                               model.Allowed(Now())
                           ? AccountLoad::Pending
                           : AccountLoad::Empty;
    }
    DeviceAccountView AccountView(int64_t now) const {
        DeviceAccountView account;
        account.sign_out = sign_out.Status();
        if (!configured)
            return account;
        if (!connected) {
            account.status = DeviceAccountStatus::Offline;
            return account;
        }
        if (sign_out.Pending()) {
            account.permission_version = sign_out.Expected();
            account.can_sign_out = sign_out.Status() != DeviceSignOutStatus::Pending;
            return account;
        }

        const auto view = model.View();
        account.permission_version = view.permission_version;
        account.can_sign_out = view.permission_version > 0 &&
                               (view.reason == "ready" || view.reason == "user_signed_out" ||
                                view.reason == "market_auth_required" ||
                                view.reason == "market_auth_pending");
        if (!model.Allowed(now)) {
            account.can_scan = view.reason == "unbound";
            if (view.reason == "user_signed_out")
                account.status = DeviceAccountStatus::SignedOut;
            else if (account.can_scan || account.can_sign_out)
                account.status = DeviceAccountStatus::NeedsLogin;
            else
                account.status = DeviceAccountStatus::Synchronizing;
            return account;
        }
        account.permission_version = view.permission_version;
        account.can_sign_out = view.permission_version > 0;
        account.can_refresh = account_load != AccountLoad::Pending;
        if (account_load == AccountLoad::Loaded) {
            account.status = DeviceAccountStatus::Ready;
            account.username = account_username;
            account.balance = account_balance;
        } else {
            account.status = account_load == AccountLoad::Failed ? DeviceAccountStatus::Unavailable
                                                                 : DeviceAccountStatus::Loading;
        }
        return account;
    }
    void FailClosed(const std::string& reason) {
        std::function<void(uint64_t)> cb;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (model.Revoke(reason)) {
                cb = on_revoked;
                generation = model.View().generation;
            }
            ResetAccount();
            refresh = true;
            dirty = true;
        }
        if (cb)
            cb(generation);
    }
    device_control::ApplyResult Apply(const device_control::HttpResult& result, bool request) {
        std::function<void(uint64_t)> cb;
        uint64_t generation = 0;
        device_control::ApplyResult applied;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto old = model.View();
            applied = request ? model.ApplyRequest(result.body, Now(), result.elapsed_ms)
                              : model.ApplyState(result.body, Now(), result.elapsed_ms);
            auto next = model.View();
            bool sign_out_finished = false;
            if (!request && applied == device_control::ApplyResult::Applied) {
                pending_ack.Rebase(next);
                sign_out_finished = sign_out.Observe(next);
                if (sign_out_finished && next.voice_allowed && old.generation == next.generation) {
                    // Lifting the local stop after a rejected request requires a
                    // new audio generation even if server permission never changed.
                    model.AdvanceGeneration();
                    next = model.View();
                }
            }
            if (sign_out_finished || old.generation != next.generation)
                ResetAccount();
            if (!request && applied == device_control::ApplyResult::Applied && account_mode &&
                (old.permission_version != next.permission_version ||
                 (!old.verification_uri.empty() && old.purpose == "account" &&
                  Now() < old.qr_deadline_ms &&
                  (next.verification_uri.empty() ||
                   next.verification_uri != old.verification_uri)))) {
                account_mode = false;
                account_switch_pending = false;
            }
            if (applied != device_control::ApplyResult::Applied)
                refresh = true;
            if (old.generation != next.generation) {
                cb = on_revoked;
                generation = next.generation;
            }
            dirty = true;
        }
        if (cb)
            cb(generation);
        return applied;
    }
    void Run() {
        int backoff = 1000;
        while (true) {
            Publish();
            int64_t rate_wait;
            {
                std::lock_guard<std::mutex> lock(mutex);
                rate_wait = std::max(rate_limit_until, retry_until) - Now();
            }
            if (rate_wait > 0) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(std::min<int64_t>(1000, rate_wait)));
                continue;
            }
            std::string endpoint, method = "GET", body, key, id, secret, origin;
            bool create = false, ack = false, can_run = false, sent_ack_audio = false;
            bool immediate_state = false;
            uint64_t sent_account_ticket = 0, sent_account_permission = 0, sent_sign_out = 0;
            uint64_t sent_ack_revision = 0, sent_ack_permission = 0;
            const auto epoch = interrupt.load();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopping)
                    break;
                can_run = configured && connected;
                if (can_run) {
                    const auto view = model.View();
                    const int64_t remaining = model.LeaseDeadline() - Now();
                    const bool need_lease = view.voice_allowed && remaining <= 30000;
                    const auto intent =
                        account_mode && (account_switch_pending || !model.HasServerRequest())
                            ? std::string("account")
                            : model.AutoIntent();
                    if (!refresh && sign_out.CanSend() && Now() >= sign_out_retry_at) {
                        endpoint = "unbind";
                        method = "POST";
                        sent_sign_out = sign_out.Expected();
                        body = "{\"expected_permission_version\":" + std::to_string(sent_sign_out) +
                               "}";
                        refresh = true;
                    } else if (!refresh && !need_lease && pending_ack.Pending() &&
                               !sign_out.Pending()) {
                        ack = true;
                        endpoint = "state-acks";
                        method = "POST";
                        sent_ack_revision = pending_ack.Revision();
                        sent_ack_permission = pending_ack.Permission();
                        sent_ack_audio = pending_ack.AudioStopped();
                        body = "{\"revision\":" + std::to_string(sent_ack_revision) +
                               ",\"permission_version\":" + std::to_string(sent_ack_permission) +
                               ",\"audio_stopped\":" + (sent_ack_audio ? "true" : "false") + "}";
                    } else if (!refresh && !need_lease && !intent.empty() && !sign_out.Pending()) {
                        create = true;
                        endpoint = "requests";
                        method = "POST";
                        key = pending_create.Select(intent, view.permission_version, RandomKey());
                        body = "{\"intent\":\"" + intent + "\",\"expected_permission_version\":" +
                               std::to_string(view.permission_version) + "}";
                    } else if (!refresh && !need_lease && account_load == AccountLoad::Pending &&
                               !sign_out.Pending() && model.Allowed(Now())) {
                        endpoint = "account";
                        sent_account_ticket = account_ticket;
                        sent_account_permission = view.permission_version;
                    } else {
                        immediate_state = refresh;
                        int64_t wait = refresh ? 0 : 20000;
                        if (view.voice_allowed)
                            wait = std::min(wait, std::max<int64_t>(0, remaining - 30000));
                        if (!view.verification_uri.empty())
                            wait = std::min(
                                wait, std::max<int64_t>(0, view.qr_deadline_ms - Now() - 1000));
                        if (sign_out.CanSend())
                            wait = std::min(wait, std::max<int64_t>(0, sign_out_retry_at - Now()));
                        endpoint = "state";
                        if (view.revision)
                            endpoint += "?after_revision=" + std::to_string(view.revision) +
                                        "&wait_ms=" + std::to_string(wait);
                        refresh = false;
                    }
                    id = device_id;
                    secret = credential;
                    origin = base;
                }
            }
            if (!can_run) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
                continue;
            }
            auto result = device_control::ControlRequest(
                origin + endpoint, method, id, secret, body, key,
                [this, epoch]() { return interrupt.load() != epoch; });
            if (result.failure == device_control::HttpFailure::Cancelled ||
                interrupt.load() != epoch) {
                if (immediate_state) {
                    std::lock_guard<std::mutex> lock(mutex);
                    refresh = true;
                }
                continue;
            }
            // Menu endpoints share device authentication and the trusted TLS
            // channel. Revoke before their display/business handling can return.
            bool retry = false;
            if (result.failure == device_control::HttpFailure::Tls ||
                result.failure == device_control::HttpFailure::Invalid) {
                FailClosed(result.failure == device_control::HttpFailure::Tls ? "tls_error"
                                                                              : "invalid_response");
                retry = true;
            } else if (result.failure == device_control::HttpFailure::None &&
                       (result.status == 401 || result.status == 403)) {
                FailClosed("device_auth_required");
                if (sent_sign_out != 0) {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (sign_out.Expected() == sent_sign_out)
                        sign_out.Reject(DeviceSignOutStatus::Failed);
                }
                retry = true;
            } else if (sent_account_permission != 0) {
                std::string username, balance;
                const bool valid = result.failure == device_control::HttpFailure::None &&
                                   result.status == 200 &&
                                   device_control::ParseAccountDetails(
                                       result.body, id, sent_account_permission, username, balance);
                std::lock_guard<std::mutex> lock(mutex);
                if (sent_account_ticket == account_ticket && account_load == AccountLoad::Pending &&
                    model.Allowed(Now()) &&
                    model.View().permission_version == sent_account_permission) {
                    account_load = valid ? AccountLoad::Loaded : AccountLoad::Failed;
                    account_username = std::move(username);
                    account_balance = std::move(balance);
                    if (result.status == 409)
                        refresh = true;
                }
                continue;
            } else if (sent_sign_out != 0) {
                const bool ok = result.failure == device_control::HttpFailure::None &&
                                result.status == 200 &&
                                device_control::ValidateSignOutResponse(result.body, sent_sign_out);
                std::lock_guard<std::mutex> lock(mutex);
                if (sign_out.Expected() == sent_sign_out && connected && device_id == id &&
                    interrupt.load() == epoch) {
                    if (result.failure == device_control::HttpFailure::None) {
                        if (result.status == 404 || result.status == 405 || result.status == 501)
                            sign_out.Reject(DeviceSignOutStatus::Unsupported);
                        else if (result.status == 400 || result.status == 409 ||
                                 result.status == 410)
                            sign_out.Reject(DeviceSignOutStatus::Failed);
                    }
                    // Confirm success with /state, including when the response
                    // was lost. Never retry using a newer permission version.
                    sign_out_retry_at = Now() + (ok ? 1000 : 3000);
                    if (result.status == 429)
                        sign_out_retry_at =
                            std::max(sign_out_retry_at,
                                     Now() + device_control::RetryDelayMs(
                                                 result.retry_after, result.body,
                                                 model.EstimatedServerTime(Now()), result.date));
                    refresh = true;
                }
                continue;
            } else if (result.failure != device_control::HttpFailure::None) {
                retry = true;
            } else if (result.status == 429) {
                int64_t unix_ms;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    unix_ms = model.EstimatedServerTime(Now());
                }
                if (unix_ms == 0 && std::time(nullptr) >= 1577836800)
                    unix_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
                const auto delay = device_control::RetryDelayMs(result.retry_after, result.body,
                                                                unix_ms, result.date);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    rate_limit_until = Now() + std::max<int64_t>(1000, delay);
                }
                retry = true;
            } else if (result.status >= 500) {
                retry = true;
            } else if (result.status == 200) {
                if (ack) {
                    if (!device_control::ValidateAckResponse(result.body, sent_ack_revision,
                                                             sent_ack_permission)) {
                        FailClosed("invalid_response");
                        retry = true;
                    } else {
                        std::lock_guard<std::mutex> lock(mutex);
                        pending_ack.Confirm(sent_ack_revision, sent_ack_permission, sent_ack_audio);
                        backoff = 1000;
                    }
                } else {
                    const auto applied = Apply(result, create);
                    retry = applied != device_control::ApplyResult::Applied;
                    if (!retry)
                        backoff = 1000;
                    if (create && !retry) {
                        std::lock_guard<std::mutex> lock(mutex);
                        pending_create.Complete(200);
                        refresh = true;
                        account_switch_pending = false;
                    }
                }
            } else if (result.status == 409 || result.status == 410) {
                std::lock_guard<std::mutex> lock(mutex);
                if (create) {
                    pending_create.Complete(result.status);
                    model.DiscardRequest();
                    dirty = true;
                }
                // A QR-only revision may race the ACK. Keep its applied permission and
                // rebase after the authoritative snapshot, without claiming a new stop.
                refresh = true;
            } else {
                FailClosed("invalid_response");
                retry = true;
            }
            Publish();
            if (retry) {
                const int jitter = esp_random() % std::max(1, backoff / 4);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    retry_until = Now() + std::min(30000, backoff + jitter);
                }
                backoff = std::min(30000, backoff * 2);
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            task = nullptr;
        }
        vTaskDelete(nullptr);
    }
};

DeviceControl::DeviceControl() : impl_(std::make_unique<Impl>()) {}
DeviceControl::~DeviceControl() = default;
bool DeviceControl::Enabled() const { return true; }
void DeviceControl::SetCallbacks(std::function<void(const DeviceControlView&)> changed,
                                 std::function<void(uint64_t)> revoked) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->on_changed = std::move(changed);
        impl_->on_revoked = std::move(revoked);
        impl_->dirty = true;
    }
    impl_->Wake(false);
}
void DeviceControl::Start(const std::string& ota, const std::string& base) {
    device_control::IdentityConfig identity;
    const bool loaded = device_control::LoadDeviceIdentity(identity);
    const auto& id = identity.device_id;
    const auto& credential = identity.credential;
    const bool valid =
        loaded && impl_->timer_ready &&
        device_control::TrustedDiscovery(ota, base, identity.ota_origin, identity.control_origin);
    impl_->Wake(true);
    std::function<void(uint64_t)> revoked;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const bool same = impl_->configured && valid && impl_->base == base &&
                          impl_->device_id == id && impl_->credential == credential;
        if (!same) {
            if (impl_->model.Revoke(valid ? "synchronizing" : "not_configured")) {
                revoked = impl_->on_revoked;
                generation = impl_->model.View().generation;
            }
            if (valid) {
                device_control::Url parsed;
                device_control::ParseUrl(base, parsed);
                const auto old_generation = impl_->model.View().generation;
                impl_->model = device_control::Model(id, parsed.Origin());
                // generation must never be reused after reconfiguration.
                impl_->model.AdvanceGeneration(old_generation + 1);
            }
            impl_->pending_create.Clear();
            impl_->pending_ack.Clear();
            impl_->account_mode = false;
            impl_->account_switch_pending = false;
            impl_->sign_out = {};
        }
        impl_->configured = valid;
        impl_->base = base;
        impl_->device_id = id;
        impl_->credential = credential;
        impl_->refresh = true;
        impl_->dirty = true;
        impl_->ResetAccount();
        if (!impl_->task) {
            if (xTaskCreate([](void* p) { static_cast<Impl*>(p)->Run(); }, "device_control", 8192,
                            impl_.get(), 4, &impl_->task) != pdPASS) {
                impl_->configured = false;
                impl_->model.Revoke("control_unavailable");
            }
        }
    }
    if (revoked)
        revoked(generation);
    impl_->Wake(true);
}
void DeviceControl::NotifyNetwork(bool connected) {
    std::function<void(uint64_t)> revoked;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->connected == connected)
            return;
        // Reconnect must obtain a fresh state before opening a new voice
        // connection, even when the lease had time left at disconnection.
        if (connected && impl_->model.Revoke("synchronizing")) {
            revoked = impl_->on_revoked;
            generation = impl_->model.View().generation;
        }
        impl_->connected = connected;
        impl_->refresh = true;
        impl_->dirty = true;
        impl_->ResetAccount();
    }
    if (revoked)
        revoked(generation);
    impl_->Wake(true);
}
bool DeviceControl::IsVoiceAllowed() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->configured && !impl_->sign_out.Pending() && impl_->model.Allowed(Now());
}
bool DeviceControl::IsCurrentGeneration(uint64_t generation) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->configured && !impl_->sign_out.Pending() && impl_->model.Allowed(Now()) &&
           impl_->model.View().generation == generation;
}
uint64_t DeviceControl::GetGeneration() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->model.View().generation;
}
bool DeviceControl::GetVoiceConfig(DeviceVoiceConfig& out) {
    bool allowed;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        allowed = impl_->configured && !impl_->sign_out.Pending() && impl_->model.Voice(Now(), out);
    }
    if (!allowed)
        Refresh();
    return allowed;
}
DeviceControlView DeviceControl::GetView() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->View(Now());
}
void DeviceControl::Refresh() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->sign_out.Pending())
            return;
        impl_->refresh = true;
    }
    impl_->Wake(true);
}
void DeviceControl::RequestAccount() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->sign_out.Pending())
            return;
        if (impl_->model.View().purpose != "account")
            impl_->model.DiscardRequest();
        impl_->account_mode = true;
        impl_->account_switch_pending = true;
        impl_->refresh = true;
        impl_->dirty = true;
    }
    impl_->Wake(true);
}
void DeviceControl::CloseAccount() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->account_mode = false;
        impl_->account_switch_pending = false;
        impl_->pending_create.Clear();
        impl_->refresh = true;
        impl_->dirty = true;
    }
    impl_->Wake(true);
}
DeviceAccountView DeviceControl::GetAccountView() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->AccountView(Now());
}
void DeviceControl::ShowAccountDetails(bool visible) {
    bool fetch;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->account_details_visible == visible)
            return;
        impl_->account_details_visible = visible;
        impl_->ResetAccount();
        fetch = impl_->account_load == Impl::AccountLoad::Pending;
    }
    if (fetch)
        impl_->Wake(true);
}
void DeviceControl::RefreshAccountDetails() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->account_details_visible || !impl_->AccountView(Now()).can_refresh)
            return;
        impl_->ResetAccount();
    }
    impl_->Wake(true);
}
bool DeviceControl::SignOut(uint64_t expected) {
    std::function<void(uint64_t)> revoked;
    uint64_t generation;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto view = impl_->AccountView(Now());
        if (!view.can_sign_out || view.permission_version != expected ||
            !impl_->sign_out.Begin(expected))
            return false;
        impl_->model.AdvanceGeneration();
        generation = impl_->model.View().generation;
        revoked = impl_->on_revoked;
        impl_->sign_out_retry_at = 0;
        impl_->account_mode = false;
        impl_->account_switch_pending = false;
        impl_->pending_create.Clear();
        impl_->ResetAccount();
        impl_->refresh = true;
        impl_->dirty = true;
    }
    if (revoked)
        revoked(generation);
    impl_->Wake(true);
    return true;
}
void DeviceControl::AcknowledgeState(uint64_t revision, uint64_t pv, bool stopped) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto view = impl_->model.View();
        if (impl_->sign_out.Pending() || pv != view.permission_version ||
            revision > view.revision || (!view.voice_allowed && !stopped))
            return;
        if (impl_->pending_ack.Pending() && impl_->pending_ack.Permission() == pv &&
            impl_->pending_ack.AudioStopped() && !stopped)
            return;
        impl_->pending_ack.Set(revision, pv, stopped);
    }
    impl_->Wake(true);
}

#else
struct DeviceControl::Impl {};
DeviceControl::DeviceControl() : impl_(std::make_unique<Impl>()) {}
DeviceControl::~DeviceControl() = default;
bool DeviceControl::Enabled() const { return false; }
void DeviceControl::SetCallbacks(std::function<void(const DeviceControlView&)>,
                                 std::function<void(uint64_t)>) {}
void DeviceControl::Start(const std::string&, const std::string&) {}
void DeviceControl::NotifyNetwork(bool) {}
bool DeviceControl::IsVoiceAllowed() const { return true; }
bool DeviceControl::IsCurrentGeneration(uint64_t) const { return true; }
uint64_t DeviceControl::GetGeneration() const { return 0; }
bool DeviceControl::GetVoiceConfig(DeviceVoiceConfig&) { return false; }
DeviceControlView DeviceControl::GetView() const { return {}; }
void DeviceControl::Refresh() {}
void DeviceControl::RequestAccount() {}
void DeviceControl::CloseAccount() {}
DeviceAccountView DeviceControl::GetAccountView() const { return {}; }
void DeviceControl::ShowAccountDetails(bool) {}
void DeviceControl::RefreshAccountDetails() {}
bool DeviceControl::SignOut(uint64_t) { return false; }
void DeviceControl::AcknowledgeState(uint64_t, uint64_t, bool) {}
#endif

DeviceControl& DeviceControl::GetInstance() {
    static DeviceControl instance;
    return instance;
}
