#include "lcd_display.h"
#include "assets/lang_config.h"
#include "device_control/device_control.h"
#include "gif/lvgl_gif.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <vector>
#include <algorithm>
#include <font_awesome.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <src/misc/cache/lv_cache.h>
#if CONFIG_XIAOZHI_DEVICE_CONTROL && LV_USE_QRCODE
#include <src/libs/qrcode/qrcodegen.h>
#endif

#include "application.h"
#include "board.h"
#include "features/audio_recorder.h"
#include "features/music_player.h"
#include "features/pomodoro_timer.h"
#include "services/environment_service.h"
#include "storage/sd_card_service.h"
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
#include "assets/hncig_logo_i2.h"
#include "standby_dashboard.h"
#endif

#define TAG "LcdDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_4);

namespace {
constexpr int64_t kTopBarAutoHideUs = 3 * 1000 * 1000;
} // namespace

void LcdDisplay::InitializeLcdThemes() {
    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_4);

    // light theme
    auto light_theme = new LvglTheme("light");
    light_theme->set_background_color(lv_color_hex(0xFFFFFF));
    light_theme->set_text_color(lv_color_hex(0x000000));
    light_theme->set_chat_background_color(lv_color_hex(0xE0E0E0));
    light_theme->set_user_bubble_color(lv_color_hex(0x00FF00));
    light_theme->set_assistant_bubble_color(lv_color_hex(0xDDDDDD));
    light_theme->set_system_bubble_color(lv_color_hex(0xFFFFFF));
    light_theme->set_system_text_color(lv_color_hex(0x000000));
    light_theme->set_border_color(lv_color_hex(0x000000));
    light_theme->set_low_battery_color(lv_color_hex(0x000000));
    light_theme->set_text_font(text_font);
    light_theme->set_icon_font(icon_font);
    light_theme->set_large_icon_font(large_icon_font);

    // dark theme
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_background_color(lv_color_hex(0x000000));
    dark_theme->set_text_color(lv_color_hex(0xFFFFFF));
    dark_theme->set_chat_background_color(lv_color_hex(0x1F1F1F));
    dark_theme->set_user_bubble_color(lv_color_hex(0x00FF00));
    dark_theme->set_assistant_bubble_color(lv_color_hex(0x222222));
    dark_theme->set_system_bubble_color(lv_color_hex(0x000000));
    dark_theme->set_system_text_color(lv_color_hex(0xFFFFFF));
    dark_theme->set_border_color(lv_color_hex(0xFFFFFF));
    dark_theme->set_low_battery_color(lv_color_hex(0xFF0000));
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("light", light_theme);
    theme_manager.RegisterTheme("dark", dark_theme);
}

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    // Initialize LCD themes
    InitializeLcdThemes();

    // Load theme from settings
    Settings settings("display", false);
    std::string theme_name = settings.GetString("theme", "light");
    current_theme_ = LvglThemeManager::GetInstance().GetTheme(theme_name);

    // Create a timer to hide the preview image
    esp_timer_create_args_t preview_timer_args = {
        .callback = [](void* arg) {
            LcdDisplay* display = static_cast<LcdDisplay*>(arg);
            display->SetPreviewImage(nullptr);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "preview_timer",
        .skip_unhandled_events = false,
    };
    esp_timer_create(&preview_timer_args, &preview_timer_);

    // Create a timer to hide the transient top bar after touch.
    esp_timer_create_args_t top_bar_timer_args = {
        .callback = [](void* arg) {
            LcdDisplay* display = static_cast<LcdDisplay*>(arg);
            DisplayLockGuard lock(display);
            display->SetTopBarVisible(false);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "top_bar_auto_hide",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&top_bar_timer_args, &top_bar_auto_hide_timer_);
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    {
        esp_err_t __err = esp_lcd_panel_disp_on_off(panel_, true);
        if (__err == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "Panel does not support disp_on_off; assuming ON");
        } else {
            ESP_ERROR_CHECK(__err);
        }
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}


// RGB LCD implementation
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y,
                           bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }
    };
    
    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }
    
    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy, lv_display_rotation_t rotation)
    : LcdDisplay(panel_io, panel, width, height) {
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 50),
        .double_buffer = false,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram =false,
            .sw_rotate = true,
        },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = false,
        }
    };
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    if (rotation != LV_DISPLAY_ROTATION_0) {
        if (lvgl_port_lock(0)) {
            lv_display_set_rotation(display_, rotation);
            lvgl_port_unlock();
        }
        if (rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270) {
            std::swap(width_, height_);
        }
        ESP_LOGI(TAG, "Display rotated to %d degrees, logical resolution %dx%d",
                 static_cast<int>(rotation) * 90, width_, height_);
    }
}

LcdDisplay::~LcdDisplay() {
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    standby_dashboard_.reset();
#endif
    SetPreviewImage(nullptr);

#if CONFIG_XIAOZHI_DEVICE_CONTROL
    if (binding_timer_ != nullptr) {
        lv_timer_delete(binding_timer_);
    }
    if (binding_panel_ != nullptr) {
        lv_obj_delete(binding_panel_);
    }
    if (account_button_ != nullptr) {
        lv_obj_delete(account_button_);
    }
    if (binding_close_button_ != nullptr) {
        lv_obj_delete(binding_close_button_);
    }
#endif

    // Clean up GIF controller
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    
    if (preview_timer_ != nullptr) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
    }
    if (top_bar_auto_hide_timer_ != nullptr) {
        esp_timer_stop(top_bar_auto_hide_timer_);
        esp_timer_delete(top_bar_auto_hide_timer_);
    }

#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    if (lichuang_feature_shell_ != nullptr) {
        lv_obj_del(lichuang_feature_shell_);
        lichuang_feature_shell_ = nullptr;
    }
