# Lichuang Dev AI Features PRD and Technical Plan

Date: 2026-07-05

## 1. Background

This document records the product requirements and technical plan for extending the xiaozhi-esp32 firmware on the Lichuang Dev ESP32-S3 board. The target board is the "立创实战派" board using ESP32-S3-WROOM-1-N16R8, ST7789 display, FT6X36/FT6336 touch, ES8311 audio DAC, ES7210 audio ADC, NS4150B power amplifier, and MicroSD card slot.

The implementation decision is intentionally deferred until the requirements and hardware constraints are clear. This document is the handoff artifact for later development.

## 2. Source Inputs

- Hardware netlist: `C:\Users\admin\Downloads\Netlist_ESP32-S3-V1_0_1_2026-07-05.net`
- Hardware schematic: `C:\Users\admin\Downloads\SCH_ESP32-S3-V1_0_1_2026-07-05.pdf`
- Hardware parameter image: `C:\Users\admin\Desktop\立创实战派硬件参数.png`
- Current board source:
  - `main/boards/lichuang-dev/lichuang_dev_board.cc`
  - `main/boards/lichuang-dev/config.h`
- Common display/status code:
  - `main/display/lvgl_display/lvgl_display.cc`
  - `main/boards/common/adc_battery_monitor.cc`

## 3. Confirmed Product Requirements

### 3.1 Standby Screen

When Xiaozhi AI is idle and not awakened, the device immediately shows a standby screen. The screen remains always on.

The standby screen shall show:

- Current time
- Date
- Weekday
- Local weather
- Local air quality
- Top bar with battery and Wi-Fi/network status

Location behavior:

- The device shall locate the city through Wi-Fi/IP after network connection.
- The device shall then use city information, preferably `adcode`, to request weather data.

Weather and air quality behavior:

- Weather data shall use AMap/Gaode Web Service API.
- Air quality can use a third-party or self-hosted aggregation API.
- AMap API key shall be stored through configuration, such as `menuconfig` or NVS, and must not be hardcoded.

### 3.2 Pomodoro Timer

The Pomodoro screen shall provide default quick duration choices:

- 15 minutes
- 30 minutes
- 45 minutes

Voice control shall support natural phrases equivalent to:

- "开始 25 分钟番茄钟"
- "暂停番茄钟"
- "继续番茄钟"
- "取消番茄钟"
- "番茄钟还剩多久"

Custom duration constraints:

- Allowed range: 1 to 180 minutes.
- If the requested duration is outside the range, the device shall reply by voice that the duration must be between 1 and 180 minutes.

Completion behavior:

- When the timer finishes, the device shall notify the user by voice announcement.

### 3.3 Music Playback

The music playback screen shall provide:

- Previous track
- Next track
- Play
- Pause
- Music list

Music files:

- Files come from the hardware MicroSD card.
- Supported formats for the first version: MP3 and WAV.
- Music directory: `/sdcard/music`
- Recording directory: `/sdcard/recordings`
- The music list shall include playable MP3/WAV files from both directories.
- Sort order: newest modified time first.

Voice interaction conflict:

- If music is playing and Xiaozhi is awakened, music shall pause automatically.

### 3.4 Recording

The recording feature shall support voice and touch UI control.

Voice control shall support natural phrases equivalent to:

- "开始录音"
- "暂停录音"
- "继续录音"
- "停止录音"

Manual UI controls:

- Record
- Pause

Recording format:

- WAV

File naming:

- `REC_YYYYMMDD_HHMMSS.wav`
- Example: `REC_20260705_183000.wav`

Storage:

- Save recordings to `/sdcard/recordings`.
- Saved recording files shall be visible in the music playback list.

Recording limits:

- Default maximum single recording duration: 60 minutes.
- If SD card free space is insufficient, stop recording and notify the user by voice.
- If Xiaozhi is awakened during recording, recording shall pause automatically.

### 3.5 Main UI Navigation

The standby experience shall use touch gestures:

- Swipe left/right to switch between Pomodoro, Music, and Recording screens.
- The standby/home screen remains the default idle screen.

## 4. Hardware and IO Verification

### 4.1 Matched IO

The following netlist connections match the current `lichuang-dev` source configuration:

| Function | Netlist | Current source |
|---|---:|---:|
| I2C SDA | GPIO1 | `AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_1` |
| I2C SCL | GPIO2 | `AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_2` |
| I2S DIN | GPIO12 | `AUDIO_I2S_GPIO_DIN GPIO_NUM_12` |
| I2S WS | GPIO13 | `AUDIO_I2S_GPIO_WS GPIO_NUM_13` |
| I2S BCLK | GPIO14 | `AUDIO_I2S_GPIO_BCLK GPIO_NUM_14` |
| I2S MCLK | GPIO38 | `AUDIO_I2S_GPIO_MCLK GPIO_NUM_38` |
| I2S DOUT | GPIO45 | `AUDIO_I2S_GPIO_DOUT GPIO_NUM_45` |
| LCD DC | GPIO39 | `io_config.dc_gpio_num = GPIO_NUM_39` |
| LCD MOSI | GPIO40 | `buscfg.mosi_io_num = GPIO_NUM_40` |
| LCD SCK | GPIO41 | `buscfg.sclk_io_num = GPIO_NUM_41` |
| LCD backlight | GPIO42 | `DISPLAY_BACKLIGHT_PIN GPIO_NUM_42` |
| Boot button | GPIO0 | `BOOT_BUTTON_GPIO GPIO_NUM_0` |
| DVP camera pins | GPIO3/4/5/6/7/8/9/15/16/17/18/46 | Current camera config matches |

### 4.2 PCA9557 Signals

The board uses PCA9557 on the shared I2C bus. Current source behavior and netlist naming indicate:

| Signal | Netlist | Current source usage |
|---|---|---|
| LCD control | `LCD_CS` | PCA9557 output bit is used during display init |
| PA enable | `PA_EN` | `CustomAudioCodec::EnableOutput()` toggles PCA9557 bit 1 |
| Camera power-down | `DVP_PWDN` | `InitializeCamera()` toggles PCA9557 bit 2 |

### 4.3 SD Card

Netlist confirms the MicroSD card is connected in 1-bit SDMMC style:

| SD signal | GPIO |
|---|---:|
| DAT0 | GPIO21 |
| CLK | GPIO47 |
| CMD | GPIO48 |

Current source issue:

- `main/boards/lichuang-dev/config.h` currently defines `BUILTIN_LED_GPIO GPIO_NUM_48`.
- Netlist defines GPIO48 as `IO48_SD_CMD`.
- To enable SD card safely, GPIO48 must be reserved for SD CMD.
- The board should not use GPIO48 as a built-in LED in the Lichuang Dev build.

Decision:

- Use SDMMC 1-bit mode with CMD GPIO48, CLK GPIO47, DAT0 GPIO21.
- Disable or replace the current built-in LED configuration for this board.

### 4.4 Battery and Power Detection

The schematic and netlist show two SY8088AAC DC-DC buck converters. Their surrounding resistors include:

- R18: 150 kOhm
- R24: 150 kOhm
- R19: 10 kOhm

These are part of power conversion/feedback networks, not an ESP32 battery voltage sampling path.

The schematic/netlist also show:

- `ADC_3V3` connected to ES7210 audio ADC power/reference circuitry.
- `ADCVREF` belonging to the audio ADC reference domain.
- No confirmed `VBAT`, battery charger, fuel gauge, or ESP32 ADC-connected battery divider was found.

Decision:

- Reuse the existing status bar battery icon mechanism where possible.
- Do not claim real battery percentage support unless a real battery ADC or PMIC signal is later confirmed.
- If later hardware revision exposes battery voltage, use `main/boards/common/adc_battery_monitor.cc` with board-specific ADC unit/channel and resistor ratio.

## 5. External Data APIs

### 5.1 Weather

Use AMap/Gaode Web Service API for weather.

Suggested flow:

1. Device connects to Wi-Fi.
2. Device calls IP location API to get city/adcode.
3. Device calls weather API with `adcode`.
4. Device caches the weather result locally.
5. Standby UI refreshes from cache and triggers periodic refresh.

Configuration:

- Store AMap key in NVS or menuconfig.
- Do not hardcode the key.

### 5.2 Air Quality

Because the AMap weather API does not provide all required AQI data in the weather query, use a self-hosted or third-party aggregation endpoint.

Recommended self-hosted interface:

```http
GET /api/env?adcode=440300
```

Recommended response:

```json
{
  "adcode": "440300",
  "city": "深圳市",
  "weather": {
    "text": "多云",
    "temperature_c": 30,
    "humidity": 75,
    "wind_direction": "东南",
    "wind_power": "3"
  },
  "air": {
    "aqi": 42,
    "category": "优",
    "pm25": 18,
    "pm10": 35
  },
  "updated_at": 1783252800
}
```

Device-side fallback:

- If AQI request fails, show weather normally and show air quality as unavailable.
- If location fails, use the last cached city/adcode.

## 6. Proposed Firmware Architecture

### 6.1 Board Layer

Update Lichuang Dev board support later:

- Add SD card initialization for SDMMC 1-bit mode.
- Remove GPIO48 from LED usage.
- Keep existing display, touch, camera, audio, and PCA9557 setup.
- Keep top status bar Wi-Fi and battery update path through `LvglDisplay::UpdateStatusBar`.

Potential files:

- `main/boards/lichuang-dev/config.h`
- `main/boards/lichuang-dev/lichuang_dev_board.cc`

### 6.2 Storage Layer

Add a storage service:

- Mount `/sdcard`.
- Ensure `/sdcard/music` and `/sdcard/recordings` exist.
- Scan MP3/WAV files.
- Read file metadata and modification time.
- Provide newest-first list to UI and music player.

Suggested module:

- `main/storage/sd_card_service.h`
- `main/storage/sd_card_service.cc`

### 6.3 Environment Data Layer

Add an environment service:

- IP location
- Weather query
- AQI aggregation query
- Cache result
- Expose result to standby UI

Suggested module:

- `main/services/environment_service.h`
- `main/services/environment_service.cc`

Configuration keys:

- `amap.api_key`
- `env.aggregate_url`
- `env.refresh_interval_minutes`

### 6.4 Pomodoro Layer

Add a Pomodoro service:

- Start duration
- Pause
- Resume
- Cancel
- Query remaining time
- Completion callback
- Voice announcement on completion

Suggested module:

- `main/features/pomodoro_timer.h`
- `main/features/pomodoro_timer.cc`

### 6.5 Music Player Layer

Add a music player service:

- Build playlist from `/sdcard/music` and `/sdcard/recordings`
- Play MP3/WAV
- Pause/resume
- Previous/next
- Stop/pause automatically on wake word or chat start

Important technical note:

- Current audio pipeline is optimized for Opus voice interaction. Local MP3/WAV playback needs either an ESP-IDF audio decoder path or a lightweight decoder integrated into the existing audio output path.
- WAV playback is simpler than MP3.
- MP3 support requires confirming available decoder component and memory footprint.

Suggested module:

- `main/features/music_player.h`
- `main/features/music_player.cc`

### 6.6 Recording Layer

Add a recorder service:

- Capture microphone PCM from the audio input path.
- Write WAV header and PCM data to `/sdcard/recordings`.
- Pause/resume/stop.
- Enforce 60-minute max duration.
- Monitor free space before and during recording.
- Pause automatically on wake word/chat start.

Suggested module:

- `main/features/audio_recorder.h`
- `main/features/audio_recorder.cc`

### 6.7 UI Layer

Extend LVGL display implementation:

- Standby home screen with time/date/weekday/weather/AQI.
- Swipe navigation between Pomodoro, Music, and Recording views.
- Reuse existing top status bar battery and Wi-Fi labels.
- Keep current chat UI behavior for awakened/listening/speaking states.

Potential files:

- `main/display/lvgl_display/lvgl_display.h`
- `main/display/lvgl_display/lvgl_display.cc`
- New feature-specific UI helpers if needed.

### 6.8 Voice Tool Integration

Register new MCP/self tools so voice dialogue can control features:

Pomodoro tools:

- `self.pomodoro.start`
- `self.pomodoro.pause`
- `self.pomodoro.resume`
- `self.pomodoro.cancel`
- `self.pomodoro.get_status`

Music tools:

- `self.music.play`
- `self.music.pause`
- `self.music.next`
- `self.music.previous`
- `self.music.list`

Recording tools:

- `self.recorder.start`
- `self.recorder.pause`
- `self.recorder.resume`
- `self.recorder.stop`
- `self.recorder.get_status`

These tools should be registered after the feature services are initialized.

## 7. State and Interaction Rules

### 7.1 Idle and Standby

- When device state is idle, show standby UI immediately.
- Existing status bar update logic should continue to refresh Wi-Fi and battery icons.
- Time display should refresh at least once per minute.

