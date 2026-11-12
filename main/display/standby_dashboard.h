#ifndef STANDBY_DASHBOARD_H
#define STANDBY_DASHBOARD_H

#include <array>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "storage/sd_card_service.h"

class StandbyDashboard {
public:
    StandbyDashboard(lv_obj_t* parent, const lv_font_t* text_font, const lv_font_t* icon_font);
    ~StandbyDashboard();

    void SetVisible(bool visible);
    void SetFonts(const lv_font_t* text_font, const lv_font_t* icon_font);
    bool IsVisible() const { return visible_; }
    void Update(bool force = false);
    void ShowNotification(const char* notification, int duration_ms);
    void ShowPomodoroView();
    void ClosePomodoroView();
    void ShowCalendarView();
    void CloseCalendarView();
    void ShowMusicView();
    void CloseMusicView();
    void SetSettingsShortcutVisible(bool visible);
    void CloseFeatureViews();
    bool IsPomodoroActive() const;

private:
    static constexpr int kDigitCount = 4;
    static constexpr int kPomodoroDigitCount = 6;
    static constexpr int kSegmentCount = 7;
    static constexpr int kCalendarCellCount = 42;
    static constexpr int kMusicSpectrumBarCount = 9;
    static constexpr size_t kMaxMusicPlaylistRows = 48;
    static constexpr uint32_t kMusicRefreshTaskStackBytes = 8192;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* city_label_ = nullptr;
    lv_obj_t* network_label_ = nullptr;
    lv_obj_t* battery_group_ = nullptr;
    lv_obj_t* battery_icon_label_ = nullptr;
    lv_obj_t* battery_percent_label_ = nullptr;
    lv_obj_t* notification_chip_ = nullptr;
    lv_obj_t* notification_label_ = nullptr;
    lv_obj_t* seconds_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* weather_icon_primary_ = nullptr;
    lv_obj_t* weather_icon_accent_ = nullptr;
    lv_obj_t* temperature_label_ = nullptr;
    lv_obj_t* weather_label_ = nullptr;
    lv_obj_t* feels_like_label_ = nullptr;
    lv_obj_t* humidity_label_ = nullptr;
    lv_obj_t* wind_title_label_ = nullptr;
    lv_obj_t* wind_power_label_ = nullptr;
    lv_obj_t* air_category_label_ = nullptr;
    lv_obj_t* air_value_label_ = nullptr;
    lv_obj_t* pulse_ring_ = nullptr;
    lv_obj_t* pomodoro_entry_button_ = nullptr;
    lv_obj_t* calendar_entry_button_ = nullptr;
    lv_obj_t* music_entry_button_ = nullptr;
    lv_obj_t* settings_entry_button_ = nullptr;
    lv_obj_t* settings_shortcut_ = nullptr;
    lv_obj_t* settings_view_ = nullptr;
    lv_obj_t* settings_account_state_ = nullptr;
    lv_obj_t* settings_username_ = nullptr;
    lv_obj_t* settings_balance_ = nullptr;
    lv_obj_t* settings_refresh_button_ = nullptr;
    lv_obj_t* settings_refresh_label_ = nullptr;
    lv_obj_t* settings_consent_button_ = nullptr;
    lv_obj_t* settings_sign_out_button_ = nullptr;
    lv_obj_t* settings_sign_out_label_ = nullptr;
    lv_obj_t* settings_wifi_state_ = nullptr;
    lv_obj_t* settings_wifi_detail_ = nullptr;
    lv_obj_t* settings_wifi_button_ = nullptr;
    lv_obj_t* settings_wifi_button_label_ = nullptr;
    lv_obj_t* settings_wifi_setup_button_ = nullptr;
    lv_obj_t* settings_factory_reset_button_ = nullptr;
    lv_obj_t* settings_factory_reset_dialog_ = nullptr;
    lv_timer_t* settings_timer_ = nullptr;
    uint64_t sign_out_confirmation_ = 0;
    bool settings_shortcut_needed_ = false;
    bool settings_chinese_ = false;
    lv_obj_t* pomodoro_view_ = nullptr;
    lv_obj_t* pomodoro_progress_arc_ = nullptr;
    lv_obj_t* pomodoro_state_label_ = nullptr;
    lv_obj_t* pomodoro_detail_label_ = nullptr;
    lv_obj_t* pomodoro_pause_button_ = nullptr;
    lv_obj_t* pomodoro_pause_label_ = nullptr;
    lv_obj_t* calendar_view_ = nullptr;
    lv_obj_t* calendar_clock_label_ = nullptr;
    lv_obj_t* calendar_month_label_ = nullptr;
    lv_obj_t* calendar_today_label_ = nullptr;
    lv_obj_t* calendar_selected_date_label_ = nullptr;
    lv_obj_t* calendar_selected_weekday_label_ = nullptr;
    lv_obj_t* calendar_lunar_label_ = nullptr;
    lv_obj_t* calendar_good_label_ = nullptr;
    lv_obj_t* calendar_avoid_label_ = nullptr;
    lv_obj_t* music_view_ = nullptr;
    lv_obj_t* music_playlist_container_ = nullptr;
    lv_obj_t* music_playlist_count_label_ = nullptr;
    lv_obj_t* music_empty_label_ = nullptr;
    lv_obj_t* music_track_name_label_ = nullptr;
    lv_obj_t* music_track_meta_label_ = nullptr;
    lv_obj_t* music_state_label_ = nullptr;
    lv_obj_t* music_progress_bar_ = nullptr;
    lv_obj_t* music_elapsed_label_ = nullptr;
    lv_obj_t* music_play_pause_button_ = nullptr;
    lv_obj_t* music_play_pause_label_ = nullptr;
    lv_timer_t* notification_timer_ = nullptr;

