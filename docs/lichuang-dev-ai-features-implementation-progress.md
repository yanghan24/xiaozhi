# Lichuang Dev AI Features Implementation Progress

Date: 2026-07-06

This document records the development work completed after Phase 0 for the
Lichuang Dev ESP32-S3 board AI feature plan.

## Current Status

Phase 0 was already completed before this implementation pass.

The current implementation has completed the firmware-side work for:

- Phase 1: Hardware enablement and SD card foundation
- Phase 2: Shared runtime foundations for audio focus and recorder PCM input
- Phase 3: UI shell and standby foundation
- Phase 4: Pomodoro timer service, UI controls, and voice tools

ESP-IDF build, flash, and hardware verification are intentionally left for the
developer to run locally.

## Completed Work

### Phase 1: Hardware Enablement and SD Card

#### GPIO48 Conflict Resolution

Status: completed.

The Lichuang Dev board now reserves GPIO48 for SDMMC CMD and does not use it as
a built-in LED.

Relevant file:

- `main/boards/lichuang-dev/config.h`

Current board configuration:

- `BUILTIN_LED_GPIO` is `GPIO_NUM_NC`
- `SDCARD_SDMMC_CMD_PIN` is `GPIO_NUM_48`
- `SDCARD_SDMMC_CLK_PIN` is `GPIO_NUM_47`
- `SDCARD_SDMMC_D0_PIN` is `GPIO_NUM_21`
- `SDCARD_SDMMC_BUS_WIDTH` is `1`

Static verification performed:

- Confirmed GPIO48 is only used as `SDCARD_SDMMC_CMD_PIN` under
  `main/boards/lichuang-dev`.
- Confirmed the Lichuang Dev board does not override `GetLed()`, so it falls
  back to the common `NoLed` implementation.

#### SDMMC 1-bit Mount

Status: completed.

The Lichuang Dev board initializes and mounts the MicroSD card through SDMMC
1-bit mode at `/sdcard`.

Relevant file:

- `main/boards/lichuang-dev/lichuang_dev_board.cc`

Implemented behavior:

- Uses `SDMMC_HOST_DEFAULT()`.
- Uses `SDMMC_SLOT_CONFIG_DEFAULT()`.
- Assigns custom SDMMC pins from board config.
- Sets bus width to 1-bit mode.
- Uses `esp_vfs_fat_sdmmc_mount()`.
- Does not format the card on mount failure.
- Logs a warning and continues boot when the card is missing or mount fails.
- Calls `SdCardService::EnsureMediaDirectories()` after successful mount.

#### SD Card Service

Status: completed.

A reusable SD card storage service is available.

Relevant files:

- `main/storage/sd_card_service.h`
- `main/storage/sd_card_service.cc`
- `main/CMakeLists.txt`

Implemented behavior:

- Detects whether `/sdcard` is mounted.
- Ensures `/sdcard/music` exists.
- Ensures `/sdcard/recordings` exists.
- Scans playable media files from both directories.
- Supports `.mp3`, `.MP3`, `.wav`, and `.WAV` through case-insensitive extension matching.
- Returns full path, display name, extension, media type, file size, and modified time.
- Sorts results by newest modified time first.
- Limits scan results with `kDefaultMaxMediaFiles = 200`.
- Exposes free-space query through `GetFreeBytes()`.

Important fix:

- Replaced unsupported POSIX `sys/statvfs.h` / `statvfs()` usage with ESP-IDF
  FatFS API `esp_vfs_fat_info()`.

Build dependency updates:

- Added `esp_driver_sdmmc`.
- Added `sdmmc`.
- Existing `fatfs` dependency is retained.

### Phase 2: Shared Runtime Foundations

#### Audio Focus Model

Status: completed.

An audio focus model has been added to `AudioService` so future local music,
recording, prompt sound, and Xiaozhi voice interaction features do not compete
for codec input/output resources without coordination.

Relevant files:

- `main/audio/audio_service.h`
- `main/audio/audio_service.cc`
- `main/application.cc`

Supported focus owners:

- `kAudioFocusOwnerLocalMusic`
- `kAudioFocusOwnerRecorder`
- `kAudioFocusOwnerPromptSound`
- `kAudioFocusOwnerVoiceInteraction`

Supported focus resources:

- Input
- Output
- Input and output

Priority order implemented:

1. Voice interaction
2. Prompt sound
3. Recorder
4. Local music

Public APIs added:

- `RequestAudioFocus()`
- `ReleaseAudioFocus()`
- `HasAudioFocus()`
- `NotifyVoiceInteractionStarted()`
- `NotifyVoiceInteractionStopped()`

Behavior:

- Higher-priority owners can preempt lower-priority owners.
- Lower-priority owners can register a focus-lost callback.
- State transitions are logged.

Application state integration:

- Voice interaction focus starts when entering:
  - `kDeviceStateConnecting`
  - `kDeviceStateListening`
  - `kDeviceStateSpeaking`