### 7.2 Wake Word Priority

When wake word or chat starts:

- Pause music if playing.
- Pause recording if recording.
- Keep Pomodoro timer running unless the user explicitly pauses/cancels it.

### 7.3 Audio Resource Priority

Priority order:

1. Xiaozhi voice interaction
2. Recording control transition
3. Pomodoro completion announcement
4. Music playback

If a Pomodoro completes while music is playing:

- Pause or duck music.
- Play voice announcement.
- Resume music only if it was playing before the announcement and no chat session is active.

## 8. Implementation Phases

### Phase 1: Hardware Enablement

- Fix GPIO48 conflict by removing built-in LED usage.
- Add SDMMC 1-bit mount for GPIO21/47/48.
- Verify SD card mount, file scan, and directory creation.
- Confirm display/touch/audio still work after SD enablement.

### Phase 2: UI Shell

- Add standby home screen.
- Add swipe navigation.
- Add Pomodoro/Music/Recording UI shells.
- Reuse existing top status bar.

### Phase 3: Pomodoro

- Implement timer service.
- Add UI controls.
- Add voice tools.
- Add voice completion notification.

### Phase 4: SD Music

- Implement file scanner.
- Implement WAV playback first.
- Add MP3 playback after decoder component is confirmed.
- Add wake-word auto-pause behavior.

### Phase 5: Recording

- Implement WAV recorder.
- Add duration and free-space checks.
- Add voice and UI controls.
- Add wake-word auto-pause behavior.
- Confirm recorded files appear in music list.

### Phase 6: Weather and AQI

- Add AMap key configuration.
- Add IP location and weather request.
- Add AQI aggregation endpoint support.
- Add cache and failure fallback.

## 9. Risks and Open Items

| Item | Risk | Current decision |
|---|---|---|
| Battery percentage | No battery ADC/PMIC path found in schematic/netlist | Keep existing status bar path; mark true percentage as hardware-dependent |
| GPIO48 conflict | GPIO48 is SD CMD but current source uses it as LED | Remove/reassign LED before SD enablement |
| MP3 decoding | Decoder availability and RAM/CPU cost need validation | Implement WAV first; add MP3 after component check |
| Audio pipeline sharing | Voice, music, and recording compete for audio resources | Define voice interaction as highest priority |
| AQI source | AMap weather API alone may not satisfy AQI requirement | Use self-hosted/third-party aggregation endpoint |
| IP location accuracy | IP location can be wrong under VPN/carrier NAT | Cache location and allow future manual override |

## 10. Acceptance Criteria

### Standby

- On idle, standby screen appears immediately.
- Time/date/weekday display correctly after system time is set.
- Weather and AQI show cached data or unavailable state gracefully.
- Wi-Fi icon updates with connection status.
- Battery icon behavior is not regressed.

### Pomodoro

- 15/30/45 minute quick buttons work.
- Voice start/pause/resume/cancel/status commands work.
- Custom duration rejects values outside 1-180 minutes.
- Completion triggers voice announcement.

### Music

- SD card mounts successfully.
- MP3/WAV files under `/sdcard/music` and `/sdcard/recordings` are listed newest-first.
- Previous/next/play/pause UI controls work.
- Music pauses when Xiaozhi is awakened.

### Recording

- Voice and UI controls start/pause/resume/stop recording.
- WAV file is saved as `REC_YYYYMMDD_HHMMSS.wav`.
- Single recording is capped at 60 minutes.
- Low free space stops recording and announces the problem.
- Recording pauses when Xiaozhi is awakened.
- New recording appears in the music list.

## 11. Development Decision

The requirements are sufficiently clear for a staged implementation, except for true hardware battery percentage. Development can start with Phase 1 after explicit approval.

Before code changes, confirm:

- Whether to implement WAV playback before MP3 if MP3 decoder integration is not already available.
- Whether battery UI should show only the existing icon/default behavior until hardware battery sampling is available.

## 12. Implementation Task Cards

These cards are intended for phased AI implementation. Each card should be implemented, built, and verified before the next dependent card starts. Do not skip the validation notes, because several features share the same LCD, touch, SD, audio input, and audio output resources.

### Phase 0: Baseline and Document Cleanup

#### Card LC-AI-00: Clean PRD Encoding and Establish Build Baseline

Goal:

