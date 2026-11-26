#include "speaker_recharge.h"

#include "application.h"
#include "speaker_account.h"
#include <algorithm>
#include <cJSON.h>
#include <esp_timer.h>
#include <memory>
#include <string_view>

namespace {

constexpr const char* kEntries = "/account/recharge/entries";
constexpr const char* kConsent = "/account/recharge/authorization";
constexpr std::string_view kMarket = "https://aimarket.hncig.cn/";
constexpr std::string_view kCheckout = "https://aimarket.hncig.cn/application/recharge#ticket=";

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

Json Parse(const std::string& body) {
    return Json(cJSON_ParseWithOpts(body.c_str(), nullptr, true), cJSON_Delete);
}

std::string Text(const cJSON* json, const char* key, size_t limit = 512) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsString(value) || !value->valuestring) return {};
    std::string_view text(value->valuestring);
    return text.size() <= limit ? std::string(text) : std::string();
}

int Milliseconds(const cJSON* body, const char* key, int minimum, int maximum) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(body, key);
    if (!cJSON_IsNumber(value) || !(value->valuedouble >= minimum && value->valuedouble <= maximum)) return 0;
    return value->valueint * 1000;
}

bool Id(std::string_view text) {
    return text.size() == 36 && std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
    });
}

bool CheckoutUrl(std::string_view text) {
    if (!text.starts_with(kCheckout) || text.size() != kCheckout.size() + 43) return false;
    text.remove_prefix(kCheckout.size());
    return text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == text.npos;
}

int64_t NowMs() { return esp_timer_get_time() / 1000; }

}  // namespace

SpeakerRecharge& SpeakerRecharge::Get() {
    static SpeakerRecharge recharge;
    return recharge;
}

SpeakerRecharge::View SpeakerRecharge::GetView() const {
    std::lock_guard lock(mutex_);
    return view_;
}

void SpeakerRecharge::Open() {
    std::lock_guard lock(mutex_);
    ++generation_;
    view_ = {Page::Loading, {}, {}, "正在准备充值"};
    if (worker_) {
        xTaskNotifyGive(worker_);
        return;
    }
    if (xTaskCreate([](void* arg) {
            static_cast<SpeakerRecharge*>(arg)->Run();
            vTaskDelete(nullptr);
        }, "speaker_recharge", 6144, this, 1, &worker_) != pdPASS) {
        worker_ = nullptr;
        view_ = {Page::Failed, {}, {}, "暂时无法打开充值，请重试"};
    }
}

void SpeakerRecharge::Close() {
    std::lock_guard lock(mutex_);
    ++generation_;
    view_ = {};
    if (worker_) xTaskNotifyGive(worker_);
}

bool SpeakerRecharge::Current(uint32_t generation, int64_t permission) const {
    const auto account = SpeakerAccount::Get().GetView();
    std::lock_guard lock(mutex_);
    return generation == generation_ && account.recharge_available && account.balance_permission == permission;
}

bool SpeakerRecharge::Publish(uint32_t generation, View view) {
    {
        std::lock_guard lock(mutex_);
        if (generation != generation_) return false;
        if (view == view_) return true;
        view_ = std::move(view);
    }
    Application::GetInstance().Schedule([] { RenderSpeakerRecharge(); });
    return true;
}

void SpeakerRecharge::Run() {
    for (;;) {
        uint32_t generation;
        {
            std::lock_guard lock(mutex_);
            if (view_.page == Page::Inactive) {
                worker_ = nullptr;
                return;
            }
            generation = generation_;
        }
        Checkout(generation, SpeakerAccount::Get().GetView().balance_permission);
        {
            std::lock_guard lock(mutex_);
            if (generation == generation_) {
                worker_ = nullptr;
                return;
            }
        }
    }
}