#endif

    if (preview_image_ != nullptr) {
        lv_obj_del(preview_image_);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_del(chat_message_label_);
    }
    if (emoji_label_ != nullptr) {
        lv_obj_del(emoji_label_);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_del(emoji_image_);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_del(emoji_box_);
    }
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_del(bottom_bar_);
    }
    if (status_bar_ != nullptr) {
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }
    if (display_ != nullptr) {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void LcdDisplay::Unlock() {
    lvgl_port_unlock();
}

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
void LcdDisplay::CreateCyberConversationUI() {
    constexpr uint32_t kBackground = 0x030B16;
    constexpr uint32_t kChrome = 0x020713;
    constexpr uint32_t kGrid = 0x123048;
    constexpr uint32_t kCyan = 0x18DFF6;
    constexpr uint32_t kBlue = 0x1679D3;
    constexpr uint32_t kPurple = 0x6C51C8;
    constexpr uint32_t kText = 0xF4F8FF;

    auto box = [](lv_obj_t* parent, int x, int y, int width, int height) {
        lv_obj_t* object = lv_obj_create(parent);
        lv_obj_set_pos(object, x, y);
        lv_obj_set_size(object, width, height);
        lv_obj_set_style_pad_all(object, 0, 0);
        lv_obj_set_style_border_width(object, 0, 0);
        lv_obj_set_style_radius(object, 0, 0);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        return object;
    };

    auto* screen = lv_screen_active();
    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    container_ = box(screen, 0, 0, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(container_, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);

    // Fine technical grid across the conversation area.
    for (int x = 20; x < 800; x += 52) {
        lv_obj_t* line = box(container_, x, 48, 1, 365);
        lv_obj_set_style_bg_color(line, lv_color_hex(kGrid), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    }
    for (int y = 74; y < 413; y += 44) {
        lv_obj_t* line = box(container_, 0, y, 800, 1);
        lv_obj_set_style_bg_color(line, lv_color_hex(kGrid), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_30, 0);
    }

    // Concentric cyan/purple radar rings from the reference design.
    const int ring_sizes[] = {244, 202, 144};
    const uint32_t ring_colors[] = {kCyan, kPurple, kCyan};
    const lv_opa_t ring_opacity[] = {LV_OPA_90, LV_OPA_80, LV_OPA_50};
    for (int i = 0; i < 3; ++i) {
        const int size = ring_sizes[i];
        lv_obj_t* ring = box(container_, (800 - size) / 2, 112 + (244 - size) / 2, size, size);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(ring_colors[i]), 0);
        lv_obj_set_style_border_opa(ring, ring_opacity[i], 0);
    }

    // The official mark is stored as a compact 2-bit indexed pixel matrix.
    // Palette index 0 is transparent, so the original royal-blue background
    // is removed and only the white/light-blue logo participates in animation.
    emoji_box_ = box(container_, 321, 156, 158, 158);
    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_transform_pivot_x(emoji_box_, 79, 0);
    lv_obj_set_style_transform_pivot_y(emoji_box_, 79, 0);
    lv_obj_set_style_transform_scale(emoji_box_, 256, 0);

    emoji_label_ = lv_label_create(emoji_box_);
    lv_label_set_text(emoji_label_, "");
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);

    emoji_image_ = lv_image_create(emoji_box_);
    lv_image_set_src(emoji_image_, &hncig_logo_i2);
    lv_obj_center(emoji_image_);

    // Use a two-level company signature instead of shrinking the complete
    // legal name into one hard-to-read line. Both labels inherit the runtime
    // asset font so all Chinese glyphs render correctly.
    cyber_brand_name_label_ = lv_label_create(container_);
    lv_label_set_text(cyber_brand_name_label_, "中湘元筑未来");
    lv_obj_set_style_text_color(cyber_brand_name_label_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_letter_space(cyber_brand_name_label_, 2, 0);
    lv_obj_align(cyber_brand_name_label_, LV_ALIGN_TOP_MID, 0, 344);

    cyber_company_name_label_ = lv_label_create(container_);
    lv_label_set_text(cyber_company_name_label_, "（湖南）科技有限公司");
    lv_obj_set_style_text_color(cyber_company_name_label_, lv_color_hex(kCyan), 0);
    lv_obj_set_style_text_letter_space(cyber_company_name_label_, 1, 0);
    lv_obj_set_style_transform_scale(cyber_company_name_label_, 190, 0);
    lv_obj_align(cyber_company_name_label_, LV_ALIGN_TOP_MID, 0, 382);

    lv_obj_t* brand_rule_left = box(container_, 118, 363, 150, 1);
    lv_obj_set_style_bg_color(brand_rule_left, lv_color_hex(kBlue), 0);
    lv_obj_set_style_bg_opa(brand_rule_left, LV_OPA_60, 0);
    lv_obj_t* brand_rule_right = box(container_, 532, 363, 150, 1);
    lv_obj_set_style_bg_color(brand_rule_right, lv_color_hex(kBlue), 0);
    lv_obj_set_style_bg_opa(brand_rule_right, LV_OPA_60, 0);

    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    // Opaque chrome keeps status and subtitles readable over the grid.
    top_bar_ = box(screen, 0, 0, 800, 48);
    lv_obj_set_style_bg_color(top_bar_, lv_color_hex(kChrome), 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar_, 1, 0);
    lv_obj_set_style_border_side(top_bar_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(top_bar_, lv_color_hex(kBlue), 0);

    lv_obj_t* accent = box(top_bar_, 18, 18, 20, 12);
    lv_obj_set_style_bg_color(accent, lv_color_hex(kCyan), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);

    // Keep the legacy status labels alive for the shared status updater, but
    // hide them on this conversation screen. Reserve room for the account entry.
    network_label_ = lv_label_create(top_bar_);
    mute_label_ = lv_label_create(top_bar_);
    battery_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_label_set_text(mute_label_, "");
    lv_label_set_text(battery_label_, "");
    lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mute_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);

    cyber_exit_button_ = lv_button_create(top_bar_);
    lv_obj_set_pos(cyber_exit_button_, 560, 4);
    lv_obj_set_size(cyber_exit_button_, 136, 40);
    lv_obj_remove_flag(cyber_exit_button_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(cyber_exit_button_, lv_color_hex(0x081B31), 0);
    lv_obj_set_style_bg_opa(cyber_exit_button_, LV_OPA_80, 0);
    lv_obj_set_style_border_color(cyber_exit_button_, lv_color_hex(0xFFB020), 0);
    lv_obj_set_style_border_opa(cyber_exit_button_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cyber_exit_button_, 1, 0);
    lv_obj_set_style_radius(cyber_exit_button_, 10, 0);
    lv_obj_set_style_pad_all(cyber_exit_button_, 0, 0);
    lv_obj_set_style_bg_color(cyber_exit_button_, lv_color_hex(0xFFB020), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(cyber_exit_button_, LV_OPA_40, LV_STATE_PRESSED);
    cyber_exit_label_ = lv_label_create(cyber_exit_button_);
    lv_label_set_text(cyber_exit_label_, "结束对话");
    lv_obj_set_style_text_color(cyber_exit_label_, lv_color_hex(kText), 0);
    lv_obj_set_style_transform_scale(cyber_exit_label_, 210, 0);
    lv_obj_center(cyber_exit_label_);
    lv_obj_add_event_cb(cyber_exit_button_, CyberExitEventCallback, LV_EVENT_CLICKED, this);

    status_bar_ = box(screen, 240, 0, 320, 48);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, 310);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(status_label_);
    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, 310);
    lv_label_set_text(notification_label_, "");
    lv_obj_set_style_text_color(notification_label_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(notification_label_);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    bottom_bar_ = box(screen, 0, 413, 800, 67);
    lv_obj_set_style_bg_color(bottom_bar_, lv_color_hex(kChrome), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_bar_, 1, 0);
    lv_obj_set_style_border_side(bottom_bar_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bottom_bar_, lv_color_hex(kBlue), 0);
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_obj_set_width(chat_message_label_, 744);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_style_text_color(chat_message_label_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(chat_message_label_);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

    low_battery_popup_ = box(screen, 140, 340, 520, 64);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_hex(0xB62045), 0);
    lv_obj_set_style_bg_opa(low_battery_popup_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(low_battery_popup_, 8, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    UpdateCyberExitButtonVisibility();
}

void LcdDisplay::CyberLogoRotationAnimCallback(void* object, int32_t value) {
    if (object != nullptr) {
        lv_obj_set_style_transform_rotation(static_cast<lv_obj_t*>(object), value, 0);
    }
}

void LcdDisplay::CyberLogoScaleAnimCallback(void* object, int32_t value) {
    if (object != nullptr) {
        lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(object), value, 0);
    }
}

void LcdDisplay::StartCyberLogoAnimation() {
    if (emoji_box_ == nullptr || cyber_logo_animation_running_) {
        return;
    }

    lv_anim_t rotation;
    lv_anim_init(&rotation);
    lv_anim_set_var(&rotation, emoji_box_);
    lv_anim_set_exec_cb(&rotation, CyberLogoRotationAnimCallback);
    lv_anim_set_values(&rotation, -24, 24);
    lv_anim_set_duration(&rotation, 3400);
    lv_anim_set_reverse_duration(&rotation, 3400);
    lv_anim_set_repeat_count(&rotation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&rotation, lv_anim_path_ease_in_out);
    lv_anim_start(&rotation);

    lv_anim_t scale;
    lv_anim_init(&scale);
    lv_anim_set_var(&scale, emoji_box_);
    lv_anim_set_exec_cb(&scale, CyberLogoScaleAnimCallback);
    lv_anim_set_values(&scale, 248, 263);
    lv_anim_set_duration(&scale, 2700);
    lv_anim_set_reverse_duration(&scale, 2700);
    lv_anim_set_repeat_count(&scale, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&scale, lv_anim_path_ease_in_out);
    lv_anim_start(&scale);

    cyber_logo_animation_running_ = true;
}

void LcdDisplay::StopCyberLogoAnimation() {
    if (emoji_box_ == nullptr) {
        return;
    }
    lv_anim_delete(emoji_box_, CyberLogoRotationAnimCallback);
    lv_anim_delete(emoji_box_, CyberLogoScaleAnimCallback);
    lv_obj_set_style_transform_rotation(emoji_box_, 0, 0);
    lv_obj_set_style_transform_scale(emoji_box_, 256, 0);
    cyber_logo_animation_running_ = false;
}

void LcdDisplay::UpdateCyberExitButtonVisibility() {
    if (cyber_exit_button_ == nullptr) {
        return;
    }
    DeviceState state = Application::GetInstance().GetDeviceState();
    bool voice_active = state == kDeviceStateConnecting || state == kDeviceStateListening ||
                        state == kDeviceStateSpeaking;
    UpdateSettingsShortcutVisibility();
    if (voice_active) {
        lv_obj_remove_state(cyber_exit_button_, LV_STATE_DISABLED);
        lv_obj_remove_flag(cyber_exit_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(cyber_exit_button_, -1);
        StartCyberLogoAnimation();
    } else {
        lv_obj_add_flag(cyber_exit_button_, LV_OBJ_FLAG_HIDDEN);
        StopCyberLogoAnimation();
    }
}

void LcdDisplay::CyberExitEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display == nullptr || display->cyber_exit_button_ == nullptr) {
        return;
    }
    lv_obj_add_state(display->cyber_exit_button_, LV_STATE_DISABLED);
    Application::GetInstance().Schedule([]() { Application::GetInstance().EndVoiceInteraction(); });
}
void LcdDisplay::CreateStandbyDashboard() {
    if (standby_dashboard_ != nullptr) {
        return;
    }
    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    standby_dashboard_ = std::make_unique<StandbyDashboard>(
        lv_screen_active(), lvgl_theme->text_font()->font(), lvgl_theme->icon_font()->font());
    if (low_battery_popup_ != nullptr) {
        lv_obj_move_to_index(low_battery_popup_, -1);
    }
    UpdateStandbyDashboardVisibility();
}

void LcdDisplay::UpdateStandbyDashboardVisibility() {
    if (standby_dashboard_ == nullptr) {
        return;
    }
    bool should_show = Application::GetInstance().GetDeviceState() == kDeviceStateIdle ||
                       standby_dashboard_->IsPomodoroActive();
    bool was_visible = standby_dashboard_->IsVisible();
    standby_dashboard_->SetVisible(should_show);
    UpdateSettingsShortcutVisibility();
    if (should_show && !was_visible) {
        standby_dashboard_->Update(true);
        if (low_battery_popup_ != nullptr) {
            lv_obj_move_to_index(low_battery_popup_, -1);
        }
    }
}

void LcdDisplay::UpdateSettingsShortcutVisibility() {
    if (standby_dashboard_ == nullptr)
        return;
    bool binding_visible = false;
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    binding_visible =
        binding_panel_ != nullptr && !lv_obj_has_flag(binding_panel_, LV_OBJ_FLAG_HIDDEN);
#endif
    standby_dashboard_->SetSettingsShortcutVisible(binding_visible ||
                                                   Application::GetInstance().GetDeviceState() ==
                                                       kDeviceStateWifiConfiguring);
}

void LcdDisplay::UpdateStandbyDashboard(bool force) {
    UpdateStandbyDashboardVisibility();
    if (standby_dashboard_ != nullptr && standby_dashboard_->IsVisible()) {
        standby_dashboard_->Update(force);
    }
}
#endif

void LcdDisplay::SetTopBarVisible(bool visible) {
    if (top_bar_ != nullptr) {
        if (visible) {
            lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_to_index(top_bar_, -1);
        } else {
            lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (status_bar_ != nullptr) {
        if (visible) {
            lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_to_index(status_bar_, -1);
        } else {
            lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void LcdDisplay::ShowTopBarTemporarily() {
    SetTopBarVisible(true);
    if (top_bar_auto_hide_timer_ != nullptr) {
        esp_timer_stop(top_bar_auto_hide_timer_);
        esp_timer_start_once(top_bar_auto_hide_timer_, kTopBarAutoHideUs);
    }
}

void LcdDisplay::AttachTopBarTouchTarget(lv_obj_t* object) {
    if (object == nullptr) {
        return;
    }
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, TopBarTouchEventCallback, LV_EVENT_PRESSED, this);
}

void LcdDisplay::TopBarTouchEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->ShowTopBarTemporarily();
    }
}

#if CONFIG_XIAOZHI_DEVICE_CONTROL
void LcdDisplay::CreateDeviceBindingUi() {
    // Use the top layer so chat, images and emotion changes cannot cover this view.
    auto* layer = lv_display_get_layer_top(display_);
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    auto* font = theme->text_font()->font();
    binding_panel_ = lv_obj_create(layer);
    lv_obj_set_size(binding_panel_, width_, height_);
    lv_obj_set_style_radius(binding_panel_, 0, 0);
    lv_obj_set_style_border_width(binding_panel_, 0, 0);
    lv_obj_set_style_bg_color(binding_panel_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(binding_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(binding_panel_, lv_color_black(), 0);
    lv_obj_set_style_text_font(binding_panel_, font, 0);
    lv_obj_set_style_pad_all(binding_panel_, 12, 0);
    lv_obj_set_style_pad_top(binding_panel_, 44, 0);
    lv_obj_set_style_pad_row(binding_panel_, 8, 0);
    lv_obj_set_flex_flow(binding_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(binding_panel_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(binding_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(binding_panel_, LV_OBJ_FLAG_HIDDEN);

    binding_title_ = lv_label_create(binding_panel_);
    lv_obj_set_width(binding_title_, LV_PCT(100));
    lv_obj_set_style_text_align(binding_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(binding_title_, "正在连接，请稍候");

#if LV_USE_QRCODE
    binding_qr_frame_ = lv_obj_create(binding_panel_);
    lv_obj_set_style_radius(binding_qr_frame_, 0, 0);
    lv_obj_set_style_border_width(binding_qr_frame_, 0, 0);
    lv_obj_set_style_pad_all(binding_qr_frame_, 0, 0);
    lv_obj_set_style_bg_color(binding_qr_frame_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(binding_qr_frame_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(binding_qr_frame_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(binding_qr_frame_, LV_OBJ_FLAG_HIDDEN);
    binding_qr_ = lv_qrcode_create(binding_qr_frame_);
    lv_qrcode_set_size(binding_qr_, 1);
    lv_qrcode_set_dark_color(binding_qr_, lv_color_black());
    lv_qrcode_set_light_color(binding_qr_, lv_color_white());
    // The enclosing white frame provides an exact four-module quiet zone.
    lv_qrcode_set_quiet_zone(binding_qr_, false);
    lv_obj_center(binding_qr_);
#endif

    binding_detail_ = lv_label_create(binding_panel_);
    lv_obj_set_width(binding_detail_, LV_PCT(100));
    lv_obj_set_style_text_align(binding_detail_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(binding_detail_, "");

    // This entry remains reachable even when the normal top bar auto-hides.
    account_button_ = lv_button_create(layer);
    lv_obj_set_size(account_button_, 76, 32);
    lv_obj_align(account_button_, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_text_font(account_button_, font, 0);
    lv_obj_add_state(account_button_, LV_STATE_DISABLED);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    if (standby_dashboard_ != nullptr)
        lv_obj_add_flag(account_button_, LV_OBJ_FLAG_HIDDEN);
#endif
    auto* account_label = lv_label_create(account_button_);
    lv_label_set_text(account_label, "账号");
    lv_obj_center(account_label);
    lv_obj_add_event_cb(
        account_button_,
        [](lv_event_t* event) {
            auto* self = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
            if (self->account_action_) {
                // The action only signals the control task; it must not perform HTTP here.
                self->account_action_();
            }
        },
        LV_EVENT_CLICKED, this);

    binding_close_button_ = lv_button_create(layer);
    lv_obj_set_size(binding_close_button_, 76, 32);
    lv_obj_align(binding_close_button_, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_set_style_text_font(binding_close_button_, font, 0);
    lv_obj_add_flag(binding_close_button_, LV_OBJ_FLAG_HIDDEN);
    auto* close_label = lv_label_create(binding_close_button_);
    lv_label_set_text(close_label, "返回");
    lv_obj_center(close_label);
    lv_obj_add_event_cb(
        binding_close_button_, [](lv_event_t*) { DeviceControl::GetInstance().CloseAccount(); },
        LV_EVENT_CLICKED, nullptr);

    binding_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self = static_cast<LcdDisplay*>(lv_timer_get_user_data(timer));
            self->UpdateBindingCountdown();
        },
        100, this);
    lv_timer_pause(binding_timer_);
}

void LcdDisplay::UpdateBindingCountdown() {
    const int64_t remaining_ms = binding_deadline_ms_ - esp_timer_get_time() / 1000;
    if (remaining_ms <= 0) {
        if (binding_qr_frame_ != nullptr) {
            lv_obj_add_flag(binding_qr_frame_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(binding_title_, "二维码已过期，正在更新");
        lv_label_set_text(binding_detail_, "请等待新的二维码");
        binding_seconds_ = -1;
        lv_timer_pause(binding_timer_);
        return;
    }
    const int64_t seconds = (remaining_ms + 999) / 1000;
    if (seconds != binding_seconds_) {
        binding_seconds_ = seconds;
        char countdown[128];
        snprintf(countdown, sizeof(countdown), "请在手机上完成操作  %02lld:%02lld",
                 static_cast<long long>(seconds / 60), static_cast<long long>(seconds % 60));
        lv_label_set_text(binding_detail_, countdown);
    }
}
#endif

void LcdDisplay::SetAccountAction(std::function<void()> action) {
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    DisplayLockGuard lock(this);
    account_action_ = std::move(action);
#else
    (void)action;
#endif
}

void LcdDisplay::ShowDeviceBinding(const DeviceControlView& view) {
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (binding_panel_ == nullptr) {
        return;
    }
    const bool can_open_account = view.reason == "ready" || view.reason == "user_signed_out" ||
                                  view.reason == "market_auth_required" ||
                                  view.reason == "market_auth_pending";
    if (can_open_account && account_action_) {
        lv_obj_remove_state(account_button_, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(account_button_, LV_STATE_DISABLED);
    }
    lv_timer_pause(binding_timer_);
    lv_obj_add_flag(binding_close_button_, LV_OBJ_FLAG_HIDDEN);
    bool wifi_provisioning = false;
    bool wifi_off = false;
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    const auto wifi = Board::GetInstance().GetWifiLinkView().state;
    wifi_provisioning = wifi == WifiLinkState::Provisioning;
    wifi_off = wifi == WifiLinkState::Off;
#endif
    if (wifi_provisioning || (view.voice_allowed && view.verification_uri.empty())) {
        lv_obj_add_flag(binding_panel_, LV_OBJ_FLAG_HIDDEN);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        UpdateSettingsShortcutVisibility();
#endif
        binding_uri_.clear();
        binding_purpose_.clear();
        binding_qr_valid_ = false;
        return;
    }

    lv_obj_remove_flag(binding_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(account_button_, -1);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    UpdateSettingsShortcutVisibility();
#endif
    if (view.voice_allowed && view.purpose == "account")
        lv_obj_remove_flag(binding_close_button_, LV_OBJ_FLAG_HIDDEN);
    if (binding_qr_frame_ != nullptr) {
        lv_obj_add_flag(binding_qr_frame_, LV_OBJ_FLAG_HIDDEN);
    }
    const char* title = "正在连接，请稍候";
    const char* detail = "连接后将自动更新";
    if (wifi_off) {
        title = "Wi-Fi 已断开";
        detail = "可在设置中重新连接";
    } else if (view.sign_out_pending) {
        title = "正在退出本台音箱";
        detail = "请稍候";
    } else if (view.reason == "unbound") {
        title = "扫码登录并绑定";
    } else if (view.reason == "user_signed_out") {
        title = "扫码恢复本台音箱";
        detail = "请使用原归属账号登录";
    } else if (view.reason == "market_auth_required") {
        title = "请原账号扫码登录";
    } else if (view.reason == "market_auth_pending") {
        title = "授权待确认，请扫码";
        detail = "请使用原归属账号重新授权";
    } else if (view.reason == "device_disabled") {
        title = "设备已停用";
        detail = "请联系维护人员";
    } else if (view.reason == "not_configured") {
        title = "设备尚未配置";
        detail = "请联系维护人员";
    } else if (view.reason == "device_auth_required") {
        title = "设备认证失败";
        detail = "请联系维护人员检查设备凭据";
    } else if (view.reason == "tls_error" || view.reason == "invalid_state" ||
               view.reason == "invalid_response") {
        title = "连接信息异常";
        detail = "请联系维护人员检查连接配置";
    }
    const bool can_show_qr =
        !wifi_off &&
        (view.reason == "unbound" || view.reason == "ready" || view.reason == "user_signed_out" ||
         view.reason == "market_auth_required" || view.reason == "market_auth_pending");
    if (can_show_qr && view.purpose == "account") {
        title = "账号与绑定";
        detail = "请使用原归属账号登录";
    }
    lv_label_set_text(binding_title_, title);
    lv_label_set_text(binding_detail_, detail);

    if (!can_show_qr || view.verification_uri.empty()) {
        binding_uri_.clear();
        binding_purpose_.clear();
        binding_qr_valid_ = false;
        return;
    }

    const bool uri_valid = view.verification_uri.size() <= 512 &&
                           view.verification_uri.rfind("https://", 0) == 0 &&
                           std::all_of(view.verification_uri.begin(), view.verification_uri.end(),
                                       [](unsigned char ch) { return ch >= 0x21 && ch <= 0x7e; });
    if (!uri_valid) {
        lv_label_set_text(binding_title_, "暂时无法显示二维码");
        lv_label_set_text(binding_detail_, "请稍后重试");
        binding_qr_valid_ = false;
        return;
    }

    if (binding_uri_ != view.verification_uri || binding_purpose_ != view.purpose) {
        binding_uri_ = view.verification_uri;
        binding_purpose_ = view.purpose;
#if LV_USE_QRCODE
        const int version = qrcodegen_getMinFitVersion(qrcodegen_Ecc_MEDIUM, binding_uri_.size());
        const int modules = version > 0 ? qrcodegen_version2size(version) : 0;
        const int available_size = std::min(width_ - 32, height_ - 128);
        const int scale = modules > 0 ? available_size / (modules + 8) : 0;
        binding_qr_valid_ = false;
        if (scale > 0) {
            const int frame_size = (modules + 8) * scale;
            lv_obj_set_size(binding_qr_frame_, frame_size, frame_size);
            lv_qrcode_set_size(binding_qr_, modules * scale);
            lv_obj_center(binding_qr_);
            binding_qr_valid_ = lv_qrcode_update(binding_qr_, binding_uri_.data(),
                                                 binding_uri_.size()) == LV_RESULT_OK;
        }
#endif
    }
    if (!binding_qr_valid_) {
        lv_label_set_text(binding_title_, "暂时无法显示二维码");
        lv_label_set_text(binding_detail_, "请联系维护人员");
        return;
    }

    binding_deadline_ms_ = view.qr_deadline_ms;
    binding_seconds_ = -1;
    if (binding_deadline_ms_ > esp_timer_get_time() / 1000 && binding_qr_frame_ != nullptr) {
        lv_obj_remove_flag(binding_qr_frame_, LV_OBJ_FLAG_HIDDEN);
        lv_timer_reset(binding_timer_);
        lv_timer_resume(binding_timer_);
    }
    UpdateBindingCountdown();
#else
    (void)view;
#endif
}

#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
namespace {
constexpr const char* kLichuangViewNames[] = {
    "Home",
    "Pomodoro",
    "Music",
    "Recorder",
};

constexpr const char* kLichuangNavLabels[] = {
    "Home",
    "Timer",
    "Music",
    "Record",
};

constexpr uint32_t kLichuangTechBg = 0x06111F;
constexpr uint32_t kLichuangTechPanel = 0x0B1F33;
constexpr uint32_t kLichuangTechButton = 0x102A44;
constexpr uint32_t kLichuangTechBorder = 0x164E63;
constexpr uint32_t kLichuangTechTrack = 0x0F172A;
constexpr uint32_t kLichuangTechText = 0xE0F2FE;
constexpr uint32_t kLichuangTechMuted = 0x7DD3FC;
constexpr uint32_t kLichuangTechCyan = 0x22D3EE;
constexpr uint32_t kLichuangTechBlue = 0x38BDF8;
constexpr uint32_t kLichuangTechGreen = 0x34D399;
constexpr uint32_t kLichuangTechRed = 0xF43F5E;
constexpr uint32_t kLichuangTechAmber = 0xF59E0B;
constexpr int64_t kLichuangPomodoroVoiceOverrideUs = 30 * 1000 * 1000;

lv_obj_t* CreatePlainLabel(lv_obj_t* parent, const char* text, lv_coord_t width,
        lv_text_align_t align = LV_TEXT_ALIGN_CENTER) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

lv_obj_t* CreateInfoPanel(lv_obj_t* parent, LvglTheme* theme) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(panel, 3, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(kLichuangTechBorder), 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(kLichuangTechPanel), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(panel, lv_color_hex(kLichuangTechText), 0);
    lv_obj_set_style_pad_all(panel, theme->spacing(2), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, theme->spacing(1), 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    return panel;
}

void ApplyButtonColors(lv_obj_t* button, lv_color_t bg_color, lv_color_t border_color,
        lv_color_t text_color, lv_opa_t bg_opa = LV_OPA_90) {
    if (button == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(button, bg_color, 0);
    lv_obj_set_style_bg_opa(button, bg_opa, 0);
    lv_obj_set_style_border_color(button, border_color, 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, text_color, 0);
    }
}

lv_obj_t* CreateStyledButton(lv_obj_t* parent, LvglTheme* theme, const char* text,
        lv_coord_t width, lv_coord_t height, lv_color_t bg_color, lv_color_t text_color) {
    (void)theme;
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 3, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    ApplyButtonColors(button, bg_color, lv_color_hex(kLichuangTechBorder), text_color);
    return button;
}

lv_obj_t* CreateSmallButton(lv_obj_t* parent, LvglTheme* theme, const char* text) {
    return CreateStyledButton(parent, theme, text, 62, 30,
        lv_color_hex(kLichuangTechButton), lv_color_hex(kLichuangTechText));
}

lv_obj_t* CreateAccentButton(lv_obj_t* parent, LvglTheme* theme, const char* text,
        lv_coord_t width, lv_color_t accent_color) {
    return CreateStyledButton(parent, theme, text, width, 30,
        accent_color, lv_color_hex(0xFFFFFF));
}

lv_obj_t* CreateNavButton(lv_obj_t* parent, LvglTheme* theme, const char* text) {
    return CreateStyledButton(parent, theme, text, 70, 32,
        lv_color_hex(kLichuangTechButton), lv_color_hex(kLichuangTechMuted));
}

lv_obj_t* CreateProgressBar(lv_obj_t* parent, LvglTheme* theme, lv_color_t indicator_color) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, LV_HOR_RES - theme->spacing(36), 8);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kLichuangTechTrack), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, indicator_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    return bar;
}

void SetObjectHidden(lv_obj_t* object, bool hidden) {
    if (object == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

void SetBarIndicatorColor(lv_obj_t* bar, lv_color_t color) {
    if (bar != nullptr) {
        lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    }
}

void SetButtonText(lv_obj_t* button, const char* text) {
    if (button == nullptr) {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (label != nullptr) {
        lv_label_set_text(label, text);
        lv_obj_center(label);
    }
}

void SetBarProgress(lv_obj_t* bar, int elapsed_seconds, int duration_seconds) {
    if (bar == nullptr) {
        return;
    }
    int value = 0;
    if (duration_seconds > 0 && elapsed_seconds > 0) {
        value = std::min(1000, std::max(0, elapsed_seconds * 1000 / duration_seconds));
    }
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
}

void FormatDuration(char* buffer, size_t size, int seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    int minutes = seconds / 60;
    int remaining_seconds = seconds % 60;
    snprintf(buffer, size, "%02d:%02d", minutes, remaining_seconds);
}
} // namespace

void LcdDisplay::CreateLichuangFeatureShell() {
    if (lichuang_feature_shell_ != nullptr) {
        return;
    }

    LvglTheme* theme = static_cast<LvglTheme*>(current_theme_);
    lv_coord_t top_offset = 0;
    lv_coord_t shell_height = LV_VER_RES;
    lv_coord_t nav_height = 38;
    lv_coord_t page_height = shell_height - nav_height - theme->spacing(8);

    lichuang_feature_shell_ = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(lichuang_feature_shell_, 0, top_offset);
    lv_obj_set_size(lichuang_feature_shell_, LV_HOR_RES, shell_height);
    lv_obj_set_style_radius(lichuang_feature_shell_, 0, 0);
    lv_obj_set_style_border_width(lichuang_feature_shell_, 0, 0);
    lv_obj_set_style_pad_all(lichuang_feature_shell_, theme->spacing(2), 0);
    lv_obj_set_style_bg_color(lichuang_feature_shell_, lv_color_hex(kLichuangTechBg), 0);
    lv_obj_set_style_text_color(lichuang_feature_shell_, lv_color_hex(kLichuangTechText), 0);
    lv_obj_set_scrollbar_mode(lichuang_feature_shell_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(lichuang_feature_shell_, LV_DIR_NONE);
    lv_obj_add_flag(lichuang_feature_shell_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(lichuang_feature_shell_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(lichuang_feature_shell_, LichuangFeatureGestureEventCallback,
        LV_EVENT_GESTURE, this);
    AttachTopBarTouchTarget(lichuang_feature_shell_);

    auto create_view = [this, theme, page_height]() {
        lv_obj_t* page = lv_obj_create(lichuang_feature_shell_);
        lv_obj_set_size(page, LV_HOR_RES - theme->spacing(4), page_height);
        lv_obj_align(page, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(page, 0, 0);
        lv_obj_set_style_border_width(page, 0, 0);
        lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(page, theme->spacing(2), 0);
        lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
        AttachTopBarTouchTarget(page);
        return page;
    };

    const lv_coord_t content_width = LV_HOR_RES - theme->spacing(24);

    lv_obj_t* home = create_view();
    lv_obj_t* home_time_panel = CreateInfoPanel(home, theme);
    AttachTopBarTouchTarget(home_time_panel);
    lichuang_home_time_label_ = CreatePlainLabel(home_time_panel, "--:--", content_width);
    lv_obj_set_style_text_color(lichuang_home_time_label_, lv_color_hex(kLichuangTechCyan), 0);
    lichuang_home_date_label_ = CreatePlainLabel(home_time_panel, "Date not set", content_width);
    lichuang_home_weekday_label_ = CreatePlainLabel(home_time_panel, "Waiting for network time", content_width);

    lv_obj_t* env_panel = CreateInfoPanel(home, theme);
    AttachTopBarTouchTarget(env_panel);
    lichuang_home_weather_label_ = CreatePlainLabel(env_panel, "Weather: unavailable", content_width);
    lichuang_home_air_label_ = CreatePlainLabel(env_panel, "AQI: unavailable", content_width);

    lv_obj_t* pomodoro = create_view();
    lv_obj_t* pomo_panel = CreateInfoPanel(pomodoro, theme);
    AttachTopBarTouchTarget(pomo_panel);
    lichuang_pomodoro_mode_label_ = CreatePlainLabel(pomo_panel, "READY", content_width);
    lv_obj_set_style_text_color(lichuang_pomodoro_mode_label_, lv_color_hex(kLichuangTechCyan), 0);
    lichuang_pomodoro_remaining_label_ = CreatePlainLabel(pomo_panel, "00:00", content_width);
    lv_obj_set_style_text_color(lichuang_pomodoro_remaining_label_, lv_color_hex(kLichuangTechCyan), 0);
    lichuang_pomodoro_progress_bar_ = CreateProgressBar(pomo_panel, theme, lv_color_hex(kLichuangTechCyan));
    lichuang_pomodoro_status_label_ = CreatePlainLabel(pomo_panel, "Pick a focus length", content_width);
    lichuang_pomodoro_preset_label_ = CreatePlainLabel(pomo_panel, "Quick start", content_width);
    lv_obj_t* preset_row = lv_obj_create(pomo_panel);
    lichuang_pomodoro_preset_row_ = preset_row;
    lv_obj_set_size(preset_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(preset_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(preset_row, 0, 0);
    lv_obj_set_style_pad_all(preset_row, 0, 0);
    lv_obj_set_style_pad_column(preset_row, theme->spacing(2), 0);
    lv_obj_set_flex_flow(preset_row, LV_FLEX_FLOW_ROW);
    lv_obj_t* preset_15 = CreateAccentButton(preset_row, theme, "15m", 58, lv_color_hex(kLichuangTechCyan));
    lv_obj_t* preset_30 = CreateAccentButton(preset_row, theme, "30m", 58, lv_color_hex(kLichuangTechCyan));
    lv_obj_t* preset_45 = CreateAccentButton(preset_row, theme, "45m", 58, lv_color_hex(kLichuangTechCyan));
    AttachTopBarTouchTarget(preset_15);
    AttachTopBarTouchTarget(preset_30);
    AttachTopBarTouchTarget(preset_45);
    lv_obj_set_user_data(preset_15, this);
    lv_obj_set_user_data(preset_30, this);
    lv_obj_set_user_data(preset_45, this);
    lv_obj_add_event_cb(preset_15, LichuangPomodoroPresetEventCallback,
        LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(15)));
    lv_obj_add_event_cb(preset_30, LichuangPomodoroPresetEventCallback,
        LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(30)));
    lv_obj_add_event_cb(preset_45, LichuangPomodoroPresetEventCallback,
        LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(45)));

    lv_obj_t* pomodoro_control_row = lv_obj_create(pomo_panel);
    lv_obj_set_size(pomodoro_control_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pomodoro_control_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pomodoro_control_row, 0, 0);
    lv_obj_set_style_pad_all(pomodoro_control_row, 0, 0);
    lv_obj_set_style_pad_column(pomodoro_control_row, theme->spacing(2), 0);
    lv_obj_set_flex_flow(pomodoro_control_row, LV_FLEX_FLOW_ROW);
    lv_obj_t* pause_resume = CreateAccentButton(pomodoro_control_row, theme, "Pause", 76,
        lv_color_hex(kLichuangTechBlue));
    lichuang_pomodoro_pause_button_ = pause_resume;
    lv_obj_t* cancel = CreateAccentButton(pomodoro_control_row, theme, "Cancel", 76,
        lv_color_hex(kLichuangTechAmber));
    AttachTopBarTouchTarget(pause_resume);
    AttachTopBarTouchTarget(cancel);
    lv_obj_set_user_data(pause_resume, this);
    lv_obj_set_user_data(cancel, this);
    lv_obj_add_event_cb(pause_resume, LichuangPomodoroPauseResumeEventCallback,
        LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(cancel, LichuangPomodoroCancelEventCallback,
        LV_EVENT_CLICKED, this);

    lv_obj_t* music = create_view();
    lv_obj_t* music_panel = CreateInfoPanel(music, theme);
    AttachTopBarTouchTarget(music_panel);
    lichuang_music_status_label_ = CreatePlainLabel(music_panel, "Player idle", content_width);
    lv_obj_set_style_text_color(lichuang_music_status_label_, lv_color_hex(kLichuangTechBlue), 0);
    lichuang_music_track_label_ = CreatePlainLabel(music_panel, "Playlist: no scan yet", content_width);
    lichuang_music_progress_bar_ = CreateProgressBar(music_panel, theme, lv_color_hex(kLichuangTechBlue));
    lichuang_music_detail_label_ = CreatePlainLabel(music_panel, "00:00/00:00  -- Hz", content_width);
    lichuang_music_list_label_ = CreatePlainLabel(music_panel, "Next: --", content_width);
    lv_obj_t* control_row = lv_obj_create(music_panel);
    lv_obj_set_size(control_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(control_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control_row, 0, 0);
    lv_obj_set_style_pad_all(control_row, 0, 0);
    lv_obj_set_style_pad_column(control_row, theme->spacing(2), 0);
    lv_obj_set_flex_flow(control_row, LV_FLEX_FLOW_ROW);
    lv_obj_t* previous = CreateSmallButton(control_row, theme, "Prev");
    lv_obj_t* play_pause = CreateAccentButton(control_row, theme, "Play", 70,
        lv_color_hex(kLichuangTechBlue));
    lichuang_music_play_button_ = play_pause;
    lv_obj_t* next = CreateSmallButton(control_row, theme, "Next");
    AttachTopBarTouchTarget(previous);
    AttachTopBarTouchTarget(play_pause);
    AttachTopBarTouchTarget(next);
    lv_obj_add_event_cb(previous, LichuangMusicPreviousEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(play_pause, LichuangMusicPlayPauseEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(next, LichuangMusicNextEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* recorder = create_view();
    lv_obj_t* recorder_panel = CreateInfoPanel(recorder, theme);
    AttachTopBarTouchTarget(recorder_panel);
    lichuang_recorder_status_label_ = CreatePlainLabel(recorder_panel, "Ready to record", content_width);
    lv_obj_set_style_text_color(lichuang_recorder_status_label_, lv_color_hex(kLichuangTechRed), 0);
    lichuang_recorder_progress_bar_ = CreateProgressBar(recorder_panel, theme, lv_color_hex(kLichuangTechRed));
    lichuang_recorder_detail_label_ = CreatePlainLabel(recorder_panel, "Elapsed 00:00, free -- MB", content_width);
    lichuang_recorder_file_label_ = CreatePlainLabel(recorder_panel, "Output: /sdcard/recordings", content_width);
    lv_obj_t* recorder_row = lv_obj_create(recorder_panel);
    lv_obj_set_size(recorder_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(recorder_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(recorder_row, 0, 0);
    lv_obj_set_style_pad_all(recorder_row, 0, 0);
    lv_obj_set_style_pad_column(recorder_row, theme->spacing(2), 0);
    lv_obj_set_flex_flow(recorder_row, LV_FLEX_FLOW_ROW);
    lv_obj_t* rec = CreateAccentButton(recorder_row, theme, "Start", 78,
        lv_color_hex(kLichuangTechRed));
    lichuang_recorder_start_button_ = rec;
    lv_obj_t* rec_pause = CreateAccentButton(recorder_row, theme, "Pause", 78,
        lv_color_hex(kLichuangTechBlue));
    lichuang_recorder_pause_button_ = rec_pause;
    lv_obj_t* rec_stop = CreateAccentButton(recorder_row, theme, "Stop", 78,
        lv_color_hex(kLichuangTechAmber));
    lichuang_recorder_stop_button_ = rec_stop;
    AttachTopBarTouchTarget(rec);
    AttachTopBarTouchTarget(rec_pause);
    AttachTopBarTouchTarget(rec_stop);
    lv_obj_add_event_cb(rec, LichuangRecorderStartEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(rec_pause, LichuangRecorderPauseResumeEventCallback, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(rec_stop, LichuangRecorderStopEventCallback, LV_EVENT_CLICKED, this);

    lv_obj_t* nav_row = lv_obj_create(lichuang_feature_shell_);
    lv_obj_set_size(nav_row, LV_HOR_RES - theme->spacing(4), nav_height);
    lv_obj_align(nav_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(nav_row, 0, 0);
    lv_obj_set_style_bg_opa(nav_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nav_row, 0, 0);
    lv_obj_set_style_pad_all(nav_row, 0, 0);
    lv_obj_set_style_pad_column(nav_row, theme->spacing(1), 0);
    lv_obj_set_flex_flow(nav_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < static_cast<int>(LichuangFeatureView::kCount); ++i) {
        lv_obj_t* nav_button = CreateNavButton(nav_row, theme, kLichuangNavLabels[i]);
        AttachTopBarTouchTarget(nav_button);
        lv_obj_set_user_data(nav_button, this);
        lv_obj_add_event_cb(nav_button, LichuangNavEventCallback, LV_EVENT_CLICKED,
            reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        lichuang_nav_buttons_[i] = nav_button;
    }

    lichuang_page_label_ = CreatePlainLabel(lichuang_feature_shell_, "Home 1/4",
        LV_HOR_RES - theme->spacing(8));
    lv_obj_add_flag(lichuang_page_label_, LV_OBJ_FLAG_HIDDEN);

    lichuang_feature_views_[static_cast<int>(LichuangFeatureView::kHome)] = home;
    lichuang_feature_views_[static_cast<int>(LichuangFeatureView::kPomodoro)] = pomodoro;
    lichuang_feature_views_[static_cast<int>(LichuangFeatureView::kMusic)] = music;
    lichuang_feature_views_[static_cast<int>(LichuangFeatureView::kRecorder)] = recorder;
    SetLichuangFeatureView(LichuangFeatureView::kHome);
}

void LcdDisplay::ShowLichuangFeatureShell() {
    if (lichuang_feature_shell_ == nullptr) {
        return;
    }

    lv_obj_remove_flag(lichuang_feature_shell_, LV_OBJ_FLAG_HIDDEN);
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (preview_image_ != nullptr) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    UpdateLichuangHomeView(true);
    if (lichuang_feature_view_index_ == static_cast<int>(LichuangFeatureView::kPomodoro)) {
        UpdateLichuangPomodoroView(false);
    } else if (lichuang_feature_view_index_ == static_cast<int>(LichuangFeatureView::kMusic)) {
        UpdateLichuangMusicView(false);
    } else if (lichuang_feature_view_index_ == static_cast<int>(LichuangFeatureView::kRecorder)) {
        UpdateLichuangRecorderView(false);
    }
}

void LcdDisplay::HideLichuangFeatureShell() {
    if (lichuang_feature_shell_ != nullptr) {
        lv_obj_add_flag(lichuang_feature_shell_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void LcdDisplay::UpdateLichuangFeatureShellVisibility() {
    if (lichuang_feature_shell_ == nullptr) {
        return;
    }

    bool forced_visible = false;
    if (lichuang_feature_shell_override_until_us_ > 0) {
        int64_t now_us = esp_timer_get_time();
        if (now_us <= lichuang_feature_shell_override_until_us_) {
            forced_visible = true;
        } else {
            lichuang_feature_shell_override_until_us_ = 0;
        }
    }

    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle || forced_visible) {
        ShowLichuangFeatureShell();
    } else {
        HideLichuangFeatureShell();
    }
}

bool LcdDisplay::IsLichuangFeatureShellVisible() const {
    return lichuang_feature_shell_ != nullptr &&
        !lv_obj_has_flag(lichuang_feature_shell_, LV_OBJ_FLAG_HIDDEN);
}

void LcdDisplay::SwitchLichuangFeatureView(int delta) {
    int view_count = static_cast<int>(LichuangFeatureView::kCount);
    int next = (lichuang_feature_view_index_ + delta + view_count) % view_count;
    SetLichuangFeatureView(static_cast<LichuangFeatureView>(next));
}

void LcdDisplay::SetLichuangFeatureView(LichuangFeatureView view) {
    int next = static_cast<int>(view);
    int view_count = static_cast<int>(LichuangFeatureView::kCount);
    if (next < 0 || next >= view_count) {
        return;
    }

    for (int i = 0; i < view_count; ++i) {
        lv_obj_t* page = lichuang_feature_views_[i];
        if (page == nullptr) {
            continue;
        }
        if (i == next) {
            lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (int i = 0; i < view_count; ++i) {
        lv_obj_t* nav_button = lichuang_nav_buttons_[i];
        if (nav_button == nullptr) {
            continue;
        }
        if (i == next) {
            ApplyButtonColors(nav_button, lv_color_hex(kLichuangTechCyan),
                lv_color_hex(kLichuangTechCyan), lv_color_hex(kLichuangTechBg), LV_OPA_COVER);
        } else {
            ApplyButtonColors(nav_button, lv_color_hex(kLichuangTechButton),
                lv_color_hex(kLichuangTechBorder), lv_color_hex(kLichuangTechMuted), LV_OPA_COVER);
        }
    }

    lichuang_feature_view_index_ = next;
    if (lichuang_page_label_ != nullptr) {
        char label[32];
        snprintf(label, sizeof(label), "%s %d/%d",
            kLichuangViewNames[next], next + 1, view_count);
        lv_label_set_text(lichuang_page_label_, label);
    }

    if (view == LichuangFeatureView::kHome) {
        UpdateLichuangHomeView(true);
    } else if (view == LichuangFeatureView::kPomodoro) {
        UpdateLichuangPomodoroView(true);
    } else if (view == LichuangFeatureView::kMusic) {
        UpdateLichuangMusicView(true);
    } else if (view == LichuangFeatureView::kRecorder) {
        UpdateLichuangRecorderView(true);
    }

    ESP_LOGI(TAG, "Lichuang feature view: %s", kLichuangViewNames[next]);
}

void LcdDisplay::UpdateLichuangHomeView(bool force) {
    if (lichuang_home_time_label_ == nullptr) {
        return;
    }

    time_t now = time(nullptr);
    time_t current_minute = now / 60;
    if (force || current_minute != lichuang_last_home_update_minute_) {
        lichuang_last_home_update_minute_ = current_minute;

        struct tm timeinfo = {};
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= 2025 - 1900) {
            char time_text[16];
            char date_text[24];
            strftime(time_text, sizeof(time_text), "%H:%M", &timeinfo);
            strftime(date_text, sizeof(date_text), "%Y-%m-%d", &timeinfo);
            static const char* weekdays[] = {
                "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
            };
            lv_label_set_text(lichuang_home_time_label_, time_text);
            lv_label_set_text(lichuang_home_date_label_, date_text);
            lv_label_set_text(lichuang_home_weekday_label_, weekdays[timeinfo.tm_wday]);
        } else {
            lv_label_set_text(lichuang_home_time_label_, "--:--");
            lv_label_set_text(lichuang_home_date_label_, "Date not set");
            lv_label_set_text(lichuang_home_weekday_label_, "Waiting for network time");
        }
    }

    auto& environment = EnvironmentService::GetInstance();
    environment.RefreshAsync(false);
    auto env = environment.GetSnapshot();

    char weather[96];
    if (!env.configured) {
        snprintf(weather, sizeof(weather), "Weather: configure AMap key");
    } else if (env.weather_available) {
        snprintf(weather, sizeof(weather), "%s %s %sC H%s%%",
            env.city.empty() ? "Local" : env.city.c_str(),
            env.weather_text.c_str(),
            env.temperature_c.empty() ? "--" : env.temperature_c.c_str(),
            env.humidity.empty() ? "--" : env.humidity.c_str());
    } else if (env.refresh_in_progress) {
        snprintf(weather, sizeof(weather), "Weather: updating...");
    } else {
        snprintf(weather, sizeof(weather), "Weather: unavailable");
    }
    lv_label_set_text(lichuang_home_weather_label_, weather);

    char air[96];
    if (env.air_available) {
        char aqi_value[12];
        if (env.aqi >= 0) {
            snprintf(aqi_value, sizeof(aqi_value), "%d", env.aqi);
        } else {
            snprintf(aqi_value, sizeof(aqi_value), "--");
        }
        char pm25_value[12];
        if (env.pm25 >= 0) {
            snprintf(pm25_value, sizeof(pm25_value), "%d", env.pm25);
        } else {
            snprintf(pm25_value, sizeof(pm25_value), "--");
        }
        snprintf(air, sizeof(air), "AQI: %s %s PM2.5 %s",
            aqi_value,
            env.air_category.empty() ? "" : env.air_category.c_str(),
            pm25_value);
    } else if (!env.configured) {
        snprintf(air, sizeof(air), "AQI: configure endpoint");
    } else if (env.refresh_in_progress) {
        snprintf(air, sizeof(air), "AQI: updating...");
    } else {
        snprintf(air, sizeof(air), "AQI: unavailable");
    }
    lv_label_set_text(lichuang_home_air_label_, air);
}

void LcdDisplay::UpdateLichuangPomodoroView(bool force) {
    (void)force;
    if (lichuang_pomodoro_status_label_ == nullptr ||
            lichuang_pomodoro_remaining_label_ == nullptr) {
        return;
    }

    auto status = PomodoroTimer::GetInstance().GetStatus();

    char remaining[16];
    FormatDuration(remaining, sizeof(remaining), status.remaining_seconds);
    lv_label_set_text(lichuang_pomodoro_remaining_label_, remaining);
    SetBarProgress(lichuang_pomodoro_progress_bar_, status.elapsed_seconds,
        status.duration_minutes * 60);
    SetButtonText(lichuang_pomodoro_pause_button_,
        status.state == PomodoroState::kPaused ? "Resume" : "Pause");

    char text[64];
    char elapsed[16];
    char duration[16];
    FormatDuration(elapsed, sizeof(elapsed), status.elapsed_seconds);
    FormatDuration(duration, sizeof(duration), status.duration_minutes * 60);
    const char* mode = "READY";
    lv_color_t state_color = lv_color_hex(kLichuangTechCyan);
    bool timer_active = false;
    switch (status.state) {
        case PomodoroState::kRunning:
            mode = "FOCUS TIMER";
            state_color = lv_color_hex(kLichuangTechRed);
            timer_active = true;
            snprintf(text, sizeof(text), "Countdown %s/%s", elapsed, duration);
            break;
        case PomodoroState::kPaused:
            mode = "PAUSED";
            state_color = lv_color_hex(kLichuangTechAmber);
            timer_active = true;
            snprintf(text, sizeof(text), "Paused %s/%s", elapsed, duration);
            break;
        case PomodoroState::kFinished:
            mode = "COMPLETE";
            state_color = lv_color_hex(kLichuangTechGreen);
            snprintf(text, sizeof(text), "Finished %s", duration);
            break;
        case PomodoroState::kIdle:
        default:
            snprintf(text, sizeof(text), "Pick 15m, 30m, or 45m");
            break;
    }
    if (lichuang_pomodoro_mode_label_ != nullptr) {
        lv_label_set_text(lichuang_pomodoro_mode_label_, mode);
        lv_obj_set_style_text_color(lichuang_pomodoro_mode_label_, state_color, 0);
    }
    lv_obj_set_style_text_color(lichuang_pomodoro_remaining_label_, state_color, 0);
    lv_obj_set_style_text_color(lichuang_pomodoro_status_label_, lv_color_hex(kLichuangTechText), 0);
    SetBarIndicatorColor(lichuang_pomodoro_progress_bar_, state_color);
    SetObjectHidden(lichuang_pomodoro_preset_label_, timer_active);
    SetObjectHidden(lichuang_pomodoro_preset_row_, timer_active);
    if (timer_active) {
        ApplyButtonColors(lichuang_pomodoro_pause_button_, state_color,
            state_color, lv_color_hex(kLichuangTechBg), LV_OPA_COVER);
    } else {
        ApplyButtonColors(lichuang_pomodoro_pause_button_, lv_color_hex(kLichuangTechButton),
            lv_color_hex(kLichuangTechBorder), lv_color_hex(kLichuangTechMuted), LV_OPA_COVER);
    }
    lv_label_set_text(lichuang_pomodoro_status_label_, text);
}

void LcdDisplay::UpdateLichuangMusicView(bool force) {
    if (lichuang_music_status_label_ == nullptr ||
            lichuang_music_track_label_ == nullptr ||
            lichuang_music_detail_label_ == nullptr ||
            lichuang_music_list_label_ == nullptr) {
        return;
    }

    auto& storage = SdCardService::GetInstance();
    if (!storage.IsMounted()) {
        lichuang_last_media_scan_time_ = 0;
        lv_label_set_text(lichuang_music_status_label_, "SD: not mounted");
        lv_obj_set_style_text_color(lichuang_music_status_label_, lv_color_hex(kLichuangTechRed), 0);
        lv_label_set_text(lichuang_music_track_label_, "Playlist: unavailable");
        lv_label_set_text(lichuang_music_detail_label_, "00:00/00:00  -- Hz");
        lv_label_set_text(lichuang_music_list_label_, "Insert SD card");
        SetBarProgress(lichuang_music_progress_bar_, 0, 0);
        SetBarIndicatorColor(lichuang_music_progress_bar_, lv_color_hex(kLichuangTechRed));
        SetButtonText(lichuang_music_play_button_, "Play");
        ApplyButtonColors(lichuang_music_play_button_, lv_color_hex(kLichuangTechButton),
            lv_color_hex(kLichuangTechBorder), lv_color_hex(kLichuangTechMuted), LV_OPA_COVER);
        return;
    }

    auto& player = MusicPlayer::GetInstance();
    auto player_status = player.GetStatus();
    bool active = player_status.state == MusicPlayerState::kPlaying ||
        player_status.state == MusicPlayerState::kPaused ||
        player_status.state == MusicPlayerState::kError;
    time_t now = time(nullptr);
    bool scan_due = lichuang_last_media_scan_time_ == 0 ||
        now - lichuang_last_media_scan_time_ >= 10;

    std::vector<MediaFile> files;
    bool scanned_playlist = false;
    if (!active && (force || scan_due)) {
        lichuang_last_media_scan_time_ = now > 0 ? now : 1;
        files = player.GetPlaylist();
        player_status = player.GetStatus();
        scanned_playlist = true;
    }
    uint64_t free_mb = storage.GetFreeBytes() / (1024 * 1024);
    char status[48];
    snprintf(status, sizeof(status), "SD: mounted, free %llu MB",
        static_cast<unsigned long long>(free_mb));
    lv_label_set_text(lichuang_music_status_label_, status);
    lv_color_t music_color = player_status.state == MusicPlayerState::kError
        ? lv_color_hex(kLichuangTechRed)
        : lv_color_hex(kLichuangTechBlue);
    lv_obj_set_style_text_color(lichuang_music_status_label_, music_color, 0);

    SetBarProgress(lichuang_music_progress_bar_, player_status.elapsed_seconds,
        player_status.duration_seconds);
    SetBarIndicatorColor(lichuang_music_progress_bar_, music_color);
    SetButtonText(lichuang_music_play_button_,
        player_status.state == MusicPlayerState::kPlaying ? "Pause" : "Play");
    ApplyButtonColors(lichuang_music_play_button_, music_color,
        music_color, lv_color_hex(kLichuangTechBg), LV_OPA_COVER);

    char playlist[96];
    if (player_status.state == MusicPlayerState::kPlaying ||
            player_status.state == MusicPlayerState::kPaused) {
        snprintf(playlist, sizeof(playlist), "%s %s: %.42s",
            player_status.state == MusicPlayerState::kPlaying ? "Playing" : "Paused",
            player_status.track_type.empty() ? "audio" : player_status.track_type.c_str(),
            player_status.track_name.empty() ? "(unknown)" : player_status.track_name.c_str());
    } else if (player_status.state == MusicPlayerState::kError) {
        snprintf(playlist, sizeof(playlist), "Music error: %.58s",
            player_status.last_error.c_str());
    } else if (scanned_playlist && files.empty()) {
        snprintf(playlist, sizeof(playlist), "Playlist: 0 WAV/MP3 files");
    } else if (scanned_playlist) {
        snprintf(playlist, sizeof(playlist), "Playlist: %u WAV/MP3, newest: %.24s",
            static_cast<unsigned>(files.size()), files.front().display_name.c_str());
    } else if (player_status.playlist_size > 0) {
        if (player_status.track_name.empty()) {
            snprintf(playlist, sizeof(playlist), "Playlist: %u WAV/MP3",
                static_cast<unsigned>(player_status.playlist_size));
        } else {
            snprintf(playlist, sizeof(playlist), "Playlist: %u WAV/MP3, last: %.24s",
                static_cast<unsigned>(player_status.playlist_size),
                player_status.track_name.c_str());
        }
    } else {
        snprintf(playlist, sizeof(playlist), "Playlist: --");
    }
    lv_label_set_text(lichuang_music_track_label_, playlist);

    char elapsed[16];
    char duration[16];
    FormatDuration(elapsed, sizeof(elapsed), player_status.elapsed_seconds);
    FormatDuration(duration, sizeof(duration), player_status.duration_seconds);
    char detail[64];
    if (player_status.sample_rate > 0 && player_status.channels > 0) {
        snprintf(detail, sizeof(detail), "%s/%s  %d Hz %dch",
            elapsed,
            duration,
            player_status.sample_rate,
            player_status.channels);
    } else {
        snprintf(detail, sizeof(detail), "%s/%s  -- Hz", elapsed, duration);
    }
    lv_label_set_text(lichuang_music_detail_label_, detail);

    char list[80];
    if (!files.empty()) {
        int next_index = 0;
        if (player_status.current_index >= 0 &&
                player_status.current_index < static_cast<int>(files.size())) {
            next_index = (player_status.current_index + 1) % static_cast<int>(files.size());
        }
        snprintf(list, sizeof(list), "Next: %.40s", files[next_index].display_name.c_str());
    } else if (player_status.playlist_size > 0 && player_status.current_index >= 0) {
        snprintf(list, sizeof(list), "Track %d/%u",
            player_status.current_index + 1,
            static_cast<unsigned>(player_status.playlist_size));
    } else if (player_status.playlist_size > 0) {
        snprintf(list, sizeof(list), "Tracks: %u",
            static_cast<unsigned>(player_status.playlist_size));
    } else {
        snprintf(list, sizeof(list), "Next: --");
    }
    lv_label_set_text(lichuang_music_list_label_, list);
}

void LcdDisplay::UpdateLichuangRecorderView(bool force) {
    (void)force;
    if (lichuang_recorder_status_label_ == nullptr ||
            lichuang_recorder_file_label_ == nullptr ||
            lichuang_recorder_detail_label_ == nullptr) {
        return;
    }

    auto status = AudioRecorder::GetInstance().GetStatus();
    uint64_t free_mb = status.free_bytes / (1024 * 1024);
    uint32_t written_kb = status.bytes_written / 1024;

    SetBarProgress(lichuang_recorder_progress_bar_, status.elapsed_seconds, 60 * 60);
    SetButtonText(lichuang_recorder_start_button_, "Start");
    SetButtonText(lichuang_recorder_pause_button_,
        status.state == AudioRecorderState::kPaused ? "Resume" : "Pause");
    SetButtonText(lichuang_recorder_stop_button_, "Stop");

    char elapsed[16];
    FormatDuration(elapsed, sizeof(elapsed), status.elapsed_seconds);

    char state[72];
    lv_color_t state_color = lv_color_hex(kLichuangTechRed);
    bool recorder_active = false;
    switch (status.state) {
        case AudioRecorderState::kRecording:
            recorder_active = true;
            snprintf(state, sizeof(state), "Recording %s", elapsed);
            break;
        case AudioRecorderState::kPaused:
            recorder_active = true;
            state_color = lv_color_hex(kLichuangTechAmber);
            snprintf(state, sizeof(state), "Paused %s", elapsed);
            break;
        case AudioRecorderState::kError:
            snprintf(state, sizeof(state), "Recorder error: %.52s",
                status.last_error.c_str());
            break;
        case AudioRecorderState::kIdle:
        default:
            state_color = lv_color_hex(kLichuangTechGreen);
            snprintf(state, sizeof(state), "Ready to record");
            break;
    }
    lv_label_set_text(lichuang_recorder_status_label_, state);
    lv_obj_set_style_text_color(lichuang_recorder_status_label_, state_color, 0);
    SetBarIndicatorColor(lichuang_recorder_progress_bar_, state_color);
    if (recorder_active) {
        ApplyButtonColors(lichuang_recorder_start_button_, lv_color_hex(kLichuangTechButton),
            lv_color_hex(kLichuangTechBorder), lv_color_hex(kLichuangTechMuted), LV_OPA_COVER);
        ApplyButtonColors(lichuang_recorder_stop_button_, lv_color_hex(kLichuangTechRed),
            lv_color_hex(kLichuangTechRed), lv_color_hex(kLichuangTechBg), LV_OPA_COVER);
        ApplyButtonColors(lichuang_recorder_pause_button_, state_color,
            state_color, lv_color_hex(kLichuangTechBg), LV_OPA_COVER);
    } else {
        ApplyButtonColors(lichuang_recorder_start_button_, lv_color_hex(kLichuangTechRed),
            lv_color_hex(kLichuangTechRed), lv_color_hex(kLichuangTechBg), LV_OPA_COVER);
        ApplyButtonColors(lichuang_recorder_stop_button_, lv_color_hex(kLichuangTechButton),
            lv_color_hex(kLichuangTechBorder), lv_color_hex(kLichuangTechMuted), LV_OPA_COVER);
        ApplyButtonColors(lichuang_recorder_pause_button_, lv_color_hex(kLichuangTechButton),
            lv_color_hex(kLichuangTechBorder), lv_color_hex(kLichuangTechMuted), LV_OPA_COVER);
    }

    char detail[80];
    if (status.free_bytes > 0) {
        snprintf(detail, sizeof(detail), "Elapsed %s, %u KB, free %llu MB",
            elapsed,
            static_cast<unsigned>(written_kb),
            static_cast<unsigned long long>(free_mb));
    } else {
        snprintf(detail, sizeof(detail), "Elapsed %s, %u KB, free -- MB",
            elapsed,
            static_cast<unsigned>(written_kb));
    }
    lv_label_set_text(lichuang_recorder_detail_label_, detail);

    char file[96];
    if (!status.file_name.empty()) {
        snprintf(file, sizeof(file), "File: %.58s", status.file_name.c_str());
    } else {
        snprintf(file, sizeof(file), "Output: /sdcard/recordings");
    }
    lv_label_set_text(lichuang_recorder_file_label_, file);
}

void LcdDisplay::HandleLichuangPomodoroPreset(int minutes) {
    PomodoroTimer::GetInstance().Start(minutes);
    UpdateLichuangPomodoroView(true);
}

void LcdDisplay::HandleLichuangPomodoroPauseResume() {
    auto& timer = PomodoroTimer::GetInstance();
    auto status = timer.GetStatus();
    if (status.state == PomodoroState::kRunning) {
        timer.Pause();
    } else if (status.state == PomodoroState::kPaused) {
        timer.Resume();
    }
    UpdateLichuangPomodoroView(true);
}

void LcdDisplay::HandleLichuangPomodoroCancel() {
    PomodoroTimer::GetInstance().Cancel();
    UpdateLichuangPomodoroView(true);
}

void LcdDisplay::HandleLichuangMusicPrevious() {
    MusicPlayer::GetInstance().Previous();
    UpdateLichuangMusicView(true);
}

void LcdDisplay::HandleLichuangMusicPlayPause() {
    MusicPlayer::GetInstance().TogglePlayPause();
    UpdateLichuangMusicView(true);
}

void LcdDisplay::HandleLichuangMusicNext() {
    MusicPlayer::GetInstance().Next();
    UpdateLichuangMusicView(true);
}

void LcdDisplay::HandleLichuangRecorderStart() {
    AudioRecorder::GetInstance().Start();
    UpdateLichuangRecorderView(true);
}

void LcdDisplay::HandleLichuangRecorderPauseResume() {
    AudioRecorder::GetInstance().TogglePauseResume();
    UpdateLichuangRecorderView(true);
}

void LcdDisplay::HandleLichuangRecorderStop() {
    AudioRecorder::GetInstance().Stop();
    UpdateLichuangRecorderView(true);
}

void LcdDisplay::LichuangNavEventCallback(lv_event_t* event) {
    auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    auto* display = static_cast<LcdDisplay*>(lv_obj_get_user_data(button));
    int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if (display == nullptr ||
            index < 0 ||
            index >= static_cast<int>(LichuangFeatureView::kCount)) {
        return;
    }

    display->SetLichuangFeatureView(static_cast<LichuangFeatureView>(index));
}

void LcdDisplay::LichuangFeatureGestureEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display == nullptr) {
        return;
    }

    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        display->SwitchLichuangFeatureView(1);
        lv_indev_wait_release(indev);
    } else if (dir == LV_DIR_RIGHT) {
        display->SwitchLichuangFeatureView(-1);
        lv_indev_wait_release(indev);
    }
}

void LcdDisplay::LichuangPomodoroPresetEventCallback(lv_event_t* event) {
    auto* button = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    auto* display = static_cast<LcdDisplay*>(lv_obj_get_user_data(button));
    int minutes = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if (display != nullptr) {
        display->HandleLichuangPomodoroPreset(minutes);
    }
}

void LcdDisplay::LichuangPomodoroPauseResumeEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangPomodoroPauseResume();
    }
}

void LcdDisplay::LichuangPomodoroCancelEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangPomodoroCancel();
    }
}

void LcdDisplay::LichuangMusicPreviousEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangMusicPrevious();
    }
}

void LcdDisplay::LichuangMusicPlayPauseEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangMusicPlayPause();
    }
}

void LcdDisplay::LichuangMusicNextEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangMusicNext();
    }
}

void LcdDisplay::LichuangRecorderStartEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangRecorderStart();
    }
}

void LcdDisplay::LichuangRecorderPauseResumeEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangRecorderPauseResume();
    }
}

void LcdDisplay::LichuangRecorderStopEventCallback(lv_event_t* event) {
    auto* display = static_cast<LcdDisplay*>(lv_event_get_user_data(event));
    if (display != nullptr) {
        display->HandleLichuangRecorderStop();
    }
}
#endif

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void LcdDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    
    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(screen);
#endif

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(container_);
#endif

    /* Layer 1: Top bar - for status icons */
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    top_bar_ = lv_obj_create(screen);
#else
    top_bar_ = lv_obj_create(container_);
#endif
    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);  // 50% opacity background
    lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);
    AttachTopBarTouchTarget(top_bar_);
#endif

    // Left icon
    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    // Right icons container
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);  // Overlap with top_bar_
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(status_bar_);
#endif

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.8);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES * 0.8);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);
    
    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(), 0); // Background for chat area

    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    
    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content_, lvgl_theme->spacing(4), 0); // Space between messages
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(content_);
#endif

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    emoji_image_ = lv_img_create(screen);
    lv_obj_align(emoji_image_, LV_ALIGN_TOP_MID, 0, text_font->line_height + lvgl_theme->spacing(8));

    // Display AI logo while booting
    emoji_label_ = lv_label_create(screen);
    lv_obj_center(emoji_label_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);

#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    CreateLichuangFeatureShell();
    SetTopBarVisible(false);
#endif
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    CreateStandbyDashboard();
#endif
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    CreateDeviceBindingUi();
#endif
}
#if CONFIG_IDF_TARGET_ESP32P4
#define  MAX_MESSAGES 40
#else
#define  MAX_MESSAGES 20
#endif
void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!", role, content);
    }
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG, "SetChatMessage('%s', '%s') failed: content_ is nullptr (SetupUI() was called but container not created)", role, content);
        }
        return;
    }
    
    // Check if message count exceeds limit
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (child_count >= MAX_MESSAGES) {
        // Delete the oldest message (first child object)
        lv_obj_t* first_child = lv_obj_get_child(content_, 0);
        if (first_child != nullptr) {
            lv_obj_del(first_child);
            // Refresh child count after deletion
            child_count = lv_obj_get_child_cnt(content_);
        }
        // Scroll to the last message immediately (get last_child after deletion)
        if (child_count > 0) {
            lv_obj_t* last_child = lv_obj_get_child(content_, child_count - 1);
            if (last_child != nullptr && lv_obj_is_valid(last_child)) {
                lv_obj_scroll_to_view_recursive(last_child, LV_ANIM_OFF);
            }
        }
    }
    
    // Collapse system messages (if it's a system message, check if the last message is also a system message)
    if (strcmp(role, "system") == 0) {
        // Refresh child count to get accurate count after potential deletion above
        child_count = lv_obj_get_child_cnt(content_);
        if (child_count > 0) {
            // Get the last message container
            lv_obj_t* last_container = lv_obj_get_child(content_, child_count - 1);
            if (last_container != nullptr && lv_obj_is_valid(last_container) && lv_obj_get_child_cnt(last_container) > 0) {
                // Get the bubble inside the container
                lv_obj_t* last_bubble = lv_obj_get_child(last_container, 0);
                if (last_bubble != nullptr && lv_obj_is_valid(last_bubble)) {
                    // Check if bubble type is system message
                    void* bubble_type_ptr = lv_obj_get_user_data(last_bubble);
                    if (bubble_type_ptr != nullptr && strcmp((const char*)bubble_type_ptr, "system") == 0) {
                        // If the last message is also a system message, delete it
                        lv_obj_del(last_container);
                    }
                }
            }
        }
    } else {
        // Hide the centered AI logo
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }

    // Avoid empty message boxes
    if(strlen(content) == 0) {
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
        UpdateLichuangFeatureShellVisibility();
#endif
        return;
    }

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    // Create a message bubble
    lv_obj_t* msg_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(msg_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(msg_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(msg_bubble, 0, 0);
    lv_obj_set_style_pad_all(msg_bubble, lvgl_theme->spacing(4), 0);

    // Create the message text
    lv_obj_t* msg_text = lv_label_create(msg_bubble);
    lv_label_set_text(msg_text, content);
    
    // Calculate bubble width constraints
    lv_coord_t max_width = LV_HOR_RES * 85 / 100 - 16;  // 85% of screen width
    lv_coord_t min_width = 20;  
    
    // Let LVGL calculate the natural text width first
    lv_obj_set_width(msg_text, LV_SIZE_CONTENT);
    lv_obj_update_layout(msg_text);
    lv_coord_t text_width = lv_obj_get_width(msg_text);
    
    // Ensure text width is not less than minimum width
    if (text_width < min_width) {
        text_width = min_width;
    }

    // Constrain to max width
    lv_coord_t bubble_width = (text_width < max_width) ? text_width : max_width;
    
    // Set message text width
    lv_obj_set_width(msg_text, bubble_width);
    lv_label_set_long_mode(msg_text, LV_LABEL_LONG_WRAP);

    // Set bubble width
    lv_obj_set_width(msg_bubble, bubble_width);
    lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);

    // Set alignment and style based on message role
    if (strcmp(role, "user") == 0) {
        // User messages are right-aligned with green background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->user_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);
        
        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"user");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "assistant") == 0) {
        // Assistant messages are left-aligned with white background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->assistant_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->text_color(), 0);
        
        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"assistant");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    } else if (strcmp(role, "system") == 0) {
        // System messages are center-aligned with light gray background
        lv_obj_set_style_bg_color(msg_bubble, lvgl_theme->system_bubble_color(), 0);
        lv_obj_set_style_bg_opa(msg_bubble, LV_OPA_70, 0);
        // Set text color for contrast
        lv_obj_set_style_text_color(msg_text, lvgl_theme->system_text_color(), 0);
        
        // Set custom attribute to mark bubble type
        lv_obj_set_user_data(msg_bubble, (void*)"system");
        
        // Set appropriate width for content
        lv_obj_set_width(msg_bubble, LV_SIZE_CONTENT);
        lv_obj_set_height(msg_bubble, LV_SIZE_CONTENT);
        
        // Don't grow
        lv_obj_set_style_flex_grow(msg_bubble, 0, 0);
    }
    
    // Create a full-width container for user messages to ensure right alignment
    if (strcmp(role, "user") == 0) {
        // Create a full-width container
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        // Make container transparent and borderless
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        // Move the message bubble into this container
        lv_obj_set_parent(msg_bubble, container);
        
        // Right align the bubble in the container
        lv_obj_align(msg_bubble, LV_ALIGN_RIGHT_MID, -25, 0);
        
        // Auto-scroll to this container
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else if (strcmp(role, "system") == 0) {
        // Create full-width container for system messages to ensure center alignment
        lv_obj_t* container = lv_obj_create(content_);
        lv_obj_set_width(container, LV_HOR_RES);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        
        lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 0, 0);
        
        lv_obj_set_parent(msg_bubble, container);
        lv_obj_align(msg_bubble, LV_ALIGN_CENTER, 0, 0);
        lv_obj_scroll_to_view_recursive(container, LV_ANIM_ON);
    } else {
        // For assistant messages
        // Left align assistant messages
        lv_obj_align(msg_bubble, LV_ALIGN_LEFT_MID, 0, 0);

        // Auto-scroll to the message bubble
        lv_obj_scroll_to_view_recursive(msg_bubble, LV_ANIM_ON);
    }
    
    // Store reference to the latest message label
    chat_message_label_ = msg_text;
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    UpdateLichuangFeatureShellVisibility();
#endif
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }

    if (image == nullptr) {
        return;
    }
    
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    // Create a message bubble for image preview
    lv_obj_t* img_bubble = lv_obj_create(content_);
    lv_obj_set_style_radius(img_bubble, 8, 0);
    lv_obj_set_scrollbar_mode(img_bubble, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(img_bubble, 0, 0);
    lv_obj_set_style_pad_all(img_bubble, lvgl_theme->spacing(4), 0);
    
    // Set image bubble background color (similar to system message)
    lv_obj_set_style_bg_color(img_bubble, lvgl_theme->assistant_bubble_color(), 0);
    lv_obj_set_style_bg_opa(img_bubble, LV_OPA_70, 0);
    
    // Set custom attribute to mark bubble type
    lv_obj_set_user_data(img_bubble, (void*)"image");

    // Create the image object inside the bubble
    lv_obj_t* preview_image = lv_image_create(img_bubble);
    
    // Calculate appropriate size for the image
    lv_coord_t max_width = LV_HOR_RES * 70 / 100;  // 70% of screen width
    lv_coord_t max_height = LV_VER_RES * 50 / 100; // 50% of screen height
    
    // Calculate zoom factor to fit within maximum dimensions
    auto img_dsc = image->image_dsc();
    lv_coord_t img_width = img_dsc->header.w;
    lv_coord_t img_height = img_dsc->header.h;
    if (img_width == 0 || img_height == 0) {
        img_width = max_width;
        img_height = max_height;
        ESP_LOGW(TAG, "Invalid image dimensions: %ld x %ld, using default dimensions: %ld x %ld", img_width, img_height, max_width, max_height);
    }
    
    lv_coord_t zoom_w = (max_width * 256) / img_width;
    lv_coord_t zoom_h = (max_height * 256) / img_height;
    lv_coord_t zoom = (zoom_w < zoom_h) ? zoom_w : zoom_h;
    
    // Ensure zoom doesn't exceed 256 (100%)
    if (zoom > 256) zoom = 256;
    
    // Set image properties
    lv_image_set_src(preview_image, img_dsc);
    lv_image_set_scale(preview_image, zoom);
    
    // Add event handler to clean up LvglImage when image is deleted
    // We need to transfer ownership of the unique_ptr to the event callback
    LvglImage* raw_image = image.release(); // Release ownership of smart pointer
    lv_obj_add_event_cb(preview_image, [](lv_event_t* e) {
        LvglImage* img = (LvglImage*)lv_event_get_user_data(e);
        if (img != nullptr) {
            delete img; // Properly release memory by deleting LvglImage object
        }
    }, LV_EVENT_DELETE, (void*)raw_image);
    
    // Calculate actual scaled image dimensions
    lv_coord_t scaled_width = (img_width * zoom) / 256;
    lv_coord_t scaled_height = (img_height * zoom) / 256;
    
    // Set bubble size to be 16 pixels larger than the image (8 pixels on each side)
    lv_obj_set_width(img_bubble, scaled_width + 16);
    lv_obj_set_height(img_bubble, scaled_height + 16);
    
    // Don't grow in flex layout
    lv_obj_set_style_flex_grow(img_bubble, 0, 0);
    
    // Center the image within the bubble
    lv_obj_center(preview_image);
    
    // Left align the image bubble like assistant messages
    lv_obj_align(img_bubble, LV_ALIGN_LEFT_MID, 0, 0);

    // Auto-scroll to the image bubble
    lv_obj_scroll_to_view_recursive(img_bubble, LV_ANIM_ON);
}

void LcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (content_ == nullptr) {
        return;
    }
    
    // Use lv_obj_clean to delete all children of content_ (chat message bubbles)
    lv_obj_clean(content_);
    
    // Reset chat_message_label_ as it has been deleted
    chat_message_label_ = nullptr;
    
    // Show the centered AI logo (emoji_label_) again
    if (emoji_label_ != nullptr) {
        lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
    
    ESP_LOGI(TAG, "Chat messages cleared");
}
#else
void LcdDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    
    Display::SetupUI();  // Mark SetupUI as called
    DisplayLockGuard lock(this);
    LvglTheme* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    CreateCyberConversationUI();
    CreateStandbyDashboard();
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    CreateDeviceBindingUi();
#endif
    return;
#endif

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);
    lv_obj_set_style_bg_color(screen, lvgl_theme->background_color(), 0);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(screen);
#endif

    /* Container - used as background */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_color(container_, lvgl_theme->border_color(), 0);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(container_);
#endif

    /* Bottom layer: emoji_box_ - centered display */
    emoji_box_ = lv_obj_create(screen);
    lv_obj_set_size(emoji_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(emoji_box_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(emoji_box_, 0, 0);
    lv_obj_set_style_border_width(emoji_box_, 0, 0);
    lv_obj_align(emoji_box_, LV_ALIGN_CENTER, 0, 0);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(emoji_box_);
#endif

    emoji_label_ = lv_label_create(emoji_box_);
    lv_obj_set_style_text_font(emoji_label_, large_icon_font, 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, FONT_AWESOME_MICROCHIP_AI);

    emoji_image_ = lv_img_create(emoji_box_);
    lv_obj_center(emoji_image_);
    lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);

    /* Middle layer: preview_image_ - centered display */
    preview_image_ = lv_image_create(screen);
    lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(preview_image_);
#endif

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(screen);
    lv_obj_set_size(top_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);  // 50% opacity background
    lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_style_pad_top(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(top_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_left(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(top_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(top_bar_);
#endif

    // Left icon
    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);

    // Right icons container
    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_margin_left(battery_label_, lvgl_theme->spacing(2), 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_pad_top(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_style_pad_bottom(status_bar_, lvgl_theme->spacing(2), 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);  // Overlap with top_bar_
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(status_bar_);
#endif

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES * 0.75);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES * 0.75);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    /* Bottom bar - auto height, grows upward with wrapped text */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_width(bottom_bar_, LV_HOR_RES);
    lv_obj_set_height(bottom_bar_, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_pad_all(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* chat_message_label_ placed in bottom_bar_, multiline wrapped display */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#else
    /* Top layer: Bottom bar - fixed height at bottom */
    bottom_bar_ = lv_obj_create(screen);
    lv_obj_set_size(bottom_bar_, LV_HOR_RES, text_font->line_height + lvgl_theme->spacing(8));
    lv_obj_set_style_radius(bottom_bar_, 0, 0);
    lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    lv_obj_set_style_text_color(bottom_bar_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_pad_all(bottom_bar_, 0, 0);
    lv_obj_set_style_pad_left(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_pad_right(bottom_bar_, lvgl_theme->spacing(4), 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* chat_message_label_ placed in bottom_bar_, single-line horizontal scroll */
    chat_message_label_ = lv_label_create(bottom_bar_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES - lvgl_theme->spacing(8));
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    lv_obj_align(chat_message_label_, LV_ALIGN_CENTER, 0, 0);

    // Start scrolling after a delay (short text won't scroll)
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000), LV_PART_MAIN);
    lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);  // Hide until there is content
#endif
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    AttachTopBarTouchTarget(bottom_bar_);
#endif

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, -lvgl_theme->spacing(4));
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);
    lv_obj_set_style_radius(low_battery_popup_, lvgl_theme->spacing(4), 0);
    
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    CreateLichuangFeatureShell();
    SetTopBarVisible(false);
#endif
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    CreateStandbyDashboard();
#endif
#if CONFIG_XIAOZHI_DEVICE_CONTROL
    CreateDeviceBindingUi();
#endif
}

void LcdDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // zoom factor 0.5
        lv_image_set_scale(preview_image_, 128 * width_ / img_dsc->header.w);
    }

    // Hide emoji_box_
    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

void LcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetChatMessage('%s', '%s') called before SetupUI() - message will be lost!", role, content);
    }
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG, "SetChatMessage('%s', '%s') failed: chat_message_label_ is nullptr (SetupUI() was called but label not created)", role, content);
        }
        return;
    }
    lv_label_set_text(chat_message_label_, content);
    // Show bottom_bar_ only when there is content (and subtitle is not globally hidden)
    if (bottom_bar_ != nullptr) {
        if (content == nullptr || content[0] == '\0') {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else if (!hide_subtitle_) {
            lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    UpdateLichuangFeatureShellVisibility();
#endif
#if CONFIG_USE_MULTILINE_CHAT_MESSAGE
    // Re-align bottom_bar_ after text change so it stays anchored to the bottom
    // as its height adapts to the wrapped content.
    if (bottom_bar_ != nullptr) {
        lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
#endif
}

void LcdDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    // In non-wechat mode, just clear the chat message label and hide the bar
    if (chat_message_label_ != nullptr) {
        lv_label_set_text(chat_message_label_, "");
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}
#endif

void LcdDisplay::ShowPomodoroTimerView() {
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    {
        DisplayLockGuard lock(this);
        if (standby_dashboard_ != nullptr) {
            standby_dashboard_->ShowPomodoroView();
            UpdateStandbyDashboardVisibility();
        }
    }
#endif
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    {
        DisplayLockGuard lock(this);
        if (lichuang_feature_shell_ == nullptr) {
            return;
        }

        lichuang_feature_shell_override_until_us_ =
            esp_timer_get_time() + kLichuangPomodoroVoiceOverrideUs;
        SetLichuangFeatureView(LichuangFeatureView::kPomodoro);
        ShowLichuangFeatureShell();
    }
#endif
}

void LcdDisplay::ClosePomodoroTimerView() {
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    DisplayLockGuard lock(this);
    if (standby_dashboard_ != nullptr) {
        standby_dashboard_->ClosePomodoroView();
        UpdateStandbyDashboardVisibility();
    }
#endif
}

void LcdDisplay::CloseStandbyFeatureViews() {
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    DisplayLockGuard lock(this);
    if (standby_dashboard_ != nullptr) {
        standby_dashboard_->CloseFeatureViews();
    }
#else
    ClosePomodoroTimerView();
#endif
}

void LcdDisplay::ClearPreviewImage() { SetPreviewImage(nullptr); }

void LcdDisplay::SetStatus(const char* status) {
    LvglDisplay::SetStatus(status);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    {
        DisplayLockGuard lock(this);
        UpdateCyberExitButtonVisibility();
        UpdateStandbyDashboardVisibility();
    }
#endif
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    {
        DisplayLockGuard lock(this);
        UpdateLichuangFeatureShellVisibility();
    }
#endif
}

void LcdDisplay::ShowNotification(const char* notification, int duration_ms) {
    LvglDisplay::ShowNotification(notification, duration_ms);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    DisplayLockGuard lock(this);
    if (standby_dashboard_ != nullptr) {
        standby_dashboard_->ShowNotification(notification, duration_ms);
    }
#endif
}

void LcdDisplay::UpdateStatusBar(bool update_all) {
    LvglDisplay::UpdateStatusBar(update_all);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    {
        DisplayLockGuard lock(this);
        UpdateCyberExitButtonVisibility();
        UpdateStandbyDashboard(update_all);
    }
#endif
#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    {
        DisplayLockGuard lock(this);
        UpdateLichuangFeatureShellVisibility();
        if (IsLichuangFeatureShellVisible()) {
            UpdateLichuangHomeView(update_all);
            if (lichuang_feature_view_index_ == static_cast<int>(LichuangFeatureView::kPomodoro)) {
                UpdateLichuangPomodoroView(update_all);
            } else if (lichuang_feature_view_index_ ==
                       static_cast<int>(LichuangFeatureView::kMusic)) {
                UpdateLichuangMusicView(update_all);
            } else if (lichuang_feature_view_index_ ==
                       static_cast<int>(LichuangFeatureView::kRecorder)) {
                UpdateLichuangRecorderView(update_all);
            }
        }
    }
#endif
}

void LcdDisplay::SetEmotion(const char* emotion) {
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    // Keep the fixed geometric mark instead of replacing it with legacy emoji.
    (void)emotion;
    return;
#endif
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetEmotion('%s') called before SetupUI() - emotion will not be displayed!", emotion);
    }
    if (emoji_image_ == nullptr) {
        if (setup_ui_called_) {
            ESP_LOGW(TAG, "SetEmotion('%s') failed: emoji_image_ is nullptr (SetupUI() was called but emoji image not created)", emotion);
        }
        return;
    }

    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(emotion) : nullptr;
    if (image == nullptr) {
        const char* utf8 = font_awesome_get_utf8(emotion);
        if (utf8 != nullptr && emoji_label_ != nullptr) {
            DisplayLockGuard lock(this);
            if (gif_controller_) {
                gif_controller_->Stop();
                gif_controller_.reset();
            }
            lv_label_set_text(emoji_label_, utf8);
            lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    DisplayLockGuard lock(this);
    // Stop any running GIF animation in the same lock scope as setting new image
    // to prevent LVGL from accessing freed image data between operations
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    if (image->IsGif()) {
        // Create new GIF controller
        gif_controller_ = std::make_unique<LvglGif>(image->image_dsc());
        
        if (gif_controller_->IsLoaded()) {
            // Set up frame update callback
            gif_controller_->SetFrameCallback([this]() {
                lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            });
            
            // Set initial frame and start animation
            lv_image_set_src(emoji_image_, gif_controller_->image_dsc());
            gif_controller_->Start();
            
            // Show GIF, hide others
            lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGE(TAG, "Failed to load GIF for emotion: %s", emotion);
            gif_controller_.reset();
        }
    } else {
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    // In WeChat message style, if emotion is neutral, don't display it
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    if (strcmp(emotion, "neutral") == 0 && child_count > 0) {
        // Stop GIF animation if running
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
#endif

#if CONFIG_BOARD_TYPE_LICHUANG_DEV_S3
    UpdateLichuangFeatureShellVisibility();
#endif
}

void LcdDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    
    // Get the active screen
    lv_obj_t* screen = lv_screen_active();

    // Set font
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    if (standby_dashboard_ != nullptr) {
        standby_dashboard_->SetFonts(text_font, icon_font);
    }

    // Keep the conversation screen on the same deep navy background used
    // while the device is configuring Wi-Fi. The global light theme must not
    // repaint this board-specific interface white when voice chat starts.
    constexpr uint32_t kCyberBackground = 0x030B16;
    constexpr uint32_t kCyberChrome = 0x020713;
    constexpr uint32_t kCyberText = 0xF4F8FF;
    constexpr uint32_t kCyberCyan = 0x18DFF6;
    lv_obj_set_style_bg_color(screen, lv_color_hex(kCyberBackground), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(kCyberText), 0);

    if (container_ != nullptr) {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_color(container_, lv_color_hex(kCyberBackground), 0);
        lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    }
    if (top_bar_ != nullptr) {
        lv_obj_set_style_bg_color(top_bar_, lv_color_hex(kCyberChrome), 0);
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_COVER, 0);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_set_style_bg_color(bottom_bar_, lv_color_hex(kCyberChrome), 0);
        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_COVER, 0);
    }

    lv_obj_t* fixed_text_labels[] = {
        status_label_,     notification_label_,     chat_message_label_,      low_battery_label_,
        cyber_exit_label_, cyber_brand_name_label_, cyber_company_name_label_};
    for (lv_obj_t* label : fixed_text_labels) {
        if (label != nullptr) {
            lv_obj_set_style_text_font(label, text_font, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(kCyberText), 0);
        }
    }
    if (cyber_company_name_label_ != nullptr) {
        lv_obj_set_style_text_color(cyber_company_name_label_, lv_color_hex(kCyberCyan), 0);
    }

    if (low_battery_popup_ != nullptr) {
        lv_obj_set_style_bg_color(low_battery_popup_, lv_color_hex(0xB62045), 0);
    }
    Display::SetTheme(lvgl_theme);
    return;
#endif

    if (text_font->line_height >= 40) {
        lv_obj_set_style_text_font(mute_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, large_icon_font, 0);
        lv_obj_set_style_text_font(network_label_, large_icon_font, 0);
    } else {
        lv_obj_set_style_text_font(mute_label_, icon_font, 0);
        lv_obj_set_style_text_font(battery_label_, icon_font, 0);
        lv_obj_set_style_text_font(network_label_, icon_font, 0);
    }

    // Set parent text color
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lvgl_theme->text_color(), 0);

    // Set background image
    if (lvgl_theme->background_image() != nullptr) {
        lv_obj_set_style_bg_image_src(container_, lvgl_theme->background_image()->image_dsc(), 0);
    } else {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_color(container_, lvgl_theme->background_color(), 0);
    }
    
    // Update top bar background color with 50% opacity
    if (top_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(top_bar_, lvgl_theme->background_color(), 0);
    }
    
    // Update status bar elements
    lv_obj_set_style_text_color(network_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(status_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(notification_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(mute_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(battery_label_, lvgl_theme->text_color(), 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);

    // If we have the chat message style, update all message bubbles
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    // Set content background opacity
    lv_obj_set_style_bg_opa(content_, LV_OPA_TRANSP, 0);

    // Iterate through all children of content (message containers or bubbles)
    uint32_t child_count = lv_obj_get_child_cnt(content_);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* obj = lv_obj_get_child(content_, i);
        if (obj == nullptr) continue;
        
        lv_obj_t* bubble = nullptr;
        
        // Check if this object is a container or bubble
        // If it's a container (user or system message), get its child as bubble
        // If it's a bubble (assistant message), use it directly
        if (lv_obj_get_child_cnt(obj) > 0) {
            // Might be a container, check if it's a user or system message container
            // User and system message containers are transparent
            lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
            if (bg_opa == LV_OPA_TRANSP) {
                // This is a user or system message container
                bubble = lv_obj_get_child(obj, 0);
            } else {
                // This might be an assistant message bubble itself
                bubble = obj;
            }
        } else {
            // No child elements, might be other UI elements, skip
            continue;
        }
        
        if (bubble == nullptr) continue;
        
        // Use saved user data to identify bubble type
        void* bubble_type_ptr = lv_obj_get_user_data(bubble);
        if (bubble_type_ptr != nullptr) {
            const char* bubble_type = static_cast<const char*>(bubble_type_ptr);
            
            // Apply correct color based on bubble type
            if (strcmp(bubble_type, "user") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->user_bubble_color(), 0);
            } else if (strcmp(bubble_type, "assistant") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->assistant_bubble_color(), 0); 
            } else if (strcmp(bubble_type, "system") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            } else if (strcmp(bubble_type, "image") == 0) {
                lv_obj_set_style_bg_color(bubble, lvgl_theme->system_bubble_color(), 0);
            }
            
            // Update border color
            lv_obj_set_style_border_color(bubble, lvgl_theme->border_color(), 0);
            
            // Update text color for the message
            if (lv_obj_get_child_cnt(bubble) > 0) {
                lv_obj_t* text = lv_obj_get_child(bubble, 0);
                if (text != nullptr) {
                    // Set text color based on bubble type
                    if (strcmp(bubble_type, "system") == 0) {
                        lv_obj_set_style_text_color(text, lvgl_theme->system_text_color(), 0);
                    } else {
                        lv_obj_set_style_text_color(text, lvgl_theme->text_color(), 0);
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "child[%lu] Bubble type is not found", i);
        }
    }
#else
    // Simple UI mode - just update the main chat message
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, lvgl_theme->text_color(), 0);
    }
    
    if (emoji_label_ != nullptr) {
        lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    }
    
    // Update bottom bar background color with 50% opacity
    if (bottom_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(bottom_bar_, lvgl_theme->background_color(), 0);
    }
#endif
    
    // Update low battery popup
    lv_obj_set_style_bg_color(low_battery_popup_, lvgl_theme->low_battery_color(), 0);

    // No errors occurred. Save theme to settings
    Display::SetTheme(lvgl_theme);
}

void LcdDisplay::SetHideSubtitle(bool hide) {
    DisplayLockGuard lock(this);
    hide_subtitle_ = hide;
    
    // Immediately update UI visibility based on the setting
    if (bottom_bar_ != nullptr) {
        if (hide) {
            lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Only show if there is actual content to display
            const char* text = (chat_message_label_ != nullptr) ? lv_label_get_text(chat_message_label_) : nullptr;
            if (text != nullptr && text[0] != '\0') {
                lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}