- Fix garbled Chinese text in this document before using it as implementation input.
- Confirm the current Lichuang Dev firmware builds before feature work starts.

Scope:

- Correct board name, voice command examples, city/weather examples, and Chinese labels to valid UTF-8.
- Keep product decisions unchanged.
- Select `CONFIG_BOARD_TYPE_LICHUANG_DEV_S3` and target `esp32s3`.

Acceptance:

- The document renders readable Chinese and English in a UTF-8 editor.
- A clean baseline build completes without feature changes.
- Any existing warnings or known failures are recorded before implementation.

Verification:

- Run `idf.py set-target esp32s3`.
- Select Lichuang Dev S3 in `idf.py menuconfig`, or apply the equivalent sdkconfig option.
- Run `idf.py build`.

Dependencies:

- None.

### Phase 1: Hardware Enablement and SD Card

#### Card LC-AI-01: Resolve GPIO48 Conflict

Goal:

- Reserve GPIO48 for SDMMC CMD and stop using it as the built-in LED on Lichuang Dev.

Scope:

- Update `main/boards/lichuang-dev/config.h`.
- Set `BUILTIN_LED_GPIO` to `GPIO_NUM_NC` or another confirmed free GPIO.
- Check all Lichuang Dev board code paths that call `GetLed()` still behave safely without a physical LED.

Acceptance:

- GPIO48 is no longer configured as LED.
- Boot, Wi-Fi config button, display, touch, camera, and audio initialization are not regressed.

Verification:

- Build firmware.
- Flash to board.
- Confirm boot log has no GPIO48 LED initialization conflict.
- Press BOOT button to confirm chat toggle or Wi-Fi config behavior still works.

Dependencies:

- LC-AI-00.

#### Card LC-AI-02: Add SDMMC 1-bit Mount

Goal:

- Mount the MicroSD card at `/sdcard` using SDMMC 1-bit mode.

Scope:

- Add Lichuang Dev SD init in `main/boards/lichuang-dev/lichuang_dev_board.cc`.
- Use CMD GPIO48, CLK GPIO47, DAT0 GPIO21.
- Add board-local config macros for mount point and SDMMC pins.
- Do not enable format-on-failure by default.

Acceptance:

- SD card mounts when present.
- Missing card or mount failure logs a warning and firmware continues booting.
- Existing display, touch, camera, and audio still initialize.

Verification:

- Build and flash.
- Boot with SD card inserted and confirm mount success log.
- Boot without SD card and confirm graceful failure.
- Verify `/sdcard` can be listed by a temporary diagnostic log or test hook.

Dependencies:

- LC-AI-01.

#### Card LC-AI-03: Add Storage Service and Directory Scanner

Goal:

- Provide a reusable storage layer for music and recording files.

Scope:

- Add `main/storage/sd_card_service.h`.
- Add `main/storage/sd_card_service.cc`.
- Ensure `/sdcard/music` and `/sdcard/recordings` exist after mount.
- Scan `.mp3`, `.MP3`, `.wav`, and `.WAV` files from both directories.
- Sort by newest modification time first.
- Expose mounted state, free space, and scan result.

Acceptance:

- Directory creation is idempotent.
- File scanner handles missing directories, empty directories, and unsupported files.
- Scan result includes full path, display name, extension/type, size, and modified time.
- Scan has a documented maximum item count to avoid memory blow-up.

Verification:

- Build.
- Test with empty SD card.
- Test with MP3/WAV files in both directories.
- Confirm newest-first ordering by log or temporary UI output.

Dependencies:

- LC-AI-02.

### Phase 2: Shared Runtime Foundations

#### Card LC-AI-04: Add Audio Focus and Interruption Rules

Goal:

- Prevent voice interaction, music playback, recorder, and prompt sounds from fighting over audio resources.

Scope:

- Define an audio focus model in or near `AudioService`.
- Supported owners: voice interaction, prompt sound, recorder, local music.
- Voice interaction has highest priority.
- Local music must pause on wake word/chat start.
- Recorder must pause on wake word/chat start.
- Pomodoro announcement may pause or duck music, then resume music only if safe.

Acceptance:

- There is a single code path for requesting/releasing local audio input/output focus.
- Wake word or chat start can notify local music and recorder services.
- State transitions are deterministic and logged.

Verification:

- Build.
- Add temporary logs and manually trigger wake while simulated local playback/recording states are active.
- Confirm no direct codec access bypasses the agreed focus path for new features.

