#include "wifi_board.h"
#include "board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "display/emote_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "esp32_camera.h"
#include "mcp_server.h"
#include "assets/lang_config.h"
#include "features/audio_recorder.h"
#include "features/music_player.h"
#include "features/pomodoro_timer.h"
#include "services/environment_service.h"
#include "storage/sd_card_service.h"
#include "system_info.h"

#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <driver/sdmmc_host.h>
#include <esp_heap_caps.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>
#include <lvgl.h>

#include <cstdio>
#include <mutex>

#define TAG "LichuangDevBoard"

namespace {
const char* PomodoroStateName(PomodoroState state) {
    switch (state) {
        case PomodoroState::kRunning:
            return "running";
        case PomodoroState::kPaused:
            return "paused";
        case PomodoroState::kFinished:
            return "finished";
        case PomodoroState::kIdle:
        default:
            return "idle";
    }
}

const char* MusicStateName(MusicPlayerState state) {
    switch (state) {
        case MusicPlayerState::kPlaying:
            return "playing";
        case MusicPlayerState::kPaused:
            return "paused";
        case MusicPlayerState::kError:
            return "error";
        case MusicPlayerState::kIdle:
        default:
            return "idle";
    }
}

const char* RecorderStateName(AudioRecorderState state) {
    switch (state) {
        case AudioRecorderState::kRecording:
            return "recording";
        case AudioRecorderState::kPaused:
            return "paused";
        case AudioRecorderState::kError:
            return "error";
        case AudioRecorderState::kIdle:
        default:
            return "idle";
    }
}

enum class ReleaseCheckLevel {
    kPass,
    kWarn,
    kFail,
};

const char* ReleaseCheckLevelName(ReleaseCheckLevel level) {
    switch (level) {
        case ReleaseCheckLevel::kWarn:
            return "warn";
        case ReleaseCheckLevel::kFail:
            return "fail";
        case ReleaseCheckLevel::kPass:
        default:
            return "pass";
    }
}

void AddReleaseCheck(cJSON* checks, int& warning_count, int& failure_count,
        const char* name, ReleaseCheckLevel level, const std::string& message) {
    if (level == ReleaseCheckLevel::kWarn) {
        warning_count++;
    } else if (level == ReleaseCheckLevel::kFail) {
        failure_count++;
    }

    cJSON* check = cJSON_CreateObject();
    cJSON_AddStringToObject(check, "name", name);
    cJSON_AddStringToObject(check, "level", ReleaseCheckLevelName(level));
    cJSON_AddStringToObject(check, "message", message.c_str());
    cJSON_AddItemToArray(checks, check);
}

void AddRuntimeHeapDiagnostics(cJSON* root) {
    cJSON* runtime = cJSON_CreateObject();
    cJSON_AddNumberToObject(runtime, "free_heap_bytes",
        static_cast<double>(SystemInfo::GetFreeHeapSize()));
    cJSON_AddNumberToObject(runtime, "minimum_free_heap_bytes",
        static_cast<double>(SystemInfo::GetMinimumFreeHeapSize()));
    cJSON_AddNumberToObject(runtime, "internal_free_bytes",
        static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    cJSON_AddNumberToObject(runtime, "internal_minimum_free_bytes",
        static_cast<double>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
    cJSON_AddNumberToObject(runtime, "spiram_free_bytes",
        static_cast<double>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    cJSON_AddNumberToObject(runtime, "spiram_minimum_free_bytes",
        static_cast<double>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));
    cJSON_AddItemToObject(root, "runtime", runtime);
}

cJSON* BuildLichuangDiagnosticsJson(bool scan_media) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "board_type", BOARD_TYPE);
    cJSON_AddStringToObject(root, "board_name", BOARD_NAME);
    cJSON_AddBoolToObject(root, "manual_hardware_verification_required", true);

    auto& storage = SdCardService::GetInstance();
    cJSON* storage_json = cJSON_CreateObject();
    bool mounted = storage.IsMounted();
    cJSON_AddBoolToObject(storage_json, "mounted", mounted);
    if (mounted) {
        cJSON_AddNumberToObject(storage_json, "free_bytes",
            static_cast<double>(storage.GetFreeBytes()));
    }
    if (mounted && scan_media) {
        auto files = storage.ScanMediaFiles();
        cJSON_AddNumberToObject(storage_json, "media_count", files.size());
        cJSON* preview = cJSON_CreateArray();
        size_t preview_count = files.size() < 8 ? files.size() : 8;
        for (size_t i = 0; i < preview_count; ++i) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", files[i].display_name.c_str());
            cJSON_AddStringToObject(item, "path", files[i].path.c_str());
            cJSON_AddStringToObject(item, "type", files[i].type.c_str());
            cJSON_AddNumberToObject(item, "size_bytes",
                static_cast<double>(files[i].size_bytes));
            cJSON_AddItemToArray(preview, item);
        }
        cJSON_AddItemToObject(storage_json, "media_preview", preview);
    } else {
        cJSON_AddBoolToObject(storage_json, "media_scan_skipped", true);
    }
    cJSON_AddItemToObject(root, "storage", storage_json);