    std::array<std::array<lv_obj_t*, kSegmentCount>, kDigitCount> digit_segments_{};
    std::array<lv_obj_t*, 2> colon_dots_{};
    std::array<std::array<lv_obj_t*, kSegmentCount>, kPomodoroDigitCount>
        pomodoro_digit_segments_{};
    std::array<lv_obj_t*, 4> pomodoro_colon_dots_{};
    std::array<lv_obj_t*, kCalendarCellCount> calendar_day_buttons_{};
    std::array<lv_obj_t*, kCalendarCellCount> calendar_day_labels_{};
    std::array<lv_obj_t*, kMusicSpectrumBarCount> music_spectrum_bars_{};
    std::vector<MediaFile> music_playlist_;
    std::vector<lv_obj_t*> music_playlist_rows_;
    std::vector<lv_obj_t*> music_playlist_name_labels_;
    std::mutex music_refresh_mutex_;
    std::vector<MediaFile> pending_music_playlist_;
    std::atomic<bool> music_refresh_in_progress_{false};
    std::atomic<bool> music_refresh_ready_{false};
    std::atomic<bool> music_refresh_shutdown_{false};
    bool pending_music_sd_mounted_ = false;

    const lv_font_t* text_font_ = nullptr;
    const lv_font_t* icon_font_ = nullptr;
    bool visible_ = false;
    bool pomodoro_visible_ = false;
    bool calendar_visible_ = false;
    bool music_visible_ = false;
    int calendar_year_ = 2026;
    int calendar_month_ = 1;
    int calendar_selected_day_ = 1;
    int music_last_index_ = -2;
    int music_last_state_ = -1;
    int64_t last_environment_update_us_ = 0;

