#include "speaker_wave.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"
#include <cmath>

namespace {
constexpr int kSegments = 32;

lv_color_t Gradient(float x, SpeakerWave::Shape shape) {
    const bool horizon = shape == SpeakerWave::Shape::Horizon;
    const auto first = lv_color_hex(shape == SpeakerWave::Shape::Ribbon ? 0x8C55E2 : 0x16358D);
    const auto middle = lv_color_hex(0x2F6AFF);
    const auto last = lv_color_hex(horizon ? 0xFFC586 : 0x6CEEFF);
    if (x < 0.5f) {
        return lv_color_mix(middle, first, static_cast<uint8_t>(x * 510));
    }
    return lv_color_mix(last, middle, static_cast<uint8_t>((x - 0.5f) * 510));
}
}

void SpeakerWave::Create(lv_obj_t* parent, int y, int height, Shape shape) {
    shape_ = shape;
    region_ = SpeakerUi::Surface(parent, 0, y, SpeakerUi::kWidth, height);
    lv_obj_add_event_cb(region_, Draw, LV_EVENT_DRAW_MAIN, this);
}

void SpeakerWave::SetMotion(Motion motion) {
    if (motion_ == motion) {
        return;
    }
    motion_ = motion;
    Stop();
    if (motion == Motion::Still) {
        phase_ = 0;
    } else {
        timer_ = lv_timer_create([](lv_timer_t* timer) {
            auto* wave = static_cast<SpeakerWave*>(lv_timer_get_user_data(timer));
            wave->phase_ += wave->motion_ == Motion::Speaking ? 0.24f :
                            wave->motion_ == Motion::Listening ? 0.17f : 0.065f;
            if (wave->phase_ > 6.2832f) {
                wave->phase_ -= 6.2832f;
            }
            lv_obj_invalidate(wave->region_);
        }, 48, this);
    }
    lv_obj_invalidate(region_);
}

void SpeakerWave::Stop() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void SpeakerWave::Draw(lv_event_t* event) {
    static_cast<SpeakerWave*>(lv_event_get_user_data(event))->Render(event);
}

void SpeakerWave::Render(lv_event_t* event) const {
    auto* layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(region_, &area);
    const int width = lv_area_get_width(&area);
    const int height = lv_area_get_height(&area);
    const auto sample = [&](int index) -> lv_point_precise_t {
        const float x = static_cast<float>(index) / kSegments;
        float y;
        if (shape_ == Shape::Horizon) {
            const float center = 2 * x - 1;
            y = 0.27f + 0.54f * center * center;
        } else if (shape_ == Shape::Sweep) {
            y = 0.56f + 0.29f * std::sin(5.6f * x + 0.25f);
        } else {
            const float depth = motion_ == Motion::Speaking ? 0.26f :
                                motion_ == Motion::Listening ? 0.20f : 0.12f;
            y = 0.47f - depth * std::sin(6.7f * x + phase_) +
                0.045f * std::sin(13.4f * x - phase_);
        }
        return {area.x1 + static_cast<int32_t>((width - 1) * x),
                area.y1 + static_cast<int32_t>(height * y)};
    };

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.round_start = 1;
    line.round_end = 1;
    lv_draw_triangle_dsc_t fill;
    lv_draw_triangle_dsc_init(&fill);
    fill.opa = shape_ == Shape::Ribbon ? LV_OPA_20 : LV_OPA_10;
    auto left = sample(0);
    for (int i = 1; i <= kSegments; ++i) {
        const auto right = sample(i);
        const auto color = Gradient(static_cast<float>(i - 1) / (kSegments - 1), shape_);
        fill.color = color;
        fill.p[0] = left;
        fill.p[1] = right;
        fill.p[2] = {left.x, area.y2};
        lv_draw_triangle(layer, &fill);
        fill.p[0] = {right.x, area.y2};
        lv_draw_triangle(layer, &fill);

        line.p1 = left;
        line.p2 = right;
        line.color = color;
        line.width = 16;
        line.opa = static_cast<lv_opa_t>(36);
        lv_draw_line(layer, &line);
        line.color = lv_color_mix(lv_color_white(), color, 52);
        line.width = 3;
        line.opa = LV_OPA_COVER;
        lv_draw_line(layer, &line);
        left = right;
    }

    if (shape_ == Shape::Ribbon) {
        const auto crest = [&](int index) {
            auto point = sample(index);
            const float x = static_cast<float>(index) / kSegments;
            point.y += static_cast<int32_t>(24 * std::sin(x * 11.5f + phase_ * 0.8f));
            return point;
        };
        left = crest(0);
        for (int i = 2; i <= kSegments; i += 2) {
            line.p1 = left;
            line.p2 = crest(i);
            line.color = Gradient(static_cast<float>(i - 1) / kSegments, shape_);
            line.width = 2;
            line.opa = LV_OPA_60;
            lv_draw_line(layer, &line);
            left = line.p2;
        }
    }
}

#endif