Dependencies:

- LC-AI-00.

#### Card LC-AI-05: Add Recorder PCM Tap or Input Hook

Goal:

- Allow WAV recording without racing the existing wake word and voice input task.

Scope:

- Extend `AudioService` with a thread-safe PCM observer/tap for recorder use, or an equivalent single-owner input dispatch design.
- Recorder should not independently poll the codec while wake word processing owns the input path.
- Define sample rate, channel count, and sample format for recorder output.

Acceptance:

- Wake word detection remains active while recorder is idle.
- Recorder can receive PCM frames through the shared input path.
- Enabling/disabling recorder tap does not stop the input task unexpectedly.

Verification:

- Build.
- Confirm wake word still works after enabling and disabling the recorder tap.
- Log PCM frame counts during a short recording dry run.

Dependencies:

- LC-AI-04.

### Phase 3: UI Shell and Standby Foundation

#### Card LC-AI-06: Add Standby Home Screen

Goal:

- Show a real standby/home screen when the device is idle.

Scope:

- Extend LVGL display code with a standby view.
- Show time, date, weekday, weather placeholder, AQI placeholder, Wi-Fi icon, and battery/status area.
- Do not claim true battery percentage if hardware sampling is unavailable.
- Keep current chat/listening/speaking UI behavior when awakened.

Acceptance:

- Entering `kDeviceStateIdle` shows standby screen immediately.
- Leaving idle for listening/speaking restores the normal Xiaozhi interaction UI.
- Time updates at least once per minute after system time is valid.
- If time is not valid, UI shows a graceful placeholder.

Verification:

- Build and flash.
- Observe boot to idle.
- Trigger wake/listen/speak and return to idle.
- Confirm no text overlaps on 320x240 display.

Dependencies:

- LC-AI-00.

#### Card LC-AI-07: Add Swipe Navigation Shell

Goal:

- Add touch navigation between Home, Pomodoro, Music, and Recorder screens.

Scope:

- Register LVGL gesture handling for left/right swipes.
- Add screen containers or view controllers for the four views.
- Keep top status bar visible or consistently recreated across views.
- Add empty-state shells for Pomodoro, Music, and Recorder.

Acceptance:

- Swipe left/right cycles through screens.
- Home remains the default idle screen after boot.
- Touch gestures do not trigger chat controls accidentally.
- Layout stays stable on 320x240 display.

Verification:

- Build and flash.
- Swipe through all screens.
- Trigger wake from each screen and confirm return behavior is defined and stable.

Dependencies:

- LC-AI-06.

### Phase 4: Pomodoro

#### Card LC-AI-08: Implement Pomodoro Timer Service

Goal:

- Provide a feature service for Pomodoro state and timing.

Scope:

- Add `main/features/pomodoro_timer.h`.
- Add `main/features/pomodoro_timer.cc`.
- Support start, pause, resume, cancel, remaining-time query, and finish callback.
- Enforce custom duration range: 1 to 180 minutes.
- Keep Pomodoro running during voice interaction unless explicitly paused/canceled.

Acceptance:

- 15/30/45 minute presets start correctly.
- Out-of-range duration is rejected with a structured error.
- Pause/resume/cancel state transitions are idempotent and logged.
- Completion callback fires once.

Verification:

- Build.
- Use shortened test durations behind a test/debug setting or temporary internal test.
- Verify remaining time and completion behavior.

Dependencies:

- LC-AI-04.

#### Card LC-AI-09: Add Pomodoro UI and Voice Tools

Goal:

- Make Pomodoro controllable by touch and voice tool calls.

Scope:

- Add UI buttons for 15/30/45, pause/resume, cancel, and status.
- Register MCP/self tools:
  - `self.pomodoro.start`
  - `self.pomodoro.pause`
  - `self.pomodoro.resume`
  - `self.pomodoro.cancel`
  - `self.pomodoro.get_status`
- Play or request a voice announcement on completion.

Acceptance:

- Touch and tool calls share the same service state.
- Completion announcement follows audio focus rules.
- Invalid duration returns a clear voice/tool result.

Verification:

- Build and flash.
- Test UI controls.
- Test tool calls from voice interaction or MCP test client.
- Confirm Pomodoro completion does not break music or recorder state.

Dependencies:

- LC-AI-07.
- LC-AI-08.

### Phase 5: Environment Data and Standby Content