- Voice interaction focus is released when entering:
  - `kDeviceStateIdle`
  - `kDeviceStateWifiConfiguring`
  - `kDeviceStateAudioTesting`

This prepares the later music and recorder services to auto-pause when Xiaozhi
wakes or starts a chat session.

#### Recorder PCM Input Hook

Status: completed.

A shared PCM observer has been added to `AudioService`.

Relevant files:

- `main/audio/audio_service.h`
- `main/audio/audio_service.cc`

Public APIs added:

- `SetInputPcmObserver()`
- `ClearInputPcmObserver()`

Recorder PCM format defined:

- Sample rate: 16000 Hz
- Channels: mono
- Sample format: signed 16-bit PCM

Behavior:

- PCM frames are delivered from the existing `ReadAudioData()` path.
- Recorder code will not need to poll the codec directly.
- Observer callbacks are only invoked when the observer owner currently holds
  input audio focus.
- Multi-channel input is converted to mono by taking the first channel.

This preserves wake word and voice input ownership of the microphone path and
prepares Phase 7 WAV recording.

### Phase 3: UI Shell and Standby Foundation

#### Standby Home Screen

Status: completed for first hardware-visible shell.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- Adds a Lichuang Dev S3 specific standby feature shell guarded by
  `CONFIG_BOARD_TYPE_LICHUANG_DEV_S3`.
- Shows the shell automatically when the device enters `kDeviceStateIdle`.
- Hides the shell automatically when the device leaves idle for connecting,
  listening, speaking, Wi-Fi configuration, audio testing, or other active
  states.
- Keeps the existing top status bar visible for Wi-Fi, mute, battery/default
  power icon, notifications, and status text.
- Home page shows:
  - Time
  - Date
  - Weekday
  - Weather unavailable placeholder
  - AQI unavailable placeholder
- Time/date/weekday refresh through the existing one-second application clock
  tick and only show real values after system time is valid.
- The existing chat and emotion UI is preserved for non-idle Xiaozhi
  interaction states.

Current placeholder decision:

- Weather and AQI remain placeholders until Phase 5 environment service is
  implemented.
- Battery percentage is not claimed, matching the hardware limitation already
  documented in the PRD.

#### Swipe Navigation Shell

Status: completed for first hardware-visible shell.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- Registers an LVGL `LV_EVENT_GESTURE` callback on the standby feature shell.
- Swipe left cycles forward through:
  - Home
  - Pomodoro
  - Music
  - Recorder
- Swipe right cycles backward through the same views.
- Logs the active feature page name on each page switch.
- Adds a compact page indicator such as `Home 1/4`.
- Adds visible shell pages for:
  - Home
  - Pomodoro
  - Music
  - Recorder

Pomodoro shell content:

- Initially added the Pomodoro page shell.
- The page is now upgraded by Phase 4 with a real timer service, countdown
  labels, 15m/30m/45m quick-start buttons, pause/resume, and cancel.

Music shell content:

- Shows previous/play/next placeholder controls.
- Reuses `SdCardService` to display SD card mount status.
- When the SD card is mounted, displays free space and scanned MP3/WAV file
  count.
- Shows the newest scanned media filename when available.

Recorder shell content:

- Shows record/pause/stop placeholder controls.
- Shows the planned output directory `/sdcard/recordings`.

Hardware-visible checkpoint:

- After build and flash, the board should show a standby module UI when idle.
- Swiping left/right on the touch screen should visibly switch between the four
  module pages.
- With an SD card inserted, the Music page should show mounted/free-space/media
  information.
- Without an SD card, the Music page should show `SD: not mounted`.

### Phase 4: Pomodoro

#### Pomodoro Timer Service

Status: completed for firmware-side service.

Relevant files:

- `main/features/pomodoro_timer.h`
- `main/features/pomodoro_timer.cc`
- `main/CMakeLists.txt`

Implemented behavior:

- Adds `PomodoroTimer` singleton service.
- Supports start, pause, resume, cancel, status query, and finish callback.
- Enforces duration range of 1 to 180 minutes.
- Uses an ESP periodic timer to check completion once per second.
- Keeps timer state independent from Xiaozhi voice interaction state, so it
  continues running when the device wakes/listens/speaks.
- Completion callback fires once by moving the service to `kFinished`.
- Logs start, pause, resume, cancel, and finish transitions.

Status values:

- Idle
- Running
- Paused
- Finished

#### Pomodoro UI Controls

Status: completed for hardware-visible control.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- The Pomodoro page now displays:
  - Remaining time as `MM:SS`
  - Current state text
  - 15m, 30m, and 45m quick-start buttons
  - Pause/resume button
  - Cancel button
- Touch controls call the same `PomodoroTimer` service used by voice tools.
- Remaining time refreshes through the existing display status-bar tick while
  the Pomodoro page is visible.
- Starting a preset updates the display immediately.
- Pause/resume/cancel update the display immediately.
- When the timer completes, the page shows `Finished` the next time it refreshes.

Hardware-visible checkpoint:

- Swipe to the Pomodoro page.
- Tap `15m`, `30m`, or `45m`; the countdown should start immediately.
- Tap `Pause`; the countdown should stop.
- Tap `Pause` again while paused; the countdown should resume.
- Tap `Cancel`; the display should return to `00:00` and `Idle`.

#### Pomodoro Voice/MCP Tools

Status: completed.

Relevant file:

- `main/boards/lichuang-dev/lichuang_dev_board.cc`

Registered tools:

- `self.pomodoro.start`
- `self.pomodoro.pause`
- `self.pomodoro.resume`
- `self.pomodoro.cancel`
- `self.pomodoro.get_status`

Tool behavior:

- `self.pomodoro.start` accepts `duration_minutes`.
- Invalid durations return `Duration must be between 1 and 180 minutes.`
- Pause/resume/cancel are idempotent.
- Status returns a short text summary suitable for voice response.

Completion behavior:

- On finish, the board schedules a main-thread notification:
  `Pomodoro finished`.
- The board plays the existing success prompt sound.
- A true custom TTS sentence is still dependent on the higher-level voice
  interaction path; current firmware-side behavior is visible and audible.

## Verification Completed by Codex

The following non-ESP-IDF validations were performed:

- `git diff --check`
- Static search for Lichuang Dev GPIO48 usage.
- Static search for Lichuang Dev LED usage.
- Static search confirming SD card service is included in `main/CMakeLists.txt`.
- Static search confirming voice interaction focus state hooks are wired.
- Static search confirming PCM observer is called from `ReadAudioData()`.
- Static inspection of the Lichuang Dev LVGL standby shell wiring.
- Context7 LVGL documentation check for `LV_EVENT_GESTURE`,
  `lv_indev_get_gesture_dir()`, and hidden object flags.
- Static inspection confirming Pomodoro source is included in `main/CMakeLists.txt`.
- Static inspection confirming Pomodoro UI controls and MCP tools call the same
  service.

Codex did not run:

- `idf.py build`
- `idf.py flash`
- monitor or hardware tests

## Required Developer Verification

Run these locally:

```bash
idf.py build
```

Recommended hardware checks:

- Boot with SD card inserted and confirm SD mount success log.
- Boot without SD card and confirm boot continues with a warning.
- Confirm `/sdcard/music` and `/sdcard/recordings` are created.
- Put MP3/WAV files in both directories and confirm scanner ordering later when UI or logs are added.
- Confirm display, touch, audio, camera, and BOOT button behavior are not regressed.
- Trigger wake word or chat and confirm no audio deadlock or crash appears in logs.
- Confirm idle screen shows the Home standby page.
- Swipe left/right and confirm pages cycle through Home, Pomodoro, Music, and
  Recorder.
- On the Music page, confirm SD mounted/free-space/media-count display with a
  card inserted.
- On the Music page, confirm graceful `SD: not mounted` display without a card.
- Trigger wake/listen/speak from each feature page and confirm the Xiaozhi
  interaction UI appears, then returns to the standby shell when idle again.
- Test Pomodoro UI quick starts: 15m, 30m, and 45m.
- Test Pomodoro pause, resume, and cancel from the touch screen.
- Test voice/tool calls for start, pause, resume, cancel, and status.
- Test an invalid voice/tool duration such as 181 minutes and confirm the tool
  returns the range error.
- For hardware completion testing, temporarily use the 1-minute lower bound or
  a short debug edit, then confirm the finish notification and prompt sound.

## Current Modified Files

- `main/CMakeLists.txt`
- `main/application.cc`
- `main/audio/audio_service.h`
- `main/audio/audio_service.cc`
- `main/boards/lichuang-dev/lichuang_dev_board.cc`
- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`
- `main/features/pomodoro_timer.h`
- `main/features/pomodoro_timer.cc`
- `main/storage/sd_card_service.h`
- `main/storage/sd_card_service.cc`

## Known Limitations

- No ESP-IDF build result has been recorded yet.
- No SD card hardware mount result has been recorded yet.
- No hardware touch-swipe verification result has been recorded yet.
- No local music playback service has been added yet.
- No recorder service has been added yet.
- The PCM observer is infrastructure only; it does not write WAV files yet.
- MP3 playback remains unvalidated.
- Battery percentage remains hardware-dependent and is not implemented.
- Music and Recorder controls are visible UI placeholders only; their services
  and voice tools are not implemented yet.
- Weather and AQI are placeholders until the environment service phase.
- Pomodoro completion currently uses screen notification plus success prompt
  sound, not a custom generated TTS sentence.

## Next Recommended Phase

After `idf.py build` passes and the Phase 3/4 shell is confirmed on hardware,
continue with:

- Phase 5: Environment data and standby content

Planned next tasks:

- Add environment configuration for AMap key, AQI endpoint, refresh interval,
  and fallback city/adcode.
- Implement environment service with IP location, weather query, AQI query, and
  cache.
- Bind cached environment data to the Home standby page.
