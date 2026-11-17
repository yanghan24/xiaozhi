// Alternative DeviceControl worker, selected explicitly by firmware Kconfig.
// The public audio/UI boundary remains DeviceControl; no OAuth logic enters Application.
struct DeviceControl::Impl {
    std::mutex mutex;
    ProductControlModel model;
    std::function<void(const DeviceControlView&)> on_changed;
    std::function<void(uint64_t)> on_revoked;
    TaskHandle_t task = nullptr;
    esp_timer_handle_t timer = nullptr;
    bool timer_ready = false, configured = false, connected = false, refresh = true, dirty = true;
    bool stopping = false, account_mode = false, account_switch_pending = false;
    bool account_details_visible = false, maintenance_active = false;
    std::atomic<uint64_t> interrupt{0};
    std::string base, device_id, credential, operation_key;
    device_control::PendingAck pending_ack;
    device_control::PendingCreate pending_create;
    device_control::SignOutRequest sign_out;
    int64_t sign_out_retry_at = 0;
    std::function<std::string()> maintenance_request;
    std::function<void(const device_control::HttpResult&)> maintenance_response;
    Impl() {
        esp_timer_create_args_t args{};
        args.callback = [](void* p) { static_cast<Impl*>(p)->Timer(); };
        args.arg = this; args.name = "speaker_lease";
        if (esp_timer_create(&args, &timer) == ESP_OK)
            timer_ready = esp_timer_start_periodic(timer, 50000) == ESP_OK;
    }
    ~Impl() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        Wake(true);
        if (timer) { esp_timer_stop(timer); esp_timer_delete(timer); }
    }
    void Wake(bool cancel) {
        if (cancel) interrupt.fetch_add(1);
        TaskHandle_t handle;
        { std::lock_guard<std::mutex> lock(mutex); handle = task; }
        if (handle) xTaskNotifyGive(handle);
    }
    void Timer() {
        std::function<void(uint64_t)> revoke; uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto old = model.View().generation;
            if (model.Tick(Now())) dirty = true;
            if (old != model.View().generation) { revoke = on_revoked; generation = model.View().generation; }
        }
        if (revoke) revoke(generation);
    }
    void ResetAccount() {} // Account summary is part of the authenticated state snapshot.
    DeviceControlView View(int64_t now) const {
        auto view = model.View();
        if (!connected || !model.Allowed(now) || maintenance_active) view.voice_allowed = false;
        view.sign_out_pending = sign_out.Pending();
        if (view.sign_out_pending) { view.voice_allowed = false; view.reason = "signing_out"; }
        if (!connected || view.sign_out_pending || now >= view.qr_deadline_ms) {
            view.verification_uri.clear(); view.user_code.clear(); view.purpose.clear();
        }
        return view;
    }
    DeviceAccountView AccountView(int64_t now) const {
        DeviceAccountView account;
        if (!configured) return account;
        if (!connected) { account.status = DeviceAccountStatus::Offline; return account; }
        auto view = model.View(); account.permission_version = view.permission_version;
        account.sign_out = sign_out.Status();
        if (sign_out.Pending()) {
            account.permission_version = sign_out.Expected();
            account.can_sign_out = account.sign_out != DeviceSignOutStatus::Pending;
            return account;
        }
        account.can_scan = view.reason == "unbound" || view.reason == "user_signed_out" || view.reason == "market_auth_required";
        account.can_sign_out = view.permission_version > 0 && view.reason != "unbound" &&
            view.reason != "synchronizing" && view.reason != "not_configured" && view.reason != "device_auth_required";
        account.can_refresh = true;
        account.username = model.CustomerName(); account.balance = "未知";
        account.status = model.Allowed(now) ? DeviceAccountStatus::Ready :
            view.reason == "user_signed_out" ? DeviceAccountStatus::SignedOut : DeviceAccountStatus::NeedsLogin;
        return account;
    }
    void Publish() {
        std::function<void(const DeviceControlView&)> changed; DeviceControlView view;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!dirty) return;
            dirty = false; changed = on_changed; view = View(Now());
        }
        if (changed) changed(view);
    }
    void Run() {
        int64_t heartbeat_at = 0, state_at = 0, login_at = 0, maintenance_at = 0, retry_at = 0;
        int backoff = 1000;
        while (true) {
            Publish();
            std::string route, method = "GET", body, id, secret, origin;
            std::function<std::string()> maintenance_build;
            std::function<void(const device_control::HttpResult&)> maintenance_reply;
            bool begin = false, login = false, action = false, heartbeat = false, maintenance = false;
            const auto epoch = interrupt.load();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (stopping) break;
                const auto now = Now();
                if (configured && connected && now >= retry_at) {
                    if (sign_out.CanSend() && now >= sign_out_retry_at) {
                        route = "actions/unbind"; method = "POST"; action = true;
                        if (operation_key.empty()) operation_key = RandomKey();
                        body = "{\"expected_permission_version\":" + std::to_string(sign_out.Expected()) +
                            ",\"request_key\":\"" + operation_key + "\"}";
                    } else if (now >= heartbeat_at) {
                        route = "heartbeat"; method = "POST"; heartbeat = true;
                        auto object = cJSON_CreateObject();
                        cJSON_AddStringToObject(object,"board_type",BOARD_TYPE);
                        cJSON_AddStringToObject(object,"firmware_version",esp_app_get_description()->version);
                        char* encoded = cJSON_PrintUnformatted(object);
                        if (encoded) { body = encoded; cJSON_free(encoded); }
                        cJSON_Delete(object);
                        heartbeat_at = now + 20000;
                    } else if (refresh || now >= state_at) {
                        route = "state"; refresh = false; state_at = now + 5000;
                    } else if (!sign_out.Pending() && (account_switch_pending || model.CanLogin())) {
                        route = "login"; method = "POST"; begin = login = true;
                        account_switch_pending = false; login_at = now + 2000;
                    } else if (!sign_out.Pending() && !model.LoginId().empty() && now >= login_at) {
                        route = "login/" + model.LoginId(); login = true; login_at = now + 2000;
                    } else if (maintenance_request && now >= maintenance_at) {
                        route = "maintenance"; method = "POST"; maintenance = true;
                        maintenance_build = maintenance_request; maintenance_reply = maintenance_response;
                        maintenance_at = now + 20000;
                    }
                    id = device_id; secret = credential; origin = base;
                }
            }
            if (route.empty()) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250)); continue; }
            if (maintenance) {
                body = maintenance_build();
                if (body.empty()) continue;
                // Reuse the existing device/firmware maintenance API; no new OTA protocol.
                device_control::Url url; device_control::ParseUrl(origin,url);
                origin = url.Origin() + "/xiaozhi/device-control/v1/";
            }
            auto result = device_control::ControlRequest(origin + route, method, id, secret, body, "",
                [this,epoch]() { return interrupt.load() != epoch; });
            if (maintenance && maintenance_reply) maintenance_reply(result);
            if (result.failure == device_control::HttpFailure::Cancelled || interrupt.load() != epoch) continue;
            std::function<void(uint64_t)> revoked; uint64_t generation = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                const auto old = model.View().generation;
                bool retry = false;
                if (result.status == 401 || result.status == 403) {
                    model.Revoke("device_auth_required"); retry = true;
                    if (action) sign_out.Reject(DeviceSignOutStatus::Failed);
                } else if (result.failure == device_control::HttpFailure::Tls || result.failure == device_control::HttpFailure::Invalid) {
                    model.Revoke(result.failure == device_control::HttpFailure::Tls ? "tls_error" : "invalid_response"); retry = true;
                } else if (result.failure != device_control::HttpFailure::None || result.status >= 500 || result.status == 429) {
                    retry = true;
                } else if (maintenance) {
                    // Optional maintenance support cannot change ownership or voice state.
                } else if (action) {
                    if (result.status == 409 || result.status == 400) sign_out.Reject(DeviceSignOutStatus::Failed);
                    sign_out_retry_at = Now() + 3000; refresh = true;
                } else if (result.status == 200) {
                    if (!heartbeat) {
                        auto applied = login ? model.Login(result.body,Now(),result.elapsed_ms) : model.State(result.body,Now(),result.elapsed_ms);
                        if (applied == device_control::ApplyResult::Invalid) { model.Revoke("invalid_response"); retry = true; }
                        if (!login && applied == device_control::ApplyResult::Applied && sign_out.Observe(model.View())) operation_key.clear();
                        if (login) refresh = true;
                    }
                } else {
                    if (begin) account_switch_pending = false;
                    retry = true; refresh = true;
                }
                if (retry) {
                    int64_t delay = std::min(30000,backoff + static_cast<int>(esp_random() % 250));
                    if (result.status == 429) delay = std::max(delay,device_control::RetryDelayMs(result.retry_after,result.body,0,result.date));
                    retry_at = Now() + delay; backoff = std::min(30000,backoff * 2);
                } else { retry_at = 0; backoff = 1000; }
                dirty = true;
                if (old != model.View().generation) { revoked = on_revoked; generation = model.View().generation; }
            }
            if (revoked) revoked(generation);
        }
        { std::lock_guard<std::mutex> lock(mutex); task = nullptr; }
        vTaskDelete(nullptr);
    }
};