#### Card LC-AI-10: Add Environment Configuration

Goal:

- Store weather and AQI configuration without hardcoding secrets.

Scope:

- Add Kconfig and/or NVS-backed config for:
  - AMap API key.
  - AQI aggregate endpoint.
  - Refresh interval.
  - Optional fallback adcode/city.
- Never commit a real API key.

Acceptance:

- Firmware builds with empty/default config.
- Missing key disables live weather fetch gracefully.
- Config values can be read by the environment service.

Verification:

- Build.
- Boot with no API key and confirm no crash or repeated error spam.
- Configure test values and confirm they are loaded.

Dependencies:

- LC-AI-06.

#### Card LC-AI-11: Implement Environment Service

Goal:

- Fetch and cache city, weather, and AQI data for standby UI.

Scope:

- Add `main/services/environment_service.h`.
- Add `main/services/environment_service.cc`.
- Use IP location to obtain city/adcode.
- Query AMap weather by adcode.
- Query third-party or self-hosted AQI endpoint.
- Cache last successful result in NVS or a small local cache.
- Expose stale/unavailable state to UI.

Acceptance:

- Network failures do not block boot or UI.
- Last cached city/weather is used when live query fails.
- Weather can display without AQI if AQI query fails.
- Refresh interval is respected.

Verification:

- Build and flash.
- Test with valid network and API config.
- Test with Wi-Fi disconnected.
- Test with invalid API key or endpoint.

Dependencies:

- LC-AI-10.

#### Card LC-AI-12: Bind Environment Data to Standby UI

Goal:

- Replace placeholders on the standby screen with real cached data.

Scope:

- Update home screen labels from environment service.
- Show unavailable/stale states clearly but compactly.
- Avoid blocking LVGL update thread on HTTP requests.

Acceptance:

- Weather, temperature, city, and AQI/category render when available.
- Unavailable values render as placeholders.
- UI updates when new data arrives.

Verification:

- Build and flash.
- Confirm first boot placeholder, then data update.
- Confirm cached data appears after reboot without immediate network success.

Dependencies:

- LC-AI-11.

### Phase 6: Local WAV Music Playback

#### Card LC-AI-13: Implement WAV Playlist and Player Service

Goal:

- Play local WAV files from SD card through the shared audio output path.

Scope:

- Add `main/features/music_player.h`.
- Add `main/features/music_player.cc`.
- Use storage service playlist data.
- Support play, pause, resume, previous, next, stop, and current-track status.
- Implement WAV parser for PCM WAV first.
- Reject unsupported WAV formats with a clear error.

Acceptance:

- PCM WAV files under `/sdcard/music` and `/sdcard/recordings` play.
- Music pauses on wake/chat start.
- Previous/next obey newest-first playlist order.
- Playback does not bypass audio focus rules.

Verification:

- Build and flash.
- Test 16-bit mono and stereo WAV files at expected sample rates.
- Test invalid or unsupported WAV file.
- Wake the device during playback and confirm auto-pause.

Dependencies:

- LC-AI-03.
- LC-AI-04.

#### Card LC-AI-14: Add Music UI and Voice Tools

Goal:

- Make local music controllable by touch and voice tool calls.

Scope:

- Add Music screen controls: previous, play/pause, next, list.
- Add track title/status labels.
- Register MCP/self tools:
  - `self.music.play`
  - `self.music.pause`
  - `self.music.next`
  - `self.music.previous`
  - `self.music.list`

Acceptance:

- UI and tools share the same playback state.
- Empty playlist is handled gracefully.
- Track list updates after a new recording is saved.

Verification:

- Build and flash.
- Test UI controls.
- Test tool calls.
- Test no SD card, empty SD card, and populated SD card.

Dependencies:

- LC-AI-07.
- LC-AI-13.

### Phase 7: Recording

#### Card LC-AI-15: Implement WAV Recorder Service

Goal:

- Record microphone audio to WAV files on SD card.

Scope:

- Add `main/features/audio_recorder.h`.
- Add `main/features/audio_recorder.cc`.
- Write WAV header and finalize file size on stop.
- File naming format: `REC_YYYYMMDD_HHMMSS.wav`.
- Save to `/sdcard/recordings`.
- Enforce default max duration: 60 minutes.
- Check free space before and during recording.

Acceptance:

- Start, pause, resume, and stop work.
- Saved file is playable as WAV.
- Low free space stops recording and reports the reason.
- Recording pauses on wake/chat start.

Verification:

- Build and flash.
- Record a short clip and play it back from SD.
- Inspect WAV header with a desktop tool.
- Simulate or test low free space.
- Wake during recording and confirm auto-pause.

Dependencies:

- LC-AI-03.
- LC-AI-05.

#### Card LC-AI-16: Add Recorder UI and Voice Tools

Goal:

- Make recording controllable by touch and voice tool calls.

Scope:

- Add Recorder screen controls: record, pause/resume, stop.
- Show elapsed time, file name, and free-space warning.
- Register MCP/self tools:
  - `self.recorder.start`
  - `self.recorder.pause`
  - `self.recorder.resume`
  - `self.recorder.stop`
  - `self.recorder.get_status`
- Refresh music playlist when a recording is saved.

Acceptance:

- UI and tools share recorder state.
- New recordings appear in music list newest-first.
- Errors are visible in UI and returned through tools.

Verification:

- Build and flash.
- Test UI controls.
- Test voice/tool controls.
- Record, stop, switch to Music screen, and play the new recording.

Dependencies:

- LC-AI-14.
- LC-AI-15.

### Phase 8: MP3 Validation and Optional Enablement

#### Card LC-AI-17: Evaluate MP3 Decoder Feasibility

Goal:

- Decide whether MP3 playback can be enabled safely on ESP32-S3-N16R8 with the current firmware load.

Scope:

- Select a decoder component or ESP-IDF supported path.
- Measure CPU load, heap, PSRAM use, stack use, and playback stability.
- Test interaction with LVGL, camera initialization, wake word, Opus, and SD card IO.

Acceptance:

- Decoder dependency and license are documented.
- Heap watermark and stack watermark are recorded.
- 30-minute MP3 playback test passes without crash, underrun, or wake-word regression.
- If results are poor, MP3 remains disabled and WAV-only behavior is documented.

Verification:

- Build with decoder enabled in an experiment branch.
- Play several MP3 bitrates.
- Wake during MP3 playback.
- Record memory and CPU observations in this document or a linked test log.

Dependencies:

- LC-AI-13.

#### Card LC-AI-18: Enable MP3 Playback If Feasible

Goal:

- Add MP3 playback to the music player only if LC-AI-17 passes.

Scope:

- Integrate decoder into music player.
- Keep WAV path unchanged.
- Reject unsupported MP3 files gracefully.

Acceptance:

- MP3 and WAV files share playlist and controls.
- Wake/chat interruption still pauses playback.
- No regression in WAV playback or recorder playback.

Verification:

- Build and flash.
- Test MP3 and WAV mixed playlist.
- Repeat wake interruption and long playback tests.

Dependencies:

- LC-AI-17.

### Phase 9: Full-System Verification and Release Readiness

#### Card LC-AI-19: Integration Regression Test

Goal:

- Verify all new features together on real hardware.

Scope:

- Test standby, weather/AQI, Pomodoro, WAV music, recording, SD mount, wake interruption, and status bar.
- Test with SD card present, absent, full, and containing unsupported files.
- Test with Wi-Fi connected, disconnected, and API failure.

Acceptance:

- All acceptance criteria in Section 10 pass.
- No boot regression.
- No audio deadlock or stuck mute/output state.
- No LVGL crash or visible overlap on 320x240 display.

Verification:

- `idf.py build`.
- `idf.py flash monitor`.
- Manual hardware test checklist completed.
- Capture logs for at least one full pass.

Dependencies:

- LC-AI-09.
- LC-AI-12.
- LC-AI-16.
- LC-AI-18 if MP3 is enabled.

#### Card LC-AI-20: Documentation and Handoff

Goal:

- Leave a clear handoff for users and future AI agents.

Scope:

- Update this document with final implementation decisions.
- Document menuconfig/NVS keys.
- Document supported audio formats and limitations.
- Document known hardware limitation for battery percentage.
- Add troubleshooting notes for SD card, API key, weather unavailable, and unsupported audio files.

Acceptance:

- A new developer can build, configure, flash, and test the feature set from documentation.
- Limitations are explicit and not hidden in code comments.

Verification:

- Follow the documentation on a clean checkout or clean build directory.
- Confirm no real API keys or private endpoints are committed.

Dependencies:

- LC-AI-19.
