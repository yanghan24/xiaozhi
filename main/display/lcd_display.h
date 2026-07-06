#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
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
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles

    void InitializeLcdThemes();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

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
    lv_obj_t* lichuang_home_time_label_ = nullptr;
    lv_obj_t* lichuang_home_date_label_ = nullptr;
    lv_obj_t* lichuang_home_weekday_label_ = nullptr;
    lv_obj_t* lichuang_home_weather_label_ = nullptr;
    lv_obj_t* lichuang_home_air_label_ = nullptr;
    lv_obj_t* lichuang_page_label_ = nullptr;
    lv_obj_t* lichuang_pomodoro_status_label_ = nullptr;
    lv_obj_t* lichuang_pomodoro_remaining_label_ = nullptr;
    lv_obj_t* lichuang_music_status_label_ = nullptr;
    lv_obj_t* lichuang_music_track_label_ = nullptr;
    int lichuang_feature_view_index_ = 0;
    time_t lichuang_last_home_update_minute_ = 0;
    time_t lichuang_last_media_scan_time_ = 0;

    void CreateLichuangFeatureShell();
    void ShowLichuangFeatureShell();
    void HideLichuangFeatureShell();
    void UpdateLichuangFeatureShellVisibility();
    void SwitchLichuangFeatureView(int delta);
    void SetLichuangFeatureView(LichuangFeatureView view);
    void UpdateLichuangHomeView(bool force = false);
    void UpdateLichuangPomodoroView(bool force = false);
    void UpdateLichuangMusicView(bool force = false);
    void HandleLichuangPomodoroPreset(int minutes);
    void HandleLichuangPomodoroPauseResume();
    void HandleLichuangPomodoroCancel();
    static void LichuangFeatureGestureEventCallback(lv_event_t* event);
    static void LichuangPomodoroPresetEventCallback(lv_event_t* event);
    static void LichuangPomodoroPauseResumeEventCallback(lv_event_t* event);
    static void LichuangPomodoroCancelEventCallback(lv_event_t* event);
#endif

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height);
    
public:
    ~LcdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetStatus(const char* status) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
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
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);
};

#endif // LCD_DISPLAY_H
