#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <lvgl.h>

class SpeakerWave {
public:
    enum class Shape { Horizon, Sweep, Ribbon };
    enum class Motion { Still, Listening, Waiting, Speaking };

    void Create(lv_obj_t* parent, int y, int height, Shape shape);
    void SetMotion(Motion motion);
    void Stop();

private:
    static void Draw(lv_event_t* event);
    void Render(lv_event_t* event) const;
    lv_obj_t* region_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    Shape shape_ = Shape::Sweep;
    Motion motion_ = Motion::Still;
    float phase_ = 0;
};

#endif
