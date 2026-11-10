#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
#include "standby_dashboard.h"
#endif

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
#include <cstdint>
#include <ctime>
#include <memory>

#define PREVIEW_IMAGE_DURATION_MS 5000

class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    
    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    esp_timer_handle_t top_bar_auto_hide_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles

#if CONFIG_XIAOZHI_DEVICE_CONTROL
    lv_obj_t* binding_panel_ = nullptr;
    lv_obj_t* binding_title_ = nullptr;
    lv_obj_t* binding_qr_frame_ = nullptr;
    lv_obj_t* binding_qr_ = nullptr;
    lv_obj_t* binding_detail_ = nullptr;
    lv_obj_t* account_button_ = nullptr;
    lv_obj_t* binding_close_button_ = nullptr;
    lv_timer_t* binding_timer_ = nullptr;
    std::function<void()> account_action_;
    std::string binding_uri_;
    std::string binding_purpose_;
    int64_t binding_deadline_ms_ = 0;
    int64_t binding_seconds_ = -1;
    bool binding_qr_valid_ = false;

    void CreateDeviceBindingUi();
    void UpdateAccountButtonPosition();
    void UpdateBindingCountdown();
#endif

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    std::unique_ptr<StandbyDashboard> standby_dashboard_ = nullptr;
    lv_obj_t* cyber_exit_button_ = nullptr;
    lv_obj_t* cyber_exit_label_ = nullptr;
    lv_obj_t* cyber_brand_name_label_ = nullptr;
    lv_obj_t* cyber_company_name_label_ = nullptr;
    bool cyber_logo_animation_running_ = false;
#endif

    void InitializeLcdThemes();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
    void SetTopBarVisible(bool visible);
    void ShowTopBarTemporarily();
    void AttachTopBarTouchTarget(lv_obj_t* object);
    static void TopBarTouchEventCallback(lv_event_t* event);

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    void CreateCyberConversationUI();
    void UpdateCyberExitButtonVisibility();
    void StartCyberLogoAnimation();
    void StopCyberLogoAnimation();
    static void CyberLogoRotationAnimCallback(void* object, int32_t value);
    static void CyberLogoScaleAnimCallback(void* object, int32_t value);
    static void CyberExitEventCallback(lv_event_t* event);
    void CreateStandbyDashboard();
    void UpdateStandbyDashboardVisibility();
    void UpdateStandbyDashboard(bool force = false);
#endif

#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    enum class LichuangFeatureView {
        kHome = 0,
        kPomodoro,
        kMusic,
        kRecorder,
        kCount,
    };

    lv_obj_t* lichuang_feature_shell_ = nullptr;
    lv_obj_t* lichuang_feature_views_[static_cast<int>(LichuangFeatureView::kCount)] = {};
    lv_obj_t* lichuang_nav_buttons_[static_cast<int>(LichuangFeatureView::kCount)] = {};
    lv_obj_t* lichuang_home_time_label_ = nullptr;
    lv_obj_t* lichuang_home_date_label_ = nullptr;
    lv_obj_t* lichuang_home_weekday_label_ = nullptr;
    lv_obj_t* lichuang_home_weather_label_ = nullptr;
    lv_obj_t* lichuang_home_air_label_ = nullptr;
    lv_obj_t* lichuang_page_label_ = nullptr;
    lv_obj_t* lichuang_pomodoro_mode_label_ = nullptr;
    lv_obj_t* lichuang_pomodoro_status_label_ = nullptr;
    lv_obj_t* lichuang_pomodoro_remaining_label_ = nullptr;
    lv_obj_t* lichuang_pomodoro_progress_bar_ = nullptr;
    lv_obj_t* lichuang_pomodoro_preset_label_ = nullptr;
    lv_obj_t* lichuang_pomodoro_preset_row_ = nullptr;
    lv_obj_t* lichuang_pomodoro_pause_button_ = nullptr;
    lv_obj_t* lichuang_music_status_label_ = nullptr;
    lv_obj_t* lichuang_music_track_label_ = nullptr;
    lv_obj_t* lichuang_music_detail_label_ = nullptr;
    lv_obj_t* lichuang_music_list_label_ = nullptr;
    lv_obj_t* lichuang_music_progress_bar_ = nullptr;
    lv_obj_t* lichuang_music_play_button_ = nullptr;
    lv_obj_t* lichuang_recorder_status_label_ = nullptr;
    lv_obj_t* lichuang_recorder_file_label_ = nullptr;
    lv_obj_t* lichuang_recorder_detail_label_ = nullptr;
    lv_obj_t* lichuang_recorder_progress_bar_ = nullptr;
    lv_obj_t* lichuang_recorder_start_button_ = nullptr;
    lv_obj_t* lichuang_recorder_pause_button_ = nullptr;
    lv_obj_t* lichuang_recorder_stop_button_ = nullptr;
    int lichuang_feature_view_index_ = 0;
    int64_t lichuang_feature_shell_override_until_us_ = 0;
    time_t lichuang_last_home_update_minute_ = 0;
    time_t lichuang_last_media_scan_time_ = 0;

    void CreateLichuangFeatureShell();
    void ShowLichuangFeatureShell();
    void HideLichuangFeatureShell();
    void UpdateLichuangFeatureShellVisibility();
    bool IsLichuangFeatureShellVisible() const;
    void SwitchLichuangFeatureView(int delta);
    void SetLichuangFeatureView(LichuangFeatureView view);
    void UpdateLichuangHomeView(bool force = false);
    void UpdateLichuangPomodoroView(bool force = false);
    void UpdateLichuangMusicView(bool force = false);
    void UpdateLichuangRecorderView(bool force = false);
    void HandleLichuangPomodoroPreset(int minutes);
    void HandleLichuangPomodoroPauseResume();
    void HandleLichuangPomodoroCancel();
    void HandleLichuangMusicPrevious();
    void HandleLichuangMusicPlayPause();
    void HandleLichuangMusicNext();
    void HandleLichuangRecorderStart();
    void HandleLichuangRecorderPauseResume();
    void HandleLichuangRecorderStop();
    static void LichuangFeatureGestureEventCallback(lv_event_t* event);
    static void LichuangNavEventCallback(lv_event_t* event);
    static void LichuangPomodoroPresetEventCallback(lv_event_t* event);
    static void LichuangPomodoroPauseResumeEventCallback(lv_event_t* event);
    static void LichuangPomodoroCancelEventCallback(lv_event_t* event);
    static void LichuangMusicPreviousEventCallback(lv_event_t* event);
    static void LichuangMusicPlayPauseEventCallback(lv_event_t* event);
    static void LichuangMusicNextEventCallback(lv_event_t* event);
    static void LichuangRecorderStartEventCallback(lv_event_t* event);
    static void LichuangRecorderPauseResumeEventCallback(lv_event_t* event);
    static void LichuangRecorderStopEventCallback(lv_event_t* event);
#endif

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height);
    
public:
    ~LcdDisplay();
    using LvglDisplay::ShowNotification;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void ShowDeviceBinding(const DeviceControlView& view) override;
    virtual void SetAccountAction(std::function<void()> action) override;
    virtual void SetStatus(const char* status) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void ClearPreviewImage() override;
    virtual void SetupUI() override;
    virtual void ShowPomodoroTimerView() override;
    virtual void ClosePomodoroTimerView() override;
    virtual void CloseStandbyFeatureViews() override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;
    
    // Set whether to hide chat messages/subtitles
    void SetHideSubtitle(bool hide);
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy, lv_display_rotation_t rotation = LV_DISPLAY_ROTATION_0);
};

#endif // LCD_DISPLAY_H
