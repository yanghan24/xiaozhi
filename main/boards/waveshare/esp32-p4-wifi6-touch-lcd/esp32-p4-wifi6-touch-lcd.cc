#include "application.h"
#include "board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "wifi_board.h"
// #include "display/no_display.h"
#include "assets/lang_config.h"
#include "button.h"
#include "features/pomodoro_timer.h"
#include "mcp_server.h"
#include "services/environment_service.h"
#include "storage/sd_card_service.h"

#include "esp_video.h"
#include "esp_video_init.h"
#include "esp_cam_sensor_xclk.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4B
#include "esp_lcd_st7703.h"
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
#include "esp_lcd_st7701.h"
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7B
#include "esp_lcd_ek79007.h"
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_3_4C || CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4C \
    || CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_8   || CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_10_1
#include "esp_lcd_jd9365.h"
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7
#include "esp_lcd_ili9881c.h"
#endif

#include "config.h"
#include "lcd_init_cmds.h"

#include <driver/i2c_master.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_vfs_fat.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>
#include <sdmmc_cmd.h>
#include "esp_lcd_touch_gt911.h"

#include <mutex>
#define TAG "WaveshareEsp32p4"

class WaveshareEsp32p4 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay *display_;
    EspVideo* camera_ = nullptr;
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    std::mutex storage_mutex_;
    sdmmc_card_t* sd_card_ = nullptr;
    sd_pwr_ctrl_handle_t sd_power_handle_ = nullptr;
    bool sd_spi_bus_initialized_ = false;
#endif

    esp_err_t i2c_device_probe(uint8_t addr) {
        return i2c_master_probe(i2c_bus_, addr, 100);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    static esp_err_t bsp_enable_dsi_phy_power(void) {
#if MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
        static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0

        return ESP_OK;
    }

    void InitializeLCD() {
        bsp_enable_dsi_phy_power();
        esp_lcd_panel_io_handle_t io = NULL;
        esp_lcd_panel_handle_t disp_panel = NULL;

        esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
        esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id = 0,
            .num_data_lanes = 2,
            .lane_bit_rate_mbps = LCD_MIPI_DSI_LANE_BITRATE_MBPS,
        };
        esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);

        ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
        // we use DBI interface to send LCD commands and parameters
        esp_lcd_dbi_io_config_t dbi_config = {
            .virtual_channel = 0,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        };
        esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4B
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 46,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = 720,
                .v_size = 720,
                .hsync_pulse_width = 20,
                .hsync_back_porch = 80,
                .hsync_front_porch = 80,
                .vsync_pulse_width = 4,
                .vsync_back_porch = 12,
                .vsync_front_porch = 30,
            },
            .flags = {
                .use_dma2d = true,
            },
        };
        st7703_vendor_config_t vendor_config = {

            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
            .flags = {
                .use_mipi_interface = 1,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_st7703(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    esp_lcd_dpi_panel_config_t dpi_config = {                                                 
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,  
        .dpi_clock_freq_mhz = 30,                     
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,                    
        .num_fbs = 1,                                 
        .video_timing = {                             
            .h_size = 480,                            
            .v_size = 800,                            
            .hsync_pulse_width = 12,                  
            .hsync_back_porch = 42,                   
            .hsync_front_porch = 42,                  
            .vsync_pulse_width = 8,                   
            .vsync_back_porch = 2,                    
            .vsync_front_porch = 60,                  
        },                                            
        .flags = {
            .use_dma2d = true,
        },                  
    };
    st7701_vendor_config_t vendor_config = {
        .init_cmds = vendor_specific_init_default,
        .init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(st7701_lcd_init_cmd_t),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
        .flags = {
            .use_mipi_interface = 1,
        },

    };

    const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
    };
    esp_lcd_new_panel_st7701(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7B
    esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 52,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = 1024,
                .v_size = 600,
                .hsync_pulse_width = 10,
                .hsync_back_porch = 160,
                .hsync_front_porch = 160,
                .vsync_pulse_width = 1,
                .vsync_back_porch = 23,
                .vsync_front_porch = 12,
            },
            .flags = {
                .use_dma2d = true,
            },
        };
        ek79007_vendor_config_t vendor_config = {
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_ek79007(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_3_4C || CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4C
    esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 46,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = DISPLAY_WIDTH,
                .v_size = DISPLAY_HEIGHT,
                .hsync_pulse_width = 20,
                .hsync_back_porch = 20,
                .hsync_front_porch = 40,
                .vsync_pulse_width = 4,
                .vsync_back_porch = 12,
                .vsync_front_porch = 24,
            },
            .flags = {
                .use_dma2d = true,
            },
        };
        jd9365_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = 2,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_jd9365(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_8    || CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_10_1
    esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 52,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = DISPLAY_WIDTH,
                .v_size = DISPLAY_HEIGHT,
                .hsync_pulse_width = 20,
                .hsync_back_porch = 20,
                .hsync_front_porch = 40,
                .vsync_pulse_width = 4,
                .vsync_back_porch = 10,
                .vsync_front_porch = 30,
            },
            .flags = {
                .use_dma2d = true,
            },
        };
        jd9365_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = 2,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_jd9365(io, &lcd_dev_config, &disp_panel);
