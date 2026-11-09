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
#include "settings.h"

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
    bool pending_ack = false;
    uint64_t ack_revision = 0;
    uint64_t ack_permission = 0;
    bool ack_audio = false;
    std::string base;
    std::string device_id;
    std::string credential;
    int64_t rate_limit_until = 0;
    int64_t retry_until = 0;
    device_control::PendingCreate pending_create;

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
            view = model.View();
            cb = on_changed;
        }
        if (cb)
            cb(view);
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
            const auto next = model.View();
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
            bool create = false, ack = false, can_run = false;
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
                    if (!refresh && !need_lease && pending_ack) {
                        ack = true;
                        endpoint = "state-acks";
                        method = "POST";
                        sent_ack_revision = ack_revision;
                        sent_ack_permission = ack_permission;
                        body = "{\"revision\":" + std::to_string(ack_revision) +
                               ",\"permission_version\":" + std::to_string(ack_permission) +
                               ",\"audio_stopped\":" + (ack_audio ? "true" : "false") + "}";
                    } else if (!refresh && !need_lease && !intent.empty()) {
                        create = true;
                        endpoint = "requests";
                        method = "POST";
                        key = pending_create.Select(intent, view.permission_version, RandomKey());
                        body = "{\"intent\":\"" + intent + "\",\"expected_permission_version\":" +
                               std::to_string(view.permission_version) + "}";
                    } else {
                        int64_t wait = refresh ? 0 : 20000;
                        if (view.voice_allowed)
                            wait = std::min(wait, std::max<int64_t>(0, remaining - 30000));
                        if (!view.verification_uri.empty())
                            wait = std::min(
                                wait, std::max<int64_t>(0, view.qr_deadline_ms - Now() - 1000));
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
            if (result.failure == device_control::HttpFailure::Cancelled)
                continue;
            bool retry = false;
            if (result.failure == device_control::HttpFailure::Tls ||
                result.failure == device_control::HttpFailure::Invalid) {
                FailClosed(result.failure == device_control::HttpFailure::Tls ? "tls_error"
                                                                              : "invalid_response");
                retry = true;
            } else if (result.failure != device_control::HttpFailure::None) {
                retry = true;
            } else if (result.status == 401 || result.status == 403) {
                FailClosed("device_auth_required");
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
                        if (ack_revision == sent_ack_revision &&
                            ack_permission == sent_ack_permission)
                            pending_ack = false;
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
                if (ack)
                    pending_ack = false;
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
    Settings settings("device_ctrl", false);
    const auto id = settings.GetString("device_id"), credential = settings.GetString("credential");
    const bool valid = impl_->timer_ready && id.size() <= 128 && credential.size() <= 4096 &&
                       device_control::HeaderValue(id) && device_control::HeaderValue(credential) &&
                       device_control::TrustedDiscovery(ota, base, settings.GetString("ota_origin"),
                                                        settings.GetString("control_origin"));
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
            impl_->pending_ack = false;
            impl_->account_mode = false;
            impl_->account_switch_pending = false;
        }
        impl_->configured = valid;
        impl_->base = base;
        impl_->device_id = id;
        impl_->credential = credential;
        impl_->refresh = true;
        impl_->dirty = true;
        // A successfully authenticated discovery just traversed the network.
        if (valid)
            impl_->connected = true;
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
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->connected = connected;
        if (connected)
            impl_->refresh = true;
    }
    impl_->Wake(true);
}
bool DeviceControl::IsVoiceAllowed() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->configured && impl_->model.Allowed(Now());
}
bool DeviceControl::IsCurrentGeneration(uint64_t generation) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->configured && impl_->model.Allowed(Now()) &&
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
        allowed = impl_->configured && impl_->model.Voice(Now(), out);
    }
    if (!allowed)
        Refresh();
    return allowed;
}
DeviceControlView DeviceControl::GetView() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto view = impl_->model.View();
    if (!impl_->model.Allowed(Now()))
        view.voice_allowed = false;
    if (!view.verification_uri.empty() && Now() >= view.qr_deadline_ms) {
        view.verification_uri.clear();
        view.purpose.clear();
        view.qr_deadline_ms = 0;
    }
    return view;
}
void DeviceControl::Refresh() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->refresh = true;
    }
    impl_->Wake(true);
}
void DeviceControl::RequestAccount() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->model.View().purpose != "account")
            impl_->model.DiscardRequest();
        impl_->account_mode = true;
        impl_->account_switch_pending = true;
        impl_->refresh = true;
        impl_->dirty = true;
    }
    impl_->Wake(true);
}
void DeviceControl::AcknowledgeState(uint64_t revision, uint64_t pv, bool stopped) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto view = impl_->model.View();
        if (pv != view.permission_version || revision > view.revision ||
            (!view.voice_allowed && !stopped))
            return;
        if (impl_->pending_ack && impl_->ack_permission == pv && impl_->ack_audio && !stopped)
            return;
        impl_->ack_revision = revision;
        impl_->ack_permission = pv;
        impl_->ack_audio = stopped;
        impl_->pending_ack = true;
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
void DeviceControl::AcknowledgeState(uint64_t, uint64_t, bool) {}
#endif

DeviceControl& DeviceControl::GetInstance() {
    static DeviceControl instance;
    return instance;
}