    auto& environment = EnvironmentService::GetInstance();
    auto env_config = environment.GetConfig();
    auto env = environment.GetSnapshot();
    cJSON* env_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(env_json, "amap_key_configured", !env_config.amap_api_key.empty());
    cJSON_AddBoolToObject(env_json, "aggregate_url_configured", !env_config.aggregate_url.empty());
    cJSON_AddNumberToObject(env_json, "refresh_interval_minutes",
        env_config.refresh_interval_minutes);
    cJSON_AddBoolToObject(env_json, "refresh_in_progress", env.refresh_in_progress);
    cJSON_AddBoolToObject(env_json, "stale", env.stale);
    cJSON_AddBoolToObject(env_json, "location_available", env.location_available);
    cJSON_AddBoolToObject(env_json, "weather_available", env.weather_available);
    cJSON_AddBoolToObject(env_json, "air_available", env.air_available);
    cJSON_AddStringToObject(env_json, "city", env.city.c_str());
    cJSON_AddStringToObject(env_json, "adcode", env.adcode.c_str());
    cJSON_AddStringToObject(env_json, "weather", env.weather_text.c_str());
    cJSON_AddStringToObject(env_json, "temperature_c", env.temperature_c.c_str());
    cJSON_AddStringToObject(env_json, "humidity", env.humidity.c_str());
    cJSON_AddNumberToObject(env_json, "aqi", env.aqi);
    cJSON_AddNumberToObject(env_json, "pm25", env.pm25);
    cJSON_AddNumberToObject(env_json, "pm10", env.pm10);
    cJSON_AddNumberToObject(env_json, "updated_at", static_cast<double>(env.updated_at));
    cJSON_AddStringToObject(env_json, "last_error", env.last_error.c_str());
    cJSON_AddItemToObject(root, "environment", env_json);

    auto pomodoro = PomodoroTimer::GetInstance().GetStatus();
    cJSON* pomodoro_json = cJSON_CreateObject();
    cJSON_AddStringToObject(pomodoro_json, "state", PomodoroStateName(pomodoro.state));
    cJSON_AddNumberToObject(pomodoro_json, "duration_minutes", pomodoro.duration_minutes);
    cJSON_AddNumberToObject(pomodoro_json, "remaining_seconds", pomodoro.remaining_seconds);
    cJSON_AddNumberToObject(pomodoro_json, "elapsed_seconds", pomodoro.elapsed_seconds);
    cJSON_AddItemToObject(root, "pomodoro", pomodoro_json);

    auto music = MusicPlayer::GetInstance().GetStatus();
    cJSON* music_json = cJSON_CreateObject();
    cJSON_AddStringToObject(music_json, "state", MusicStateName(music.state));
    cJSON_AddNumberToObject(music_json, "current_index", music.current_index);
    cJSON_AddNumberToObject(music_json, "playlist_size", music.playlist_size);
    cJSON_AddStringToObject(music_json, "track_name", music.track_name.c_str());
    cJSON_AddStringToObject(music_json, "track_path", music.track_path.c_str());
    cJSON_AddStringToObject(music_json, "track_type", music.track_type.c_str());
    cJSON_AddNumberToObject(music_json, "sample_rate", music.sample_rate);
    cJSON_AddNumberToObject(music_json, "channels", music.channels);
    cJSON_AddNumberToObject(music_json, "elapsed_seconds", music.elapsed_seconds);
    cJSON_AddNumberToObject(music_json, "duration_seconds", music.duration_seconds);
    cJSON_AddStringToObject(music_json, "last_error", music.last_error.c_str());
    cJSON_AddItemToObject(root, "music", music_json);