#elif CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_7
    esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 80,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = DISPLAY_WIDTH,
                .v_size = DISPLAY_HEIGHT,
                .hsync_pulse_width = 50,
                .hsync_back_porch = 239,
                .hsync_front_porch = 33,
                .vsync_pulse_width = 30,
                .vsync_back_porch = 20,
                .vsync_front_porch = 2,
            },
            .flags = {
                .use_dma2d = true,
            },
        };
        ili9881c_vendor_config_t vendor_config = {
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = 2,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_ili9881c(io, &lcd_dev_config, &disp_panel);
#endif
        esp_lcd_panel_reset(disp_panel);
        esp_lcd_panel_init(disp_panel);

        display_ = new MipiLcdDisplay(io, disp_panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                      DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                      DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
                                      LV_DISPLAY_ROTATION_270);
#else
                                      LV_DISPLAY_ROTATION_0);
#endif
    }
    void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 16,                            
            .flags =
            {
                .disable_control_phase = 1,
            }
	    };
	    if (ESP_OK == i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS)) {
            ESP_LOGI(TAG, "Touch panel found at address 0x%02X", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
        } else if (ESP_OK == i2c_device_probe(ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP)) {
            ESP_LOGI(TAG, "Touch panel found at address 0x%02X", ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
            tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        } else {
            ESP_LOGE(TAG, "Touch panel not found on I2C bus");
            ESP_LOGE(TAG, "Tried addresses: 0x%02X and 0x%02X", 
                     ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, 
                     ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
            return;
        }

        tp_io_config.scl_speed_hz = 400 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }
    void InitializeCamera() {
        esp_video_init_csi_config_t base_csi_config = {
            .sccb_config = {
                .init_sccb = false,
                .i2c_handle = i2c_bus_,
                .freq = 400000,
            },
            .reset_pin = GPIO_NUM_NC,
            .pwdn_pin  = GPIO_NUM_NC,
        };

        esp_video_init_config_t cam_config = {
            .csi      = &base_csi_config,
        };

        camera_ = new EspVideo(cam_config);
    }
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            // During startup (before connected), pressing BOOT button enters Wi-Fi config mode without reboot
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    esp_err_t InitializeSdCard() {
        std::lock_guard<std::mutex> lock(storage_mutex_);
        auto& storage = SdCardService::GetInstance();
        if (storage.IsMounted()) {
            return ESP_OK;
        }

        if (sd_power_handle_ == nullptr) {
            sd_pwr_ctrl_ldo_config_t ldo_config = {
                .ldo_chan_id = 4,
            };
            esp_err_t power_ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &sd_power_handle_);
            if (power_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to enable SD LDO channel 4: %s", esp_err_to_name(power_ret));
                return power_ret;
            }
            ESP_LOGI(TAG, "SD LDO channel 4 initialized");
        }

        // ESP-Hosted already uses the P4 SDMMC peripheral for the Wi-Fi 6
        // coprocessor. Initializing the microSD card through SDMMC resets that
        // shared peripheral and eventually crashes the hosted SDIO RX task.
        // Use the card's SPI-compatible pins instead so both devices can run.
        gpio_set_pull_mode(SDCARD_SDMMC_CLK_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SDMMC_CMD_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SDMMC_D0_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SDMMC_D1_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SDMMC_D2_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SDMMC_D3_PIN, GPIO_PULLUP_ONLY);

        if (!sd_spi_bus_initialized_) {
            spi_bus_config_t bus_config = {};
            bus_config.mosi_io_num = SDCARD_SDMMC_CMD_PIN;
            bus_config.miso_io_num = SDCARD_SDMMC_D0_PIN;
            bus_config.sclk_io_num = SDCARD_SDMMC_CLK_PIN;
            bus_config.quadwp_io_num = GPIO_NUM_NC;
            bus_config.quadhd_io_num = GPIO_NUM_NC;
            bus_config.max_transfer_sz = 16 * 1024;

            esp_err_t bus_ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
            if (bus_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to initialize SD SPI bus: %s", esp_err_to_name(bus_ret));
                return bus_ret;
            }
            sd_spi_bus_initialized_ = true;
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SPI2_HOST;
        host.max_freq_khz = 10000;
        host.pwr_ctrl_handle = sd_power_handle_;
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.host_id = SPI2_HOST;
        slot_config.gpio_cs = SDCARD_SDMMC_D3_PIN;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 8,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = true,
        };
        esp_err_t ret = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_config,
                                                &mount_config, &sd_card_);
        if (ret != ESP_OK) {
            sd_card_ = nullptr;
            ESP_LOGW(TAG, "Failed to mount SD card at %s: %s", SDCARD_MOUNT_POINT,
                     esp_err_to_name(ret));
            return ret;
        }

        sdmmc_card_print_info(stdout, sd_card_);
        ESP_LOGI(TAG, "SD card mounted over SPI at %s", SDCARD_MOUNT_POINT);
        ret = storage.EnsureMediaDirectories();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to prepare SD media directories: %s", esp_err_to_name(ret));
        }
        return ret;
    }
