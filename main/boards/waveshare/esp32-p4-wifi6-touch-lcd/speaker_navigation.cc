#include "speaker_navigation.h"

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include "speaker_account/speaker_ui_widgets.h"
#include <cstdlib>
#include <utility>

namespace {
constexpr int kEdgeWidth = 24;
}

void SpeakerEdgeGesture::Create(lv_obj_t* parent, void (*on_swipe)(void*), void* context) {
    using namespace SpeakerUi;
    on_swipe_ = on_swipe;
    context_ = context;
    left_ = Surface(parent, 0, 0, kEdgeWidth, kHeight);
    bottom_ = Surface(parent, kEdgeWidth, kHeight - kEdgeWidth, kWidth - kEdgeWidth, kEdgeWidth);
    for (auto* region : {left_, bottom_}) {
        lv_obj_add_flag(region, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
        lv_obj_add_event_cb(region, OnTouch, LV_EVENT_ALL, this);
    }
    SetVisible(false);
}

void SpeakerEdgeGesture::SetVisible(bool visible) {
    tracking_ = false;
    for (auto* edge : {left_, bottom_}) {
        lv_obj_set_flag(edge, LV_OBJ_FLAG_HIDDEN, !visible);
        if (visible) {
            lv_obj_move_foreground(edge);
        }
    }
}

void SpeakerEdgeGesture::OnTouch(lv_event_t* event) {
    auto* gesture = static_cast<SpeakerEdgeGesture*>(lv_event_get_user_data(event));
    auto* input = lv_indev_active();
    const auto code = lv_event_get_code(event);
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(input, &gesture->start_);
        gesture->tracking_ = true;
    } else if (code == LV_EVENT_PRESS_LOST) {
        gesture->tracking_ = false;
    } else if (code == LV_EVENT_RELEASED && gesture->tracking_) {
        gesture->tracking_ = false;
        lv_point_t point;
        lv_indev_get_point(input, &point);
        const int dx = point.x - gesture->start_.x;
        const int dy = point.y - gesture->start_.y;
        const bool right = gesture->start_.x < kEdgeWidth && dx >= 72 && dx > std::abs(dy);
        const bool up = gesture->start_.y >= SpeakerUi::kHeight - kEdgeWidth && dy <= -72 && -dy > std::abs(dx);
        if (right || up) {
            gesture->on_swipe_(gesture->context_);
        }
    }
}

void SpeakerNavigation::Create(lv_obj_t* parent) {
    edges_.Create(parent, [](void* context) {
        static_cast<SpeakerNavigation*>(context)->Home();
    }, this);
}

void SpeakerNavigation::Register(Page page, std::function<void()> show, std::function<void()> hide,
                                 std::function<bool()> can_leave) {
    For(page) = {std::move(show), std::move(hide), std::move(can_leave)};
}

bool SpeakerNavigation::CanLeave() const {
    return current_ == Page::Count || !For(current_).can_leave || For(current_).can_leave();
}

void SpeakerNavigation::Open(Page page) {
    if (page == Page::Count || page == current_ || !CanLeave() || !For(page).show) {
        return;
    }
    if (current_ != Page::Count) {
        For(current_).hide();
    }
    current_ = page;
    For(page).show();
    edges_.SetVisible(page != Page::Home);
}

void SpeakerNavigation::Home() {
    Open(Page::Home);
}

void SpeakerNavigation::Back() {
    const bool settings_page = current_ == Page::Account || current_ == Page::SoundAndDisplay ||
                               current_ == Page::ScreensaverSettings || current_ == Page::About;
    Open(settings_page ? Page::Settings : Page::Home);
}

void SpeakerNavigation::OnBack(lv_event_t* event) {
    static_cast<SpeakerNavigation*>(lv_event_get_user_data(event))->Back();
}


#endif
