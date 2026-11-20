#pragma once

#include <cstdint>
#include <string>

struct EnvironmentSnapshot {
    bool configured = false;
    bool location_available = false;
    bool weather_available = false;
    bool air_available = false;
    bool stale = true;
    bool refresh_in_progress = false;

    std::string city;
    std::string adcode;
    std::string weather_text;
    std::string temperature_c;
    std::string humidity;
    std::string wind_direction;
    std::string wind_power;
    int aqi = -1;
    std::string air_category;
    int pm25 = -1;
    int pm10 = -1;
    int64_t updated_at = 0;
    std::string last_error;
    std::string location_status, location_source, city_id;
    int weather_code = -1;
    std::string weather_status, weather_valid_at;
    std::string air_status, air_valid_at, air_model;
    std::string pm25_text, pm10_text;
};