#endif

#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    void InitializeDashboardTools() {
        auto& pomodoro = PomodoroTimer::GetInstance();
        pomodoro.SetFinishCallback([]() {
            Application::GetInstance().Schedule([]() {
                if (PomodoroTimer::GetInstance().GetStatus().state != PomodoroState::kFinished) {
                    return;
                }
                auto* display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->ShowPomodoroTimerView();
                    display->ShowNotification("番茄钟已完成", 5000);
                }
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
            });
        });

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.pomodoro.start",
            "Start or restart a Pomodoro focus timer with a custom duration from 1 to 180 minutes. "
            "Use this when the user asks to start or set a Pomodoro timer.",
            PropertyList(
                {Property("duration_minutes", kPropertyTypeInteger,
                          PomodoroTimer::kMinDurationMinutes, PomodoroTimer::kMaxDurationMinutes)}),
            [](const PropertyList& properties) -> ReturnValue {
                int duration_minutes = properties["duration_minutes"].value<int>();
                esp_err_t ret = PomodoroTimer::GetInstance().Start(duration_minutes);
                if (ret != ESP_OK) {
                    return std::string("Duration must be between 1 and 180 minutes.");
                }
                auto* display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->ShowPomodoroTimerView();
                }
                return std::string("Pomodoro started for ") + std::to_string(duration_minutes) +
                       " minutes.";
            });

        mcp_server.AddTool("self.pomodoro.pause",
                           "Pause the running Pomodoro timer and preserve its remaining time.",
                           PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                               PomodoroTimer::GetInstance().Pause();
                               auto* display = Board::GetInstance().GetDisplay();
                               if (display != nullptr) {
                                   display->ShowPomodoroTimerView();
                               }
                               return PomodoroTimer::GetInstance().GetStatusText();
                           });

        mcp_server.AddTool("self.pomodoro.resume", "Resume a paused Pomodoro timer.",
                           PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                               PomodoroTimer::GetInstance().Resume();
                               auto* display = Board::GetInstance().GetDisplay();
                               if (display != nullptr) {
                                   display->ShowPomodoroTimerView();
                               }
                               return PomodoroTimer::GetInstance().GetStatusText();
                           });

        mcp_server.AddTool(
            "self.pomodoro.cancel",
            "Stop, close, or cancel the current Pomodoro timer and close its countdown screen.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                PomodoroTimer::GetInstance().Cancel();
                auto* display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->ClosePomodoroTimerView();
                }
                return std::string("Pomodoro canceled and closed.");
            });

        mcp_server.AddTool("self.pomodoro.get_status",
                           "Get the current Pomodoro timer state and remaining time.",
                           PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                               return PomodoroTimer::GetInstance().GetStatusText();
                           });

        mcp_server.AddUserOnlyTool(
            "self.environment.set_config",
            "Configure standby weather using an AMap Web Service API key. The key is stored in "
            "NVS.",
            PropertyList({Property("amap_api_key", kPropertyTypeString),
                          Property("aggregate_url", kPropertyTypeString, ""),
                          Property("refresh_interval_minutes", kPropertyTypeInteger, 30, 5, 1440),
                          Property("fallback_adcode", kPropertyTypeString, ""),
                          Property("fallback_city", kPropertyTypeString, "")}),
            [](const PropertyList& properties) -> ReturnValue {
                EnvironmentConfig config;
                config.amap_api_key = properties["amap_api_key"].value<std::string>();
                config.aggregate_url = properties["aggregate_url"].value<std::string>();
                config.refresh_interval_minutes =
                    properties["refresh_interval_minutes"].value<int>();
                config.fallback_adcode = properties["fallback_adcode"].value<std::string>();
                config.fallback_city = properties["fallback_city"].value<std::string>();
                EnvironmentService::GetInstance().SaveConfig(config);
                EnvironmentService::GetInstance().RefreshAsync(true);
                return std::string("Environment configuration saved and refresh started.");
            });

        mcp_server.AddTool("self.environment.refresh",
                           "Refresh standby location, weather, and air-quality data now.",
                           PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                               EnvironmentService::GetInstance().RefreshAsync(true);
                               return std::string("Environment refresh started.");
                           });

        mcp_server.AddTool(
            "self.environment.get_status",
            "Get current standby location and weather status, including configuration errors.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto snapshot = EnvironmentService::GetInstance().GetSnapshot();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "configured", snapshot.configured);
                cJSON_AddBoolToObject(json, "refresh_in_progress", snapshot.refresh_in_progress);
                cJSON_AddBoolToObject(json, "weather_available", snapshot.weather_available);
                cJSON_AddStringToObject(json, "city", snapshot.city.c_str());
                cJSON_AddStringToObject(json, "adcode", snapshot.adcode.c_str());
                cJSON_AddStringToObject(json, "weather", snapshot.weather_text.c_str());
                cJSON_AddStringToObject(json, "temperature_c", snapshot.temperature_c.c_str());
                cJSON_AddStringToObject(json, "last_error", snapshot.last_error.c_str());
                return json;
            });

        mcp_server.AddTool(
            "self.system.enter_standby",
            "Enter standby mode immediately. Use this whenever the user asks to enter standby, "
            "idle, stop everything, or return to the clock and weather dashboard. This stops "
            "music, recording, Pomodoro, previews, and the current voice interaction. Treat this "
            "as the final action for the request and do not speak or call another tool afterward.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                return Application::GetInstance().EnterStandby();
            });
    }
#endif

public:
    WaveshareEsp32p4() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeCodecI2c();
        InitializeLCD();
        InitializeTouch();
        InitializeCamera();
        InitializeButtons();
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        InitializeDashboardTools();
#endif
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual esp_err_t EnsureStorageReady() override {
#if CONFIG_BOARD_TYPE_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        return InitializeSdCard();
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }

    virtual Display *GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

};

DECLARE_BOARD(WaveshareEsp32p4);
