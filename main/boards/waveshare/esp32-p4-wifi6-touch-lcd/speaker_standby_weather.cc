#include "speaker_standby_weather.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "application.h"
#include "speaker_account/speaker_account.h"
#include <cJSON.h>
#include <esp_timer.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

namespace {
constexpr int64_t kMinuteUs = 60LL * 1000 * 1000;

std::string Text(cJSON* json, const char* key) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) && std::strlen(item->valuestring) <= 192 ? item->valuestring : "";
}

std::optional<double> ReadFeelsLike(const cJSON* json) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(json, "feelsLike");
    double value;
    if (cJSON_IsNumber(item)) {
        value = item->valuedouble;
    } else if (cJSON_IsString(item)) {
        const std::string_view text(item->valuestring);
        if (text.empty() || text.size() > 32 ||
            text.find_first_not_of("+-0123456789.eE") != std::string_view::npos) {
            return std::nullopt;
        }
        char* end;
        value = std::strtod(text.data(), &end);
        if (end != text.data() + text.size()) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
}
}  // namespace

SpeakerStandbyWeather& SpeakerStandbyWeather::Get() {
    static SpeakerStandbyWeather weather;
    return weather;
}

void SpeakerStandbyWeather::Start() {
    if (task_ == nullptr) {
        xTaskCreate([](void* context) { static_cast<SpeakerStandbyWeather*>(context)->Run(); },
                    "standby_weather", 6144, this, 1, &task_);
    }
}

SpeakerStandbyWeather::Snapshot SpeakerStandbyWeather::GetSnapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

void SpeakerStandbyWeather::Run() {
    auto& account = SpeakerAccount::Get();
    int64_t next = 0;
    bool was_ready = false;
    for (;;) {
        const auto account_view = account.GetView();
        const bool ready = account_view.standby_weather_allowed;
        if (!ready) {
            std::lock_guard lock(mutex_);
            snapshot_ = {};
            snapshot_.state = account_view.page == SpeakerAccount::Page::Offline
                ? State::Offline : State::Unavailable;
        } else if ((!was_ready || esp_timer_get_time() >= next) &&
                   Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            if (next == 0) {
                std::lock_guard lock(mutex_);
                snapshot_.state = State::Loading;
            }
            auto response = account.RequestWithIdentity(account.identity(), "GET", "/weather", {}, 6, 8000);
            std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(
                response.status == 200 ? cJSON_Parse(response.body.c_str()) : nullptr, cJSON_Delete);
            Snapshot data;
            data.state = State::Unavailable;
            if (Text(json.get(), "status") == "ready") {
                data.state = State::Ready;
                data.city = Text(json.get(), "city");
                data.condition = Text(json.get(), "condition");
                data.temperature = Text(json.get(), "temperature");
                data.feels_like = ReadFeelsLike(json.get());
                data.humidity = Text(json.get(), "humidity");
                data.wind_direction = Text(json.get(), "wind_direction");
                data.wind_power = Text(json.get(), "wind_power");
                const auto* aqi = cJSON_GetObjectItemCaseSensitive(json.get(), "aqi");
                if (cJSON_IsNumber(aqi) && aqi->valuedouble >= 0 && aqi->valuedouble <= 500 &&
                    aqi->valueint == aqi->valuedouble) {
                    data.aqi = aqi->valueint;
                }
            }
            next = esp_timer_get_time() + (data.state == State::Ready ? 20 : 1) * kMinuteUs;
            if (account.GetView().standby_weather_allowed) {
                std::lock_guard lock(mutex_);
                snapshot_ = std::move(data);
            }
        }
        if (!ready) next = 0;
        was_ready = ready;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

#endif