void SpeakerRecharge::Checkout(uint32_t generation, int64_t permission) {
    auto& account = SpeakerAccount::Get();
    auto identity = account.identity();
    const auto active = [&] { return permission > 0 && Current(generation, permission); };
    const auto request = [&](const char* method, const std::string& path) {
        return account.RequestWithIdentity(identity, method, path, {}, 8, 9000);
    };
    const auto fail = [&](const char* text) {
        if (active()) Publish(generation, {Page::Failed, {}, {}, text});
    };
    if (!active()) return;
    auto response = request("POST", kEntries);
    if (!active()) return;
    auto result = Parse(response.body);
    if (response.status == 409 && Text(result.get(), "code") == "RECHARGE_AUTHORIZATION_REQUIRED") {
        response = request("POST", kConsent);
        if (!active()) return;
        result = Parse(response.body);
        const auto transaction = Text(result.get(), "id", 36);
        if (response.status != 200 || !Id(transaction)) {
            fail("暂时无法申请充值授权，请重试");
            return;
        }
        const auto timeout = NowMs() + 600000;
        while (active() && NowMs() < timeout) {
            const auto status = Text(result.get(), "status", 32);
            if (status == "completed") break;
            if (status == "denied" || status == "expired" || status == "failed" || status == "cancelled") {
                fail(Text(result.get(), "error_code") == "OWNER_REQUIRED" ? "请使用当前绑定的集市账号授权" : "充值授权未完成，请重试");
                return;
            }
            auto url = Text(result.get(), "verification_uri");
            auto code = Text(result.get(), "user_code", 32);
            if (url.starts_with(kMarket) && !code.empty()) {
                Publish(generation, {Page::Consent, std::move(url), std::move(code), "请用原集市账号扫码授权"});
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
            if (!active()) return;
            response = request("GET", "/login/" + transaction);
            result = response.status == 200 ? Parse(response.body) : Json(nullptr, cJSON_Delete);
        }
        if (!active()) return;
        if (Text(result.get(), "status") != "completed") {
            fail("充值授权已超时，请重试");
            return;
        }
        response = request("POST", kEntries);
        result = Parse(response.body);
    }
    if (!active()) return;
    auto url = Text(result.get(), "url");
    const auto entry = Text(result.get(), "entry_id", 36);
    const auto expires = Milliseconds(result.get(), "expires_in", 1, 300);
    const auto poll = Milliseconds(result.get(), "poll_interval", 2, 30);
    if (response.status != 200 || !CheckoutUrl(url) || !Id(entry) || !expires || !poll) {
        fail("暂时无法打开充值，请稍后重试");
        return;
    }
    Publish(generation, {Page::Checkout, std::move(url), {}, "请用微信扫一扫"});
    const auto qr_deadline = NowMs() + expires;
    auto wait_deadline = qr_deadline + 30 * 60 * 1000;
    bool pending = false;
    while (active() && NowMs() < wait_deadline) {
        const int interval = !pending && NowMs() >= qr_deadline ? std::max(poll, 5000) : poll;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval));
        if (!active()) return;
        if (!pending && NowMs() >= qr_deadline) {
            Publish(generation, {Page::Waiting, {}, {}, "二维码已过期，已打开的手机页面可继续付款"});
        }
        response = request("GET", std::string(kEntries) + "/" + entry);
        if (!active()) return;
        if (response.status == 404) {
            fail("充值入口已失效，请重新获取");
            return;
        }
        if (response.status == 403 || response.status == 409) {
            fail("暂时无法核对到账，请在集市查看支付结果");
            return;
        }
        if (response.status != 200) continue;
        result = Parse(response.body);
        const auto status = Text(result.get(), "status", 32);
        if (status == "paid") {
            Publish(generation, {Page::Paid, {}, {}, "充值成功"});
            account.RefreshBalance();
            return;
        }
        if (status == "closed" || status == "check_later") {
            fail(status == "closed" ? "订单已关闭" : "暂未确认到账，请在集市查看支付结果");
            return;
        }
        if (status == "pending" && !pending) {
            pending = true;
            wait_deadline = NowMs() + 12 * 60 * 1000;
            Publish(generation, {Page::Pending, {}, {}, "等待集市确认付款"});
        }
    }
    fail("已结束等待，请在集市查看支付结果");
}
