#include "product_weather.h"
#include <cassert>
#include <string>

int main() {
    std::string candidates;
    EnvironmentSnapshot view;
    auto parse = [&](const std::string& data) {
        return product_weather::Parse("{\"version\":1,\"code\":\"OK\",\"data\":" + data + "}",
                                      view, candidates);
    };
    const std::string found = R"({"location_status":"found","location_source":"manual","city":{"id":"123","name":"上海"},)";
    assert(parse(found + R"("weather":{"status":"fresh","valid_at":"2026-11-20T10:00:00+00:00","temperature_c":-2.5,"humidity_pct":0,"weather_code":0,"description":"晴"},"air_quality":{"status":"stale","valid_at":"2026-11-20T09:00:00+00:00","model":"cams","model_estimate":true,"pm25":null,"pm10":0,"unit":"μg/m³"}})"));
    assert(view.location_available && view.city_id == "123" && view.city == "上海");
    assert(view.weather_available && view.temperature_c == "-2.5" && view.humidity == "0");
    assert(view.weather_code == 0 && view.weather_text == "晴");
    assert(view.air_available && view.pm25_text.empty() && view.pm10_text == "0");
    assert(view.weather_status == "fresh" && view.air_status == "stale" && view.stale);
    product_weather::MarkCached(view);
    assert(view.weather_status == "stale" && view.weather_valid_at == "2026-11-20T10:00:00+00:00");
    assert(parse(found + R"("weather":{"status":"unavailable","temperature_c":25},"air_quality":{"status":"loading","pm25":25}})"));
    assert(!view.weather_available && view.temperature_c.empty() && !view.air_available);
    assert(parse(found + R"("weather":{"status":"fresh","temperature_c":25},"air_quality":{"status":"fresh","model_estimate":false,"pm25":25}})"));
    assert(!view.weather_available && view.temperature_c.empty() && !view.air_available);
    assert(parse(R"({"location_status":"ambiguous","candidates":[{"id":"123","name":"朝阳","province":"辽宁"}]})"));
    assert(!view.location_available && candidates == "辽宁 朝阳 [123]");
    assert(parse(R"({"location_status":"unknown","weather":null,"air_quality":null})"));
    assert(!view.weather_available && view.city.empty() && candidates.empty());
    assert(!parse(R"({"location_status":"found","city":{"id":"1&city=bad","name":"Other"}})"));
    assert(!product_weather::Parse(R"({"version":2,"code":"OK","data":{}})", view, candidates));
    std::string old = R"({"version":1,"code":"OK","data":)" + found +
        R"("weather":{"status":"fresh","valid_at":"old","temperature_c":5},"air_quality":{"status":"unavailable"}}})";
    std::string update = R"({"version":1,"code":"OK","data":)" + found +
        R"("weather":{"status":"loading"},"air_quality":{"status":"fresh","valid_at":"now","model":"CAMS","model_estimate":true,"pm25":4,"unit":"μg/m³"}}})";
    auto other_city = update;
    other_city.replace(other_city.find("123"), 3, "456");
    product_weather::RetainUnavailable(other_city, old);
    assert(product_weather::Parse(other_city, view, candidates) && !view.weather_available);
    product_weather::RetainUnavailable(update, old);
    assert(product_weather::Parse(update, view, candidates));
    assert(view.temperature_c == "5" && view.weather_valid_at == "old" && view.weather_status == "stale");
    assert(view.pm25_text == "4" && view.air_status == "fresh");
    assert(product_weather::CityQuery("西安&", "陕西") ==
           "?city=%E8%A5%BF%E5%AE%89%26&province=%E9%99%95%E8%A5%BF");
}