    auto recorder = AudioRecorder::GetInstance().GetStatus();
    cJSON* recorder_json = cJSON_CreateObject();
    cJSON_AddStringToObject(recorder_json, "state", RecorderStateName(recorder.state));
    cJSON_AddStringToObject(recorder_json, "file_name", recorder.file_name.c_str());
    cJSON_AddStringToObject(recorder_json, "file_path", recorder.file_path.c_str());
    cJSON_AddNumberToObject(recorder_json, "elapsed_seconds", recorder.elapsed_seconds);
    cJSON_AddNumberToObject(recorder_json, "bytes_written",
        static_cast<double>(recorder.bytes_written));
    cJSON_AddNumberToObject(recorder_json, "free_bytes",
        static_cast<double>(recorder.free_bytes));
    cJSON_AddStringToObject(recorder_json, "last_error", recorder.last_error.c_str());
    cJSON_AddItemToObject(root, "recorder", recorder_json);

    AddRuntimeHeapDiagnostics(root);
    return root;
}

cJSON* BuildLichuangReleaseSelfCheckJson(bool scan_media) {
    constexpr uint64_t kMinimumSdFreeBytes = 1ULL * 1024ULL * 1024ULL;
    constexpr uint64_t kRecommendedSdFreeBytes = 10ULL * 1024ULL * 1024ULL;
    constexpr size_t kMinimumFreeHeapBytes = 64U * 1024U;
    constexpr size_t kMinimumInternalHeapBytes = 32U * 1024U;
    constexpr size_t kRecommendedInternalHeapBytes = 64U * 1024U;

    int warning_count = 0;
    int failure_count = 0;
    cJSON* root = cJSON_CreateObject();
    cJSON* checks = cJSON_CreateArray();

    auto& storage = SdCardService::GetInstance();
    bool mounted = storage.IsMounted();
    AddReleaseCheck(checks, warning_count, failure_count, "sd_mounted",
        mounted ? ReleaseCheckLevel::kPass : ReleaseCheckLevel::kFail,
        mounted ? "SD card is mounted." :
            "SD card is not mounted; music, recording, and media-list tests cannot pass.");

    uint64_t sd_free_bytes = 0;
    if (mounted) {
        sd_free_bytes = storage.GetFreeBytes();
        ReleaseCheckLevel level = ReleaseCheckLevel::kPass;
        std::string message = "SD free space is sufficient for baseline feature tests.";
        if (sd_free_bytes < kMinimumSdFreeBytes) {
            level = ReleaseCheckLevel::kFail;
            message = "SD free space is below the recorder start threshold.";
        } else if (sd_free_bytes < kRecommendedSdFreeBytes) {
            level = ReleaseCheckLevel::kWarn;
            message = "SD free space is low; short smoke tests may pass, but release regression should use a larger card.";
        }
        AddReleaseCheck(checks, warning_count, failure_count, "sd_free_space", level, message);
    }

    size_t media_count = 0;
    if (mounted && scan_media) {
        auto files = storage.ScanMediaFiles();
        media_count = files.size();
        AddReleaseCheck(checks, warning_count, failure_count, "media_playlist",
            media_count > 0 ? ReleaseCheckLevel::kPass : ReleaseCheckLevel::kWarn,
            media_count > 0 ? "At least one WAV/MP3 file is visible to the media scanner." :
                "No WAV/MP3 files were found; music playback regression needs test files under /sdcard/music or /sdcard/recordings.");
    } else if (mounted) {
        AddReleaseCheck(checks, warning_count, failure_count, "media_playlist",
            ReleaseCheckLevel::kWarn,
            "Media scan was skipped; rerun with scan_media=true before final release handoff.");
    }

    auto& environment = EnvironmentService::GetInstance();
    auto env_config = environment.GetConfig();
    auto env = environment.GetSnapshot();
    AddReleaseCheck(checks, warning_count, failure_count, "environment_config",
        env_config.amap_api_key.empty() ? ReleaseCheckLevel::kWarn : ReleaseCheckLevel::kPass,
        env_config.amap_api_key.empty() ?
            "AMap API key is not configured; standby weather will remain unavailable." :
            "AMap API key is configured.");
    AddReleaseCheck(checks, warning_count, failure_count, "weather_snapshot",
        env.weather_available ? ReleaseCheckLevel::kPass : ReleaseCheckLevel::kWarn,
        env.weather_available ?
            "Weather snapshot is available for the standby home page." :
            "Weather snapshot is not available yet; refresh network/API config during hardware verification.");
    AddReleaseCheck(checks, warning_count, failure_count, "air_quality_snapshot",
        env.air_available ? ReleaseCheckLevel::kPass : ReleaseCheckLevel::kWarn,
        env.air_available ?
            "AQI snapshot is available for the standby home page." :
            "AQI snapshot is unavailable; configure a valid aggregate endpoint if AQI is required for release.");

    auto pomodoro = PomodoroTimer::GetInstance().GetStatus();
    AddReleaseCheck(checks, warning_count, failure_count, "pomodoro_service",
        ReleaseCheckLevel::kPass,
        std::string("Pomodoro service is reachable; current state is ") +
            PomodoroStateName(pomodoro.state) + ".");

    auto music = MusicPlayer::GetInstance().GetStatus();
    AddReleaseCheck(checks, warning_count, failure_count, "music_service",
        music.state == MusicPlayerState::kError ? ReleaseCheckLevel::kFail : ReleaseCheckLevel::kPass,
        music.state == MusicPlayerState::kError ?
            std::string("Music service is in error state: ") + music.last_error :
            std::string("Music service is reachable; current state is ") +
                MusicStateName(music.state) + ".");

    auto recorder = AudioRecorder::GetInstance().GetStatus();
    AddReleaseCheck(checks, warning_count, failure_count, "recorder_service",
        recorder.state == AudioRecorderState::kError ? ReleaseCheckLevel::kFail : ReleaseCheckLevel::kPass,
        recorder.state == AudioRecorderState::kError ?
            std::string("Recorder service is in error state: ") + recorder.last_error :
            std::string("Recorder service is reachable; current state is ") +
                RecorderStateName(recorder.state) + ".");

    size_t free_heap = SystemInfo::GetFreeHeapSize();
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ReleaseCheckLevel heap_level = ReleaseCheckLevel::kPass;
    std::string heap_message = "Runtime heap headroom is above the release self-check thresholds.";
    if (free_heap < kMinimumFreeHeapBytes || internal_free < kMinimumInternalHeapBytes) {
        heap_level = ReleaseCheckLevel::kFail;
        heap_message = "Runtime heap headroom is below the minimum self-check threshold.";
    } else if (internal_free < kRecommendedInternalHeapBytes) {
        heap_level = ReleaseCheckLevel::kWarn;
        heap_message = "Internal heap headroom is low; monitor MP3 playback and recorder tests carefully.";
    }
    AddReleaseCheck(checks, warning_count, failure_count, "runtime_heap", heap_level, heap_message);

    cJSON_AddStringToObject(root, "status",
        failure_count > 0 ? "fail" : warning_count > 0 ? "warn" : "pass");
    cJSON_AddNumberToObject(root, "failures", failure_count);
    cJSON_AddNumberToObject(root, "warnings", warning_count);
    cJSON_AddBoolToObject(root, "manual_hardware_verification_required", true);
    cJSON_AddBoolToObject(root, "media_scan_requested", scan_media);
    cJSON_AddNumberToObject(root, "sd_free_bytes", static_cast<double>(sd_free_bytes));
    cJSON_AddNumberToObject(root, "media_count", static_cast<double>(media_count));
    cJSON_AddNumberToObject(root, "free_heap_bytes", static_cast<double>(free_heap));
    cJSON_AddNumberToObject(root, "internal_free_bytes", static_cast<double>(internal_free));
    cJSON_AddItemToObject(root, "checks", checks);
    return root;
}
} // namespace

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class CustomAudioCodec : public BoxAudioCodec {
private:
    Pca9557* pca9557_;

public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557* pca9557) 
        : BoxAudioCodec(i2c_bus, 
                       AUDIO_INPUT_SAMPLE_RATE, 
                       AUDIO_OUTPUT_SAMPLE_RATE,
                       AUDIO_I2S_GPIO_MCLK, 
                       AUDIO_I2S_GPIO_BCLK, 
                       AUDIO_I2S_GPIO_WS, 
                       AUDIO_I2S_GPIO_DOUT, 
                       AUDIO_I2S_GPIO_DIN,
                       GPIO_NUM_NC, 
                       AUDIO_CODEC_ES8311_ADDR, 
                       AUDIO_CODEC_ES7210_ADDR, 
                       AUDIO_INPUT_REFERENCE),
          pca9557_(pca9557) {
    }

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            pca9557_->SetOutputState(1, 1);
        } else {
            pca9557_->SetOutputState(1, 0);
        }
    }
};

class LichuangDevBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    Display* display_;
    Pca9557* pca9557_;
    Esp32Camera* camera_;
    std::mutex sd_mutex_;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
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

        // Initialize PCA9557
        pca9557_ = new Pca9557(i2c_bus_, 0x19);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_40;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_41;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
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

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC;
        io_config.dc_gpio_num = GPIO_NUM_39;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        pca9557_->SetOutputState(0, 0);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

#if CONFIG_USE_EMOTE_MESSAGE_STYLE
        display_ = new emote::EmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#else
        display_ = new SpiLcdDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#endif
    }

    void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_HEIGHT,
            .y_max = DISPLAY_WIDTH,
            .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
            .int_gpio_num = GPIO_NUM_NC, 
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 1,
                .mirror_x = 1,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 8,
            .flags =
            {
                .disable_control_phase = 1,
            }
        };
        tp_io_config.scl_speed_hz = 400000;

        esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create touch I2C panel IO: %s", esp_err_to_name(ret));
            return;
        }
        ret = esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);
        if (ret != ESP_OK || tp == nullptr) {
            ESP_LOGW(TAG, "Failed to initialize FT5x06 touch controller: %s",
                esp_err_to_name(ret));
            return;
        }

        /* Add touch input (for selected screen) */
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(), 
            .handle = tp,
        };

        if(touch_cfg.disp) {
            lvgl_port_add_touch(&touch_cfg);
        } else {
            ESP_LOGE(TAG, "Touch display is not initialized");
        }
    }

    void InitializeCamera() {
        // Open camera power
        pca9557_->SetOutputState(2, 0);

        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_2;
        config.ledc_timer = LEDC_TIMER_2;
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = -1;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 1;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

        camera_ = new Esp32Camera(config);
    }

    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "End this conversation and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                EnterWifiConfigMode();
                return true;
            });

        auto& pomodoro = PomodoroTimer::GetInstance();
        pomodoro.SetFinishCallback([]() {
            Application::GetInstance().Schedule([]() {
                auto display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->ShowNotification("Pomodoro finished", 5000);
                }
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
            });
        });

        mcp_server.AddTool("self.pomodoro.start",
            "Start a Pomodoro timer. The duration must be between 1 and 180 minutes.",
            PropertyList({
                Property("duration_minutes", kPropertyTypeInteger)
            }), [](const PropertyList& properties) -> ReturnValue {
                int duration_minutes = properties["duration_minutes"].value<int>();
                esp_err_t ret = PomodoroTimer::GetInstance().Start(duration_minutes);
                if (ret != ESP_OK) {
                    return std::string("Duration must be between 1 and 180 minutes.");
                }
                auto display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->ShowPomodoroTimerView();
                }
                return std::string("Pomodoro started for ") +
                    std::to_string(duration_minutes) + " minutes.";
            });

        mcp_server.AddTool("self.pomodoro.pause",
            "Pause the current Pomodoro timer.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                PomodoroTimer::GetInstance().Pause();
                return PomodoroTimer::GetInstance().GetStatusText();
            });

        mcp_server.AddTool("self.pomodoro.resume",
            "Resume the paused Pomodoro timer.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                PomodoroTimer::GetInstance().Resume();
                return PomodoroTimer::GetInstance().GetStatusText();
            });

        mcp_server.AddTool("self.pomodoro.cancel",
            "Cancel the current Pomodoro timer.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                PomodoroTimer::GetInstance().Cancel();
                return std::string("Pomodoro canceled.");
            });

        mcp_server.AddTool("self.pomodoro.get_status",
            "Get the current Pomodoro timer status and remaining time.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                return PomodoroTimer::GetInstance().GetStatusText();
            });

        mcp_server.AddUserOnlyTool("self.environment.set_config",
            "Configure standby weather and air-quality data sources. The AMap key is stored in NVS and must not be hardcoded.",
            PropertyList({
                Property("amap_api_key", kPropertyTypeString),
                Property("aggregate_url", kPropertyTypeString, ""),
                Property("refresh_interval_minutes", kPropertyTypeInteger, 30, 5, 1440),
                Property("fallback_adcode", kPropertyTypeString, ""),
                Property("fallback_city", kPropertyTypeString, "")
            }), [](const PropertyList& properties) -> ReturnValue {
                EnvironmentConfig config;
                config.amap_api_key = properties["amap_api_key"].value<std::string>();
                config.aggregate_url = properties["aggregate_url"].value<std::string>();
                config.refresh_interval_minutes = properties["refresh_interval_minutes"].value<int>();
                config.fallback_adcode = properties["fallback_adcode"].value<std::string>();
                config.fallback_city = properties["fallback_city"].value<std::string>();
                EnvironmentService::GetInstance().SaveConfig(config);
                EnvironmentService::GetInstance().RefreshAsync(true);
                return std::string("Environment configuration saved.");
            });

        mcp_server.AddTool("self.environment.refresh",
            "Refresh standby weather and air-quality data now.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                EnvironmentService::GetInstance().RefreshAsync(true);
                return std::string("Environment refresh started.");
            });

        mcp_server.AddTool("self.environment.get_status",
            "Get current standby weather and air-quality status.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto snapshot = EnvironmentService::GetInstance().GetSnapshot();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "configured", snapshot.configured);
                cJSON_AddBoolToObject(json, "refresh_in_progress", snapshot.refresh_in_progress);
                cJSON_AddBoolToObject(json, "weather_available", snapshot.weather_available);
                cJSON_AddBoolToObject(json, "air_available", snapshot.air_available);
                cJSON_AddStringToObject(json, "city", snapshot.city.c_str());
                cJSON_AddStringToObject(json, "adcode", snapshot.adcode.c_str());
                cJSON_AddStringToObject(json, "weather", snapshot.weather_text.c_str());
                cJSON_AddStringToObject(json, "temperature_c", snapshot.temperature_c.c_str());
                cJSON_AddNumberToObject(json, "aqi", snapshot.aqi);
                cJSON_AddStringToObject(json, "air_category", snapshot.air_category.c_str());
                cJSON_AddStringToObject(json, "last_error", snapshot.last_error.c_str());
                return json;
            });

        mcp_server.AddTool("self.music.play",
            "Play a local WAV or MP3 track from the SD card. Omit index or pass -1 to play the current or first track.",
            PropertyList({
                Property("index", kPropertyTypeInteger, -1, -1, 199)
            }), [](const PropertyList& properties) -> ReturnValue {
                int index = properties["index"].value<int>();
                esp_err_t ret = MusicPlayer::GetInstance().Play(index);
                if (ret != ESP_OK) {
                    return MusicPlayer::GetInstance().GetStatusText();
                }
                return std::string("Music playback started.");
            });

        mcp_server.AddTool("self.music.pause",
            "Pause local music playback.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                MusicPlayer::GetInstance().Pause();
                return MusicPlayer::GetInstance().GetStatusText();
            });

        mcp_server.AddTool("self.music.next",
            "Play the next local WAV or MP3 track in newest-first playlist order.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                esp_err_t ret = MusicPlayer::GetInstance().Next();
                if (ret != ESP_OK) {
                    return MusicPlayer::GetInstance().GetStatusText();
                }
                return std::string("Next track started.");
            });

        mcp_server.AddTool("self.music.previous",
            "Play the previous local WAV or MP3 track in newest-first playlist order.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                esp_err_t ret = MusicPlayer::GetInstance().Previous();
                if (ret != ESP_OK) {
                    return MusicPlayer::GetInstance().GetStatusText();
                }
                return std::string("Previous track started.");
            });

        mcp_server.AddTool("self.music.list",
            "List local WAV and MP3 tracks on the SD card.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto files = MusicPlayer::GetInstance().GetPlaylist();
                auto status = MusicPlayer::GetInstance().GetStatus();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "count", files.size());
                cJSON_AddNumberToObject(json, "current_index", status.current_index);
                cJSON* tracks = cJSON_CreateArray();
                for (size_t i = 0; i < files.size(); ++i) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddNumberToObject(item, "index", i);
                    cJSON_AddStringToObject(item, "name", files[i].display_name.c_str());
                    cJSON_AddStringToObject(item, "path", files[i].path.c_str());
                    cJSON_AddStringToObject(item, "type", files[i].type.c_str());
                    cJSON_AddNumberToObject(item, "size_bytes",
                        static_cast<double>(files[i].size_bytes));
                    cJSON_AddItemToArray(tracks, item);
                }
                cJSON_AddItemToObject(json, "tracks", tracks);
                return json;
            });

        mcp_server.AddTool("self.music.get_status",
            "Get current local music playback status.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto status = MusicPlayer::GetInstance().GetStatus();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "state",
                    status.state == MusicPlayerState::kPlaying ? "playing" :
                    status.state == MusicPlayerState::kPaused ? "paused" :
                    status.state == MusicPlayerState::kError ? "error" : "idle");
                cJSON_AddNumberToObject(json, "current_index", status.current_index);
                cJSON_AddNumberToObject(json, "playlist_size", status.playlist_size);
                cJSON_AddStringToObject(json, "track_name", status.track_name.c_str());
                cJSON_AddStringToObject(json, "track_path", status.track_path.c_str());
                cJSON_AddStringToObject(json, "track_type", status.track_type.c_str());
                cJSON_AddNumberToObject(json, "sample_rate", status.sample_rate);
                cJSON_AddNumberToObject(json, "channels", status.channels);
                cJSON_AddNumberToObject(json, "elapsed_seconds", status.elapsed_seconds);
                cJSON_AddNumberToObject(json, "duration_seconds", status.duration_seconds);
                cJSON_AddStringToObject(json, "last_error", status.last_error.c_str());
                return json;
            });

        mcp_server.AddTool("self.recorder.start",
            "Start recording microphone audio to a WAV file on the SD card.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                esp_err_t ret = AudioRecorder::GetInstance().Start();
                if (ret != ESP_OK) {
                    return AudioRecorder::GetInstance().GetStatusText();
                }
                return std::string("Recording started.");
            });

        mcp_server.AddTool("self.recorder.pause",
            "Pause the current WAV recording.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                AudioRecorder::GetInstance().Pause();
                return AudioRecorder::GetInstance().GetStatusText();
            });

        mcp_server.AddTool("self.recorder.resume",
            "Resume the paused WAV recording.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                esp_err_t ret = AudioRecorder::GetInstance().Resume();
                if (ret != ESP_OK) {
                    return AudioRecorder::GetInstance().GetStatusText();
                }
                return std::string("Recording resumed.");
            });

        mcp_server.AddTool("self.recorder.stop",
            "Stop the current WAV recording and finalize the file header.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                AudioRecorder::GetInstance().Stop();
                return AudioRecorder::GetInstance().GetStatusText();
            });

        mcp_server.AddTool("self.recorder.get_status",
            "Get current WAV recorder status.",
            PropertyList(), [](const PropertyList& properties) -> ReturnValue {
                auto status = AudioRecorder::GetInstance().GetStatus();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "state",
                    status.state == AudioRecorderState::kRecording ? "recording" :
                    status.state == AudioRecorderState::kPaused ? "paused" :
                    status.state == AudioRecorderState::kError ? "error" : "idle");
                cJSON_AddStringToObject(json, "file_name", status.file_name.c_str());
                cJSON_AddStringToObject(json, "file_path", status.file_path.c_str());
                cJSON_AddNumberToObject(json, "elapsed_seconds", status.elapsed_seconds);
                cJSON_AddNumberToObject(json, "bytes_written", status.bytes_written);
                cJSON_AddNumberToObject(json, "free_bytes",
                    static_cast<double>(status.free_bytes));
                cJSON_AddStringToObject(json, "last_error", status.last_error.c_str());
                return json;
            });

        mcp_server.AddTool("self.lichuang.get_diagnostics",
            "Get a consolidated Lichuang Dev feature diagnostic snapshot for hardware regression testing. "
            "Set scan_media to true only when you want to scan the SD card media list.",
            PropertyList({
                Property("scan_media", kPropertyTypeBoolean, false)
            }), [](const PropertyList& properties) -> ReturnValue {
                bool scan_media = properties["scan_media"].value<bool>();
                return BuildLichuangDiagnosticsJson(scan_media);
            });

        mcp_server.AddTool("self.lichuang.run_release_self_check",
            "Run a lightweight release-readiness self-check for Lichuang Dev features. "
            "This does not replace manual hardware verification.",
            PropertyList({
                Property("scan_media", kPropertyTypeBoolean, false)
            }), [](const PropertyList& properties) -> ReturnValue {
                bool scan_media = properties["scan_media"].value<bool>();
                return BuildLichuangReleaseSelfCheckJson(scan_media);
            });
    }

    esp_err_t InitializeSdCard() {
        std::lock_guard<std::mutex> lock(sd_mutex_);
        auto& storage = SdCardService::GetInstance();
        if (storage.IsMounted()) {
            return ESP_OK;
        }

        gpio_set_pull_mode(SDCARD_SDMMC_CLK_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SDMMC_CMD_PIN, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(SDCARD_SDMMC_D0_PIN, GPIO_PULLUP_ONLY);

        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        slot_config.clk = SDCARD_SDMMC_CLK_PIN;
        slot_config.cmd = SDCARD_SDMMC_CMD_PIN;
        slot_config.d0 = SDCARD_SDMMC_D0_PIN;
        slot_config.width = SDCARD_SDMMC_BUS_WIDTH;
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = true,
        };

        sdmmc_card_t* card = nullptr;
        esp_err_t ret = esp_vfs_fat_sdmmc_mount(
            SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to mount SD card at %s: %s",
                SDCARD_MOUNT_POINT, esp_err_to_name(ret));
            return ret;
        }

        sdmmc_card_print_info(stdout, card);
        ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);

        ret = storage.EnsureMediaDirectories();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to prepare SD media directories: %s",
                esp_err_to_name(ret));
            return ret;
        }
        return ESP_OK;
    }

    void InitializeSdCardAsync() {
        BaseType_t ret = xTaskCreate([](void* arg) {
            auto* self = static_cast<LichuangDevBoard*>(arg);
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_err_t sd_ret = self->InitializeSdCard();
            if (sd_ret != ESP_OK) {
                ESP_LOGW(TAG, "Deferred SD card initialization failed: %s",
                    esp_err_to_name(sd_ret));
            }
            vTaskDelete(nullptr);
        }, "sdcard_init", 4096, this, 2, nullptr);
        if (ret != pdPASS) {
            ESP_LOGW(TAG, "Failed to create deferred SD card initialization task");
        }
    }

public:
    LichuangDevBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        GetBacklight()->RestoreBrightness();
        InitializeTouch();
        InitializeButtons();
        InitializeCamera();
        InitializeTools();
        InitializeSdCardAsync();
    }

    virtual esp_err_t EnsureStorageReady() override {
        return InitializeSdCard();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CustomAudioCodec audio_codec(
            i2c_bus_, 
            pca9557_);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(LichuangDevBoard);
