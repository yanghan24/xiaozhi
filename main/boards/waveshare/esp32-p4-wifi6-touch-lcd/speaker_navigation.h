#pragma once
#include <sdkconfig.h>

#if CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <lvgl.h>
#include <array>
#include <functional>

class SpeakerEdgeGesture {
public:
    void Create(lv_obj_t* parent, void (*on_swipe)(void*), void* context);
    void SetVisible(bool visible);

private:
    static void OnTouch(lv_event_t* event);

    lv_obj_t* left_ = nullptr;
    lv_obj_t* bottom_ = nullptr;
    void (*on_swipe_)(void*) = nullptr;
    void* context_ = nullptr;
    lv_point_t start_{};
    bool tracking_ = false;
};

class SpeakerNavigation {
public:
    enum class Page { Home, Settings, Account, Calendar, Music, Recording, Pomodoro, Count };

    void Create(lv_obj_t* parent);
    void Register(Page page, std::function<void()> show, std::function<void()> hide,
                  std::function<bool()> can_leave = {});
    void Open(Page page);
    void Back();
    void Home();
    Page Current() const { return current_; }
    bool CanLeave() const;
    static void OnBack(lv_event_t* event);

private:
    struct Route {
        std::function<void()> show;
        std::function<void()> hide;
        std::function<bool()> can_leave;
    };

    Route& For(Page page) { return routes_[static_cast<size_t>(page)]; }
    const Route& For(Page page) const { return routes_[static_cast<size_t>(page)]; }

    std::array<Route, static_cast<size_t>(Page::Count)> routes_;
    Page current_ = Page::Count;
    SpeakerEdgeGesture edges_;
};

#endif