    void CreateBackground();
    void CreateTopBar();
    void CreateClockPanel();
    void CreateWeatherPanel();
    void CreatePulseIndicator();
    void CreatePomodoroEntry();
    void CreatePomodoroView();
    void CreateCalendarEntry();
    void CreateCalendarView();
    void CreateMusicEntry();
    void CreateMusicView();
    void CreateSettingsEntry();
    void CreateSettingsView();
    void DestroySettingsView();
    void ShowSettingsView();
    void CloseSettingsView();
    void UpdateSettingsView();
    void HandleSettingsSignOut();
    void ShowFactoryResetConfirmation();
    void CloseFactoryResetConfirmation();
    const char* SettingsText(const char* chinese, const char* fallback) const {
        return settings_chinese_ ? chinese : fallback;
    }
    void SetMainEntryButtonsEnabled(bool enabled);
    void CreateNotificationChip();
    void CreateDigit(int index, int x, int y);
    void CreatePomodoroDigit(int index, int x, int y);
    void SetDigit(int index, int value);
    void SetPomodoroDigit(int index, int value, lv_color_t color);
    void UpdateClock(time_t now);
    void UpdateEnvironment(bool force);
    void UpdateDeviceStatus();
    void UpdatePomodoro();
    void UpdateCalendarClock(time_t now);
    void UpdateCalendarView();
    void ChangeCalendarMonth(int delta);
    void SelectCalendarDate(int day);
    void ReturnCalendarToToday();
    void RefreshMusicPlaylist();
    void ApplyPendingMusicPlaylist();
    void RenderMusicPlaylist(std::vector<MediaFile> playlist, bool sd_mounted);
    void UpdateMusicView();
    void StyleMusicPlaylistRow(int index, bool selected, bool playing);
    void HandleMusicTrackSelect(int index);
    void HandleMusicPrevious();
    void HandleMusicPlayPause();
    void HandleMusicNext();
    void HandlePomodoroPreset(int minutes);
    void HandlePomodoroPauseResume();
    void HandlePomodoroClose();
    void StartPulseAnimation();
    void StopPulseAnimation();

    static const char* WeekdayText(int weekday);
    static int DaysInMonth(int year, int month);
    static int FirstWeekday(int year, int month);
    static std::string LunarDateText(int year, int month, int day);
    static void SelectWeatherIcons(const std::string& weather, const char*& primary,
                                   const char*& accent, lv_color_t& primary_color,
                                   lv_color_t& accent_color);
    static bool CalculateFeelsLikeC(const std::string& temperature, const std::string& humidity,
                                    int& feels_like);
    static std::string WindTitle(const std::string& direction);
    static std::string WindPower(const std::string& power);
    static std::string AirCategory(int aqi, const std::string& category);
    static lv_color_t AirCategoryColor(int aqi);
    static void FormatDuration(int seconds, char* output, size_t output_size);
    static void NotificationTimerCallback(lv_timer_t* timer);
    static void PomodoroEntryEventCallback(lv_event_t* event);
    static void PomodoroPresetEventCallback(lv_event_t* event);
    static void PomodoroPauseEventCallback(lv_event_t* event);
    static void PomodoroCloseEventCallback(lv_event_t* event);
    static void CalendarEntryEventCallback(lv_event_t* event);
    static void CalendarPreviousMonthEventCallback(lv_event_t* event);
    static void CalendarNextMonthEventCallback(lv_event_t* event);
    static void CalendarTodayEventCallback(lv_event_t* event);
    static void CalendarDayEventCallback(lv_event_t* event);
    static void CalendarCloseEventCallback(lv_event_t* event);
    static void MusicEntryEventCallback(lv_event_t* event);
    static void MusicRefreshEventCallback(lv_event_t* event);
    static void MusicTrackEventCallback(lv_event_t* event);
    static void MusicPreviousEventCallback(lv_event_t* event);
    static void MusicPlayPauseEventCallback(lv_event_t* event);
    static void MusicNextEventCallback(lv_event_t* event);
    static void MusicCloseEventCallback(lv_event_t* event);
    static void MusicRefreshTask(void* arg);
};

#endif  // STANDBY_DASHBOARD_H
