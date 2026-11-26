#include "speaker_screensaver_view.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "speaker_account/speaker_ui_widgets.h"

namespace {
using SpeakerUi::Icon;
using SpeakerUi::Label;
using SpeakerUi::Scale;
using SpeakerUi::Surface;
using SpeakerUi::UpdateLabel;

// The standby layout follows firmware/p4's StandbyDashboard, without its app controls.
constexpr uint32_t kBackground = 0x050A14;
constexpr uint32_t kBackgroundDeep = 0x071525;
constexpr uint32_t kPanel = 0x061426;
constexpr uint32_t kWeatherPanel = 0x081B31;
constexpr uint32_t kCyan = 0x27E5FF;
constexpr uint32_t kBlue = 0x3487FF;
constexpr uint32_t kViolet = 0x8A6CFF;
constexpr uint32_t kText = 0xF4F8FF;
constexpr uint32_t kMuted = 0xA8B6CB;
constexpr uint32_t kGrid = 0x0C3553;
constexpr uint32_t kSegmentOff = 0x0A3145;
constexpr uint32_t kSun = 0xFFC21A;
constexpr int kWindColumnWidth = 110;
constexpr int kWindColumnCenterX = 289;
constexpr int kWindTitleY = 199;

lv_obj_t* Block(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opacity,
                int radius = 0) {
    auto* block = Surface(parent, x, y, w, h);
    lv_obj_set_style_bg_color(block, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(block, opacity, LV_PART_MAIN);
    lv_obj_set_style_radius(block, radius, LV_PART_MAIN);
    return block;
}

lv_obj_t* Frame(lv_obj_t* parent, int x, int y, int w, int h, uint32_t fill, lv_opa_t opacity,
                uint32_t border, lv_opa_t border_opacity, int width = 1, int radius = 12) {
    auto* frame = Block(parent, x, y, w, h, fill, opacity, radius);
    lv_obj_set_style_border_color(frame, lv_color_hex(border), LV_PART_MAIN);
    lv_obj_set_style_border_width(frame, width, LV_PART_MAIN);
    lv_obj_set_style_border_opa(frame, border_opacity, LV_PART_MAIN);
    return frame;
}

lv_obj_t* CreateSun(lv_obj_t* parent) {
    // Draw the sun with local LVGL shapes so it stays centered without font bearings.
    auto* sun = Surface(parent, 35, 43, 64, 64);
    Frame(sun, 18, 18, 28, 28, kSun, LV_OPA_TRANSP, kSun, LV_OPA_COVER, 3, LV_RADIUS_CIRCLE);
    static constexpr lv_point_precise_t rays[][2] = {
        {{32, 2}, {32, 10}},  {{48, 16}, {53, 11}}, {{54, 32}, {62, 32}}, {{48, 48}, {53, 53}},
        {{32, 54}, {32, 62}}, {{16, 48}, {11, 53}}, {{2, 32}, {10, 32}},  {{11, 11}, {16, 16}},
    };
    for (const auto& points : rays) {
        auto* ray = lv_line_create(sun);
        lv_obj_remove_style_all(ray);
        lv_line_set_points(ray, points, 2);
        lv_obj_set_style_line_color(ray, lv_color_hex(kSun), LV_PART_MAIN);
        lv_obj_set_style_line_width(ray, 3, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(ray, true, LV_PART_MAIN);
    }
    return sun;
}

lv_obj_t* Centered(lv_obj_t* parent, const char* text, int x, int y, int width,
                   uint32_t color = kText) {
    auto* label = Label(parent, text, color);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

std::string WithUnit(const std::string& value, const char* unit, const char* fallback = "--") {
    if (value.empty()) {
        return fallback;
    }
    return value.ends_with(unit) ? value : value + unit;
}

const char* WindDirectionLabel(const std::string& direction) {
    struct Direction {
        std::string_view code;
        const char* label;
    };
    constexpr Direction directions[] = {
        {"n", "北风"},          {"nne", "东北偏北风"},   {"ne", "东北风"}, {"ene", "东北偏东风"},
        {"e", "东风"},          {"ese", "东南偏东风"},   {"se", "东南风"}, {"sse", "东南偏南风"},
        {"s", "南风"},          {"ssw", "西南偏南风"},   {"sw", "西南风"}, {"wsw", "西南偏西风"},
        {"w", "西风"},          {"wnw", "西北偏西风"},   {"nw", "西北风"}, {"nnw", "西北偏北风"},
        {"none", "无持续风向"}, {"vrb", "风向变化不定"},
    };
    for (const auto& item : directions) {
        if (item.code == direction) {
            return item.label;
        }
    }
    // Providers that already return descriptive text need no translation.
    return direction.empty() ? "风向" : direction.c_str();
}

void UpdateWindDirection(lv_obj_t* label, const std::string& direction) {
    const char* text = WindDirectionLabel(direction);
    const auto* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    // The longest mapped labels have six characters; bound the text layer even if the
    // server sends a longer label. Scale around the center of the existing column.
    const int width = std::clamp<int>(size.x, kWindColumnWidth, 192);
    lv_obj_set_width(label, width);
    lv_obj_set_x(label, kWindColumnCenterX - width / 2);
    Scale(label, kWindColumnWidth * 256 / width);
    UpdateLabel(label, text);
}

bool ParseReading(const std::string& text, const char* unit, double min, double max,
                  double& value) {
    const char* start = text.c_str();
    char* end;
    value = std::strtod(start, &end);
    return end != start && (*end == '\0' || std::strcmp(end, unit) == 0) && std::isfinite(value) &&
           value >= min && value <= max;
}

std::string FeelsLike(const SpeakerStandbyWeather::Snapshot& weather) {
    if (weather.feels_like) {
        char text[32];
        std::snprintf(text, sizeof(text), "%g°", *weather.feels_like);
        return text;
    }

    double temp, humidity;
    if (!ParseReading(weather.temperature, "°C", -90, 65, temp) ||
        !ParseReading(weather.humidity, "%", 0, 100, humidity)) {
        return "--";
    }
    if (temp >= 26.0 && humidity >= 40.0) {
        const double f = temp * 1.8 + 32;
        const double h = humidity;
        const double index = -42.379 + 2.04901523 * f + 10.14333127 * h - 0.22475541 * f * h -
                             0.00683783 * f * f - 0.05481717 * h * h + 0.00122874 * f * f * h +
                             0.00085282 * f * h * h - 0.00000199 * f * f * h * h;
        temp = (index - 32) / 1.8;
    }
    return std::to_string(std::lround(temp)) + "°";
}

const char* WeatherStatus(SpeakerStandbyWeather::State state) {
    using State = SpeakerStandbyWeather::State;
    switch (state) {
        case State::Loading:
            return "正在更新";
        case State::Offline:
            return "网络未连接";
        default:
            return "暂无天气";
    }
}

}  // namespace

void SpeakerScreensaverView::Create(lv_obj_t* parent, lv_event_cb_t on_exit, void* context) {
    root_ = SpeakerUi::Panel(parent);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBackground), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(root_, lv_color_hex(kBackgroundDeep), LV_PART_MAIN);
    lv_obj_add_flag(root_,
                    static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_add_event_cb(root_, on_exit, LV_EVENT_CLICKED, context);

    for (int x = 20; x < SpeakerUi::kWidth; x += 52) {
        Block(root_, x, 48, 1, 432, kGrid, LV_OPA_20);
    }
    for (int y = 74; y < SpeakerUi::kHeight; y += 44) {
        Block(root_, 0, y, SpeakerUi::kWidth, 1, kGrid, LV_OPA_20);
    }
    Block(root_, 0, 47, SpeakerUi::kWidth, 1, kBlue, LV_OPA_80);
    Block(root_, 399, 76, 2, 350, kCyan, LV_OPA_70);
    Block(root_, 397, 235, 6, 34, kCyan, LV_OPA_60, LV_RADIUS_CIRCLE);
    Block(root_, 0, 454, 180, 2, kBlue, LV_OPA_70);
    Block(root_, 620, 454, 180, 2, kBlue, LV_OPA_70);

    lv_obj_align(Icon(root_, MATERIAL_SYMBOLS_LOCATION_ON, kCyan), LV_ALIGN_TOP_LEFT, 18, 8);
    city_ = Label(root_, "本地天气", kText);
    lv_obj_set_width(city_, 250);
    lv_label_set_long_mode(city_, LV_LABEL_LONG_DOT);
    lv_obj_align(city_, LV_ALIGN_TOP_LEFT, 50, 2);
    wifi_ = Icon(root_, MATERIAL_SYMBOLS_WIFI_OFF, kText);
    lv_obj_align(wifi_, LV_ALIGN_TOP_RIGHT, -18, 8);

    CreateClock();
    CreateWeather();

    Frame(root_, 103, 304, 194, 72, kBackground, LV_OPA_40, kCyan, LV_OPA_70, 3, LV_RADIUS_CIRCLE);
    Frame(root_, 126, 318, 148, 44, kBackground, LV_OPA_TRANSP, kViolet, LV_OPA_80, 1,
          LV_RADIUS_CIRCLE);
    constexpr int heights[] = {20, 32, 45, 60, 45, 32, 20};
    for (size_t i = 0; i < std::size(heights); ++i) {
        Block(root_, 165 + 12 * static_cast<int>(i), 340 - heights[i] / 2, 4, heights[i],
              i == 3 ? kViolet : kCyan, LV_OPA_COVER, 2);
    }
    Centered(root_, "筑音 · 待机中", 34, 390, 332);
    Block(root_, 68, 414, 42, 2, kCyan, LV_OPA_80);
    Block(root_, 290, 414, 42, 2, kCyan, LV_OPA_80);
}

void SpeakerScreensaverView::CreateClock() {
    Frame(root_, 18, 72, 364, 160, kPanel, LV_OPA_50, kCyan, LV_OPA_50);
    constexpr int xs[] = {38, 108, 204, 274};
    constexpr int geometry[][4] = {
        {9, 0, 44, 10},  {52, 8, 10, 47}, {52, 67, 10, 47}, {9, 112, 44, 10},
        {0, 67, 10, 47}, {0, 8, 10, 47},  {9, 56, 44, 10},
    };
    for (size_t i = 0; i < segments_.size(); ++i) {
        for (size_t segment = 0; segment < segments_[i].size(); ++segment) {
            const auto& g = geometry[segment];
            segments_[i][segment] =
                Block(root_, xs[i] + g[0], 91 + g[1], g[2], g[3], kSegmentOff, LV_OPA_40, 5);
        }
    }
    Block(root_, 182, 122, 11, 11, kCyan, LV_OPA_COVER, 3);
    Block(root_, 182, 172, 11, 11, kCyan, LV_OPA_COVER, 3);
    seconds_ = Centered(root_, "--", 340, 172, 42, kCyan);
    date_ = Centered(root_, "等待网络校时", 24, 240, 352);
}

void SpeakerScreensaverView::CreateWeather() {
    auto* panel = Frame(root_, 420, 72, 364, 356, kWeatherPanel, LV_OPA_90, kBlue, LV_OPA_80);
    // Build the local weather symbols from lightweight LVGL shapes.
    sun_ = CreateSun(panel);
    cloud_ = Surface(panel, 30, 53, 74, 45);
    Block(cloud_, 1, 18, 72, 26, kText, LV_OPA_COVER, 13);
    Block(cloud_, 11, 8, 28, 29, kText, LV_OPA_COVER, 14);
    Block(cloud_, 28, 0, 35, 39, kText, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    rain_ = Surface(panel, 42, 94, 50, 12);
    for (int x : {0, 22, 44}) {
        Block(rain_, x, 0, 5, 12, kCyan, LV_OPA_COVER, 2);
    }
    weather_placeholder_ = Centered(panel, "--", 20, 55, 94, kMuted);
    UpdateWeatherIcons({});

    Block(panel, 134, 30, 1, 108, kCyan, LV_OPA_70);
    temperature_ = Centered(panel, "--°C", 158, 32, 184);
    Scale(temperature_, 380);
    condition_ = Centered(panel, "正在更新", 158, 101, 184);
    // Keep long provider descriptions on one line so they cannot overlap the statistics.
    lv_obj_set_height(
        condition_, lv_font_get_line_height(lv_obj_get_style_text_font(condition_, LV_PART_MAIN)));
    Block(panel, 20, 150, 324, 1, kCyan, LV_OPA_60);

    const char* titles[] = {"体感", "湿度", "风向"};
    lv_obj_t** values[] = {&feels_like_, &humidity_, &wind_power_};
    for (int i = 0; i < 3; ++i) {
        const int x = 10 + 112 * i;
        if (i > 0) {
            Block(panel, x - 2, 173, 1, 86, kCyan, LV_OPA_50);
        }
        if (i == 0) {
            lv_obj_set_pos(Icon(panel, MATERIAL_SYMBOLS_DEVICE_THERMOSTAT, kCyan), x + 40, 166);
        } else if (i == 1) {
            Centered(panel, "%", x, 161, 110, kCyan);
        } else {
            Block(panel, x + 46, 173, 20, 2, kCyan, LV_OPA_COVER, 1);
            Block(panel, x + 39, 179, 32, 2, kCyan, LV_OPA_COVER, 1);
            Block(panel, x + 44, 185, 19, 2, kCyan, LV_OPA_COVER, 1);
        }
        auto* title = Centered(panel, titles[i], x, kWindTitleY, kWindColumnWidth, kMuted);
        if (i == 2) {
            wind_direction_ = title;
        }
        *values[i] = Centered(panel, "--", x, 233, 110);
    }

    auto* air = Frame(panel, 40, 286, 284, 52, kBackground, LV_OPA_70, kCyan, LV_OPA_COVER, 1,
                      LV_RADIUS_CIRCLE);
    Centered(air, "空气", 33, 1, 65);
    air_category_ = Centered(air, "--", 102, 1, 86, kMuted);
    air_value_ = Centered(air, "--", 195, 1, 57);
}

void SpeakerScreensaverView::SetVisible(bool visible) {
    lv_obj_set_flag(root_, LV_OBJ_FLAG_HIDDEN, !visible);
}

bool SpeakerScreensaverView::Visible() const { return !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN); }

void SpeakerScreensaverView::Update(const std::tm* local_time,
                                    const SpeakerStandbyWeather::Snapshot& weather,
                                    const char* wifi) {
    UpdateClock(local_time);
    UpdateWeather(weather);
    UpdateLabel(wifi_, wifi);
}

void SpeakerScreensaverView::UpdateClock(const std::tm* local_time) {
    constexpr uint8_t digit_masks[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    std::array<uint8_t, 4> next{};
    char seconds[4] = "--";
    char date[48] = "等待网络校时";
    if (local_time) {
        const auto& time = *local_time;
        next = {digit_masks[time.tm_hour / 10], digit_masks[time.tm_hour % 10],
                digit_masks[time.tm_min / 10], digit_masks[time.tm_min % 10]};
        constexpr const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
        std::strftime(seconds, sizeof(seconds), "%S", &time);
        std::snprintf(date, sizeof(date), "%02d月%02d日  星期%s", time.tm_mon + 1, time.tm_mday,
                      weekdays[time.tm_wday]);
    }
    for (size_t digit = 0; digit < masks_.size(); ++digit) {
        const uint8_t changed = masks_[digit] ^ next[digit];
        for (size_t segment = 0; segment < segments_[digit].size(); ++segment) {
            if ((changed & (1U << segment)) == 0) {
                continue;
            }
            const bool active = next[digit] & (1U << segment);
            auto* bar = segments_[digit][segment];
            lv_obj_set_style_bg_color(bar, lv_color_hex(active ? kCyan : kSegmentOff),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(bar, active ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
        }
    }
    masks_ = next;
    UpdateLabel(seconds_, seconds);
    UpdateLabel(date_, date);
}

void SpeakerScreensaverView::UpdateWeatherIcons(const SpeakerStandbyWeather::Snapshot& weather) {
    const auto& condition = weather.condition;
    // Only the locally drawn sun, cloud and rain symbols are supported here. Do not
    // accidentally interpret compound snow, fog or hail descriptions as sunshine/rain.
    constexpr const char* unsupported[] = {"雪", "雾", "霾", "沙", "尘", "冰雹", "龙卷"};
    const bool supported =
        weather.state == SpeakerStandbyWeather::State::Ready &&
        std::none_of(std::begin(unsupported), std::end(unsupported),
                     [&](const char* text) { return condition.find(text) != std::string::npos; });
    const bool rain = supported &&
        (condition.find("雨") != std::string::npos || condition.find("雷") != std::string::npos);
    const bool partly_cloudy = supported && !rain && condition.find("多云") != std::string::npos;
    const bool sunny = supported && !rain && !partly_cloudy &&
                       condition.find("晴") != std::string::npos;
    const bool cloudy = rain || partly_cloudy ||
        (supported && !sunny &&
         (condition.find("阴") != std::string::npos || condition.find("云") != std::string::npos));

    lv_obj_set_pos(sun_, partly_cloudy ? 43 : 35, partly_cloudy ? 36 : 43);
    lv_obj_set_pos(cloud_, partly_cloudy ? 28 : 30, partly_cloudy ? 68 : rain ? 44 : 53);
    lv_obj_set_flag(sun_, LV_OBJ_FLAG_HIDDEN, !sunny && !partly_cloudy);
    lv_obj_set_flag(cloud_, LV_OBJ_FLAG_HIDDEN, !cloudy);
    lv_obj_set_flag(rain_, LV_OBJ_FLAG_HIDDEN, !rain);
    lv_obj_set_flag(weather_placeholder_, LV_OBJ_FLAG_HIDDEN, sunny || cloudy);
}

void SpeakerScreensaverView::UpdateWeather(const SpeakerStandbyWeather::Snapshot& weather) {
    if (last_weather_ == weather) {
        return;
    }
    last_weather_ = weather;
    const bool ready = weather.state == SpeakerStandbyWeather::State::Ready;
    UpdateLabel(city_, ready && !weather.city.empty() ? weather.city.c_str() : "本地天气");
    UpdateLabel(temperature_, WithUnit(weather.temperature, "°C", "--°C").c_str());
    UpdateLabel(condition_, ready && !weather.condition.empty() ? weather.condition.c_str()
                                                                : WeatherStatus(weather.state));
    UpdateWeatherIcons(weather);
    UpdateLabel(feels_like_, FeelsLike(weather).c_str());
    UpdateLabel(humidity_, WithUnit(weather.humidity, "%").c_str());
    UpdateWindDirection(wind_direction_, weather.wind_direction);
    UpdateLabel(wind_power_, WithUnit(weather.wind_power, "级").c_str());
    constexpr const char* air_names[] = {"优", "良", "轻度", "中度", "重度"};
    constexpr uint32_t air_colors[] = {0x5CFF73, 0xE9ED4A, 0xFFB020, 0xFF5A67, 0xFF5A67};
    const int aqi = weather.aqi;
    const int band = aqi <= 50 ? 0 : aqi <= 100 ? 1 : aqi <= 150 ? 2 : aqi <= 200 ? 3 : 4;
    UpdateLabel(air_category_, aqi < 0 ? "--" : air_names[band]);
    lv_obj_set_style_text_color(air_category_, lv_color_hex(aqi < 0 ? kMuted : air_colors[band]),
                                LV_PART_MAIN);
    UpdateLabel(air_value_, aqi < 0 ? "--" : std::to_string(aqi).c_str());
}

#endif
