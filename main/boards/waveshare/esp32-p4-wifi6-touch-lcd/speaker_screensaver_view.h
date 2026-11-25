#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <lvgl.h>

#include <array>
#include <ctime>

#include "speaker_standby_weather.h"

class SpeakerScreensaverView {
public:
    void Create(lv_obj_t* parent, lv_event_cb_t on_exit, void* context);
    void SetVisible(bool visible);
    bool Visible() const;
    void Update(const std::tm* local_time, const SpeakerStandbyWeather::Snapshot& weather,
                const char* wifi);

private:
    void CreateClock();
    void CreateWeather();
    void UpdateClock(const std::tm* local_time);
    void UpdateWeather(const SpeakerStandbyWeather::Snapshot& weather);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* city_ = nullptr;
    lv_obj_t* wifi_ = nullptr;
    lv_obj_t* seconds_ = nullptr;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* sun_ = nullptr;
    lv_obj_t* cloud_ = nullptr;
    lv_obj_t* rain_ = nullptr;
    lv_obj_t* temperature_ = nullptr;
    lv_obj_t* condition_ = nullptr;
    lv_obj_t* feels_like_ = nullptr;
    lv_obj_t* humidity_ = nullptr;
    lv_obj_t* wind_direction_ = nullptr;
    lv_obj_t* wind_power_ = nullptr;
    lv_obj_t* air_category_ = nullptr;
    lv_obj_t* air_value_ = nullptr;
    std::array<std::array<lv_obj_t*, 7>, 4> segments_{};
    std::array<uint8_t, 4> masks_{};
    SpeakerStandbyWeather::Snapshot last_weather_;
};

#endif
