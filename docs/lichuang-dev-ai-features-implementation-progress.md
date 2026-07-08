# Lichuang Dev AI Features Implementation Progress

Date: 2026-07-08

This document records the development work completed after Phase 0 for the
Lichuang Dev ESP32-S3 board AI feature plan.

## Current Status

Phase 0 was already completed before this implementation pass.

The current implementation has completed the firmware-side work for:

- Phase 1: Hardware enablement and SD card foundation
- Phase 2: Shared runtime foundations for audio focus and recorder PCM input
- Phase 3: UI shell and standby foundation
- Phase 4: Pomodoro timer service, UI controls, and voice tools
- Phase 5: Environment configuration, weather/AQI service, and Home binding
- Phase 6: Local WAV music playback service, Music UI controls, and voice tools
- Phase 7: WAV recorder service, Recorder UI controls, and voice tools
- Phase 8: MP3 decoder integration and optional MP3 playback enablement
- Phase 9: Release-readiness diagnostics, self-check, and handoff foundation

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
- Enables internal pull-up on SDMMC CMD and D0.
- Uses the default 20 MHz SDMMC frequency for a more conservative first
  hardware path.
- Uses the Lichuang example's internal pull-up slot flag and 16 KB FAT
  allocation unit size.
- Uses `esp_vfs_fat_sdmmc_mount()`.
- Does not format the card on mount failure.
- Logs a warning and continues boot when the card is missing or mount fails.
- Calls `SdCardService::EnsureMediaDirectories()` after successful mount.
- Exposes board-level `EnsureStorageReady()` so feature modules can retry SD
  mount after boot.

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

#### Touch Navigation Shell

Status: completed and refreshed after hardware UI feedback.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- Adds an explicit bottom navigation row with four touch buttons:
  - Home
  - Timer
  - Music
  - Record
- The selected navigation button is highlighted so the active feature page is
  visible without relying on the old page indicator.
- Registers an LVGL `LV_EVENT_GESTURE` callback on the standby feature shell.
- Swipe left remains available as a fallback and cycles forward through:
  - Home
  - Pomodoro
  - Music
  - Recorder
- Swipe right remains available as a fallback and cycles backward through the
  same views.
- Logs the active feature page name on each page switch.
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

- Shows previous/play/next controls.
- Reuses `SdCardService` to display SD card mount status.
- When the SD card is mounted, displays free space and scanned WAV file count.
- Shows the newest scanned media filename when available.
- The controls are now bound to the Phase 6 WAV player service.

Recorder shell content:

- Shows record/pause/stop controls.
- Shows recorder state, elapsed seconds, free-space hint, and current/last file.
- The controls are now bound to the Phase 7 WAV recorder service.

Hardware-visible checkpoint:

- After build and flash, the board should show a standby module UI when idle.
- Tapping the bottom `Home`, `Timer`, `Music`, and `Record` buttons should
  visibly switch between the four module pages.
- Swiping left/right on the touch screen should still switch between pages as a
  fallback gesture.
- With an SD card inserted, the Music page should show mounted/free-space/media
  information.
- Without an SD card, the Music page should show `SD: not mounted`.

#### Pomodoro, Music, and Recorder UI Refresh

Status: completed for firmware-side LVGL UI.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- The standby feature UI now uses a denser sci-fi HUD/dashboard layout with a
  dark navy background, cyan panel borders, dark module buttons, bright selected
  navigation state, and neon state colors.
- Timer and recorder warning/running states use neon red/amber, while healthy
  ready/completed states use cyan/green.
- Pomodoro page now has a countdown progress bar, elapsed/total status text,
  and dynamic `Pause`/`Resume` button text.
- When a Pomodoro countdown is running or paused, the page switches into a
  dedicated timer view by hiding quick presets and showing `FOCUS TIMER` or
  `PAUSED`, the remaining `MM:SS` value, progress, and active controls.
- Starting Pomodoro through Xiaozhi AI voice now requests the display to show
  the Timer countdown page immediately. A short feature-shell override prevents
  the voice dialogue/subtitle UI from covering the countdown while the voice
  response finishes.
- Music page now has a playback progress bar, dynamic `Play`/`Pause` button
  text, active track state, elapsed/duration, sample rate, channel count, and a
  next-track or track-position hint.
- Music page resets detail/progress/list labels cleanly when the SD card is not
  mounted.
- Music page reads active playback status without periodically rebuilding the
  SD playlist during playback.
- Recorder page now has a recording progress bar, dynamic `Pause`/`Resume`
  button text, elapsed time, written KB, remaining free-space hint, and
  current/last file display.
- Recorder page now exposes explicit `Start`, `Pause`/`Resume`, and `Stop`
  buttons. `Start` is emphasized while idle, and `Stop` is emphasized while
  recording or paused.

Hardware-visible checkpoint:

- Tap `Timer`, start a preset, and confirm the countdown screen shows
  `FOCUS TIMER`, remaining time, progress, and elapsed/total text updating once
  per second.
- Start Pomodoro through Xiaozhi AI voice and confirm the display jumps to the
  Timer countdown page instead of staying on the voice dialogue page.
- Confirm Home, Timer, Music, and Record pages share the same dark HUD
  technology visual style.
- Pause and resume Pomodoro and confirm the button text changes between
  `Pause` and `Resume`.
- Tap `Music`, play a WAV/MP3 file, and confirm the progress bar,
  elapsed/duration, sample-rate/channel text, and `Play`/`Pause` label update.
- Tap `Record`, tap `Start`, and confirm the recording bar, elapsed/written
  detail, free-space hint, and pause/resume label update.
- While recording, tap `Stop` and confirm recording ends and the file label is
  retained for the last recording.

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

- Tap `Timer` in the bottom navigation, or swipe to the Pomodoro page.
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
- On successful start, `self.pomodoro.start` requests the display to jump to
  the Timer countdown page.
- Invalid durations return `Duration must be between 1 and 180 minutes.`
- Pause/resume/cancel are idempotent.
- Status returns a short text summary suitable for voice response.

Completion behavior:

- On finish, the board schedules a main-thread notification:
  `Pomodoro finished`.
- The board plays the existing success prompt sound.
- A true custom TTS sentence is still dependent on the higher-level voice
  interaction path; current firmware-side behavior is visible and audible.

### Phase 5: Environment Data and Standby Content

#### Environment Configuration

Status: completed.

Relevant files:

- `main/services/environment_service.h`
- `main/services/environment_service.cc`
- `main/boards/lichuang-dev/lichuang_dev_board.cc`
- `main/CMakeLists.txt`

Implemented behavior:

- Adds `EnvironmentService` singleton.
- Stores configuration in NVS namespace `env`.
- Does not hardcode or commit any real API key.
- Uses short NVS keys compatible with ESP-IDF NVS key length limits:
  - `amap_key`
  - `aqi_url`
  - `refresh_min`
  - `fb_adcode`
  - `fb_city`
  - `cache`
- Adds user-only MCP tool `self.environment.set_config`.
- Adds runtime tools:
  - `self.environment.refresh`
  - `self.environment.get_status`

Configuration fields:

- AMap API key
- Optional AQI aggregate endpoint
- Refresh interval, clamped to 5 to 1440 minutes
- Optional fallback adcode
- Optional fallback city

#### Environment Service

Status: completed for first live-data path.

Relevant files:

- `main/services/environment_service.h`
- `main/services/environment_service.cc`
- `main/application.cc`

Implemented behavior:

- Starts a non-blocking background refresh when the network connects.
- Home UI can also request a refresh without blocking LVGL.
- If AMap key is missing, refresh is skipped gracefully.
- Uses AMap IP location API to obtain city/adcode.
- Uses AMap weather API to obtain live weather by adcode.
- Uses optional aggregate endpoint by appending `adcode=<value>` for AQI data.
- Caches last successful environment snapshot in NVS.
- Loads cached data on boot and marks it stale until refreshed.
- Network/API failures do not block boot and are exposed as status/error text.

AQI aggregate response support:

- Supports the planned shape:

```json
{
  "air": {
    "aqi": 42,
    "category": "Good",
    "pm25": 18,
    "pm10": 35
  }
}
```

- Also accepts top-level `aqi`, `category`, `pm25`, and `pm10` fields.

#### Home Standby Binding

Status: completed.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- Home page no longer shows only fixed weather/AQI placeholders.
- If no AMap key is configured, Home shows `Weather: configure AMap key`.
- If refresh is in progress, Home shows updating states.
- If weather is available, Home shows compact city, weather, temperature, and
  humidity.
- If AQI is available, Home shows AQI/category/PM2.5.
- If AQI endpoint is not configured or fails, weather can still display
  normally while AQI remains unavailable.
- Environment labels refresh through the existing display tick without blocking
  the LVGL thread.

Hardware-visible checkpoint:

- Boot without configuring an AMap key; Home should show a clear configuration
  hint rather than crashing or spamming errors.
- Configure values through `self.environment.set_config`.
- Trigger `self.environment.refresh` or reconnect Wi-Fi.
- Home should transition through updating/unavailable states and then show city
  and weather when the API call succeeds.
- If an AQI endpoint is configured and returns valid data, Home should show AQI
  and PM2.5.
- Reboot after one successful refresh; cached weather should be available as a
  stale snapshot before the next live refresh completes.

### Phase 6: Local WAV Music Playback

#### Audio Playback Queue Entry

Status: completed.

Relevant files:

- `main/audio/audio_service.h`
- `main/audio/audio_service.cc`

Implemented behavior:

- Adds `AudioService::PushPcmToPlaybackQueue()`.
- Allows a focus owner to push decoded PCM frames into the existing speaker
  playback queue.
- Requires the caller to hold output audio focus.
- Reuses the existing audio output task, speaker enable path, and audio power
  timer instead of writing directly to the codec from the music player.

#### WAV Playlist and Player Service

Status: completed for first hardware-audible WAV playback path.

Relevant files:

- `main/features/music_player.h`
- `main/features/music_player.cc`
- `main/CMakeLists.txt`

Implemented behavior:

- Adds `MusicPlayer` singleton service.
- Initially built a newest-first WAV-only local music list from `/sdcard/music`
  and `/sdcard/recordings`; Phase 8 extends the same list to MP3 files.
- Supports play, pause, resume, stop, previous, next, status, and playlist
  query.
- Parses RIFF/WAVE files and locates `fmt ` and `data` chunks.
- Supports 16-bit PCM mono and stereo WAV files.
- Downmixes stereo WAV input to mono when the board output path is mono.
- Rejects unsupported WAV files with clear error text.
- The initial Phase 6 path required WAV sample rate to match the board audio
  output rate; Phase 8 adds output resampling for local music.
- Uses `kAudioFocusOwnerLocalMusic`.
- Automatically pauses when voice interaction preempts output focus.

Unsupported in this phase:

- Compressed WAV formats.
- Non-16-bit WAV formats.

#### Music UI Controls

Status: completed for touch control.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- Music page `Prev`, `Play`, and `Next` buttons now call `MusicPlayer`.
- Music page shows:
  - SD mount/free-space status.
  - Local music playlist count and newest track while idle.
  - Current play/pause state, track name, elapsed seconds, and total seconds
    during playback.
  - Error text for unsupported WAV files or missing playlist.
- Display updates through the existing one-second status-bar tick.
- No LVGL updates are performed from the playback task.

Hardware-visible/audible checkpoint:

- Put a 24000 Hz, 16-bit PCM WAV file under `/sdcard/music` or
  `/sdcard/recordings`.
- Tap `Music` in the bottom navigation, or swipe to the Music page.
- Tap `Play`; the board should play the WAV through the speaker.
- Tap `Prev`/`Next`; playback should switch through the newest-first local
  music playlist.
- Wake Xiaozhi or start chat during playback; local music should pause.
- Return to idle and tap `Play` again to resume from the paused track position.

#### Music Voice/MCP Tools

Status: completed.

Relevant file:

- `main/boards/lichuang-dev/lichuang_dev_board.cc`

Registered tools:

- `self.music.play`
- `self.music.pause`
- `self.music.next`
- `self.music.previous`
- `self.music.list`
- `self.music.get_status`

Tool behavior:

- `self.music.play` accepts optional `index`; omit it or pass `-1` to play the
  current/first track.
- `self.music.list` returns the local music playlist with index, name, path,
  type, and size.
- `self.music.get_status` returns state, current index, playlist size, current
  track, sample rate, channel count, elapsed time, duration, and last error.

### Phase 7: WAV Recording

#### WAV Recorder Service

Status: completed for first hardware-recordable WAV path.

Relevant files:

- `main/features/audio_recorder.h`
- `main/features/audio_recorder.cc`
- `main/CMakeLists.txt`

Implemented behavior:

- Adds `AudioRecorder` singleton service.
- Calls `Board::EnsureStorageReady()` before reporting SD not mounted, so a
  recording start can retry SD mounting if boot-time mount was too early.
- Records microphone PCM through the Phase 2 `AudioService` PCM observer.
- Saves files under `/sdcard/recordings`.
- Uses file naming format `REC_YYYYMMDD_HHMMSS.wav`.
- Writes a placeholder WAV header on start.
- Finalizes RIFF/data sizes when recording stops or errors out.
- Records 16000 Hz mono signed 16-bit PCM WAV, matching
  `kRecorderPcmSampleRate`, `kRecorderPcmChannels`, and
  `kRecorderPcmBitsPerSample`.
- Supports start, pause, resume, stop, status, and short text status.
- Enforces default max duration of 60 minutes.
- Checks SD free space before start and during recording.
- Stops recording with a clear error if free space drops below the safety
  threshold.
- Uses `kAudioFocusOwnerRecorder`.
- Pauses automatically when Xiaozhi voice interaction preempts input focus.

Current storage thresholds:

- Start requires at least 1 MB free.
- Ongoing recording requires at least 512 KB free.

#### Recorder UI Controls

Status: completed for touch control.

Relevant files:

- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`

Implemented behavior:

- Recorder page `Start`, `Pause`, and `Stop` buttons now call `AudioRecorder`.
- `Pause` works as pause/resume depending on current state.
- Recorder page shows:
  - Recording/paused/idle/error state.
  - Elapsed seconds.
  - Last checked SD free space while recording/paused.
  - Current or last file name.
- Display updates through the existing one-second status-bar tick.
- No LVGL updates are performed from the PCM observer callback.

Hardware-visible/recordable checkpoint:

- Tap `Record` in the bottom navigation, or swipe to the Recorder page.
- Tap `Start`; the page should show recording state and elapsed seconds.
- Tap `Pause`; elapsed time should stop increasing.
- Tap `Pause` again; recording should resume.
- Tap `Stop`; the WAV header should be finalized and the file should remain
  under `/sdcard/recordings`.
- Tap `Music`; the new recording should appear in the local music playlist and
  can be played by the music player.
- Wake Xiaozhi or start chat while recording; recorder should pause.

#### Recorder Voice/MCP Tools

Status: completed.

Relevant file:

- `main/boards/lichuang-dev/lichuang_dev_board.cc`

Registered tools:

- `self.recorder.start`
- `self.recorder.pause`
- `self.recorder.resume`
- `self.recorder.stop`
- `self.recorder.get_status`

Tool behavior:

- `self.recorder.start` creates a new WAV file under `/sdcard/recordings`.
- `self.recorder.pause` pauses the current recording.
- `self.recorder.resume` resumes a paused recording.
- `self.recorder.stop` finalizes the WAV file header.
- `self.recorder.get_status` returns state, file name/path, elapsed seconds,
  bytes written, last checked free bytes, and last error.

### Phase 8: MP3 Validation and Optional Enablement

#### MP3 Decoder Feasibility

Status: firmware-side decoder path implemented; hardware performance
validation still required.

Relevant files:

- `main/features/music_player.h`
- `main/features/music_player.cc`
- `main/audio/audio_service.cc`
- `main/display/lcd_display.cc`
- `main/boards/lichuang-dev/lichuang_dev_board.cc`

Component decision:

- Reuses the existing managed component `espressif/esp_audio_codec` 2.4.1.
- Uses `esp_mp3_dec_register()` and `esp_audio_simple_dec_open()` with
  `ESP_AUDIO_SIMPLE_DEC_TYPE_MP3`.
- Relies on component Kconfig `CONFIG_AUDIO_DECODER_MP3_SUPPORT`, which is
  enabled by default in the component Kconfig.
- Does not add a new third-party decoder dependency.

Implemented behavior:

- Local music playlist now includes both WAV and MP3 files from
  `/sdcard/music` and `/sdcard/recordings`.
- `MusicPlayer` dispatches WAV and MP3 through separate decode paths but keeps
  the same audio focus, pause/resume, previous/next, and status APIs.
- MP3 decode output is required to be 16-bit mono or stereo PCM.
- Decoded local music is converted to the board output channel count.
- Decoded local music is resampled to the board output sample rate when needed,
  using `esp_audio_effects` rate conversion.
- The previous WAV sample-rate-match limitation has been removed; WAV playback
  also uses the same output conversion/resampling helper.
- `AudioService::PushPcmToPlaybackQueue()` now checks playback queue capacity
  before moving the caller's PCM buffer, preventing chunk loss when the local
  music task retries a full queue.
- MP3 pause/resume stores the consumed file offset and resumes from that offset
  as a best-effort compressed-stream resume point.

Music UI and MCP updates:

- Music page text now shows `WAV/MP3` playlist counts.
- Active playback text includes the current file type.
- `self.music.list` returns WAV and MP3 tracks and includes each track `type`.
- `self.music.get_status` includes `track_type`.
- Music tool descriptions now describe local WAV or MP3 playback.

Hardware validation still required:

- Confirm `idf.py build` with `CONFIG_AUDIO_DECODER_MP3_SUPPORT=y`.
- Play 24 kHz, 44.1 kHz, and 48 kHz MP3 files.
- Test several MP3 bitrates.
- Watch for underruns while LVGL, wake word, Opus, SD IO, and camera init are
  present.
- Record heap/PSRAM watermark and task stack headroom during MP3 playback.
- Wake the device during MP3 playback and confirm local music pauses cleanly.
- Pause and resume MP3 playback and confirm the resume point is acceptable.

### Phase 9: Full-System Verification and Release Readiness

#### Integration Diagnostic Tool

Status: firmware-side diagnostic and release self-check hooks completed;
hardware regression still required.

Relevant file:

- `main/boards/lichuang-dev/lichuang_dev_board.cc`

Registered tool:

- `self.lichuang.get_diagnostics`
- `self.lichuang.run_release_self_check`

Tool behavior:

- Returns one consolidated JSON snapshot for Lichuang Dev feature regression.
- Reports board identity and marks that manual hardware verification is still
  required.
- Reports SD mount state and free bytes.
- By default, avoids scanning the SD media list to keep the tool lightweight and
  side-effect free during active playback.
- Accepts optional `scan_media=true`; when enabled and SD is mounted, returns
  media count and a preview of up to eight newest WAV/MP3 files.
- Reports environment configuration presence without exposing the AMap API key
  or aggregate URL value.
- Reports weather/AQI availability, stale/update state, city/adcode, compact
  weather fields, AQI/PM values, update timestamp, and last error.
- Reports Pomodoro state, duration, remaining seconds, and elapsed seconds.
- Reports local music state, playlist size, track metadata, sample rate,
  channels, elapsed/duration, and last error.
- Reports recorder state, current/last file, elapsed seconds, written bytes,
  last checked free bytes, and last error.
- Reports runtime heap watermarks:
  - total free heap
  - minimum free heap
  - internal free heap
  - minimum internal free heap
  - SPIRAM free heap
  - minimum SPIRAM free heap

Suggested use during hardware regression:

- Call `self.lichuang.get_diagnostics` before testing to capture baseline SD,
  environment, feature, and heap state.
- Call it during WAV/MP3 playback without `scan_media` to avoid unnecessary SD
  scanning.
- Call it with `scan_media=true` after adding files or stopping a recording to
  confirm the media list sees expected WAV/MP3 files.
- Capture the returned JSON alongside monitor logs for the Phase 9 handoff.

#### Release Self-Check Tool

Status: completed for firmware-side preflight guidance.

Relevant file:

- `main/boards/lichuang-dev/lichuang_dev_board.cc`

Registered tool:

- `self.lichuang.run_release_self_check`

Tool behavior:

- Returns a compact JSON preflight report with top-level `status`, `failures`,
  `warnings`, and `checks`.
- Uses `pass`, `warn`, and `fail` check levels.
- Does not replace manual hardware testing; it is intended to make release
  readiness gaps visible before or during the hardware checklist.
- Checks SD mount status as a release-blocking failure when missing.
- Checks SD free space against recorder start threshold and a larger
  recommended headroom warning threshold.
- Checks media playlist only when `scan_media=true`; otherwise warns that the
  media scan was intentionally skipped.
- Checks AMap/weather/AQI readiness as warnings rather than hard failures so
  offline or unconfigured API tests can still proceed with clear limitations.
- Checks Pomodoro service reachability.
- Checks Music and Recorder services for active error state.
- Checks total/internal heap headroom with conservative self-check thresholds.

Suggested use during hardware regression:

- Run `self.lichuang.run_release_self_check` after boot to catch missing SD,
  missing environment config, active service errors, or low heap before a long
  test pass.
- Run `self.lichuang.run_release_self_check` with `scan_media=true` after
  copying media files or saving a recording.
- Treat any `fail` result as a blocker for release handoff until the hardware
  cause is understood.
- Treat `warn` results as items to either fix, intentionally accept, or document
  in the final handoff notes.

#### Release Handoff Document

Status: completed for documentation handoff draft.

Relevant file:

- `docs/lichuang-dev-ai-features-release-handoff.md`

Documented content:

- Build target and manual ESP-IDF build/flash flow.
- Lichuang Dev S3 board selection requirement.
- MP3 decoder Kconfig confirmation.
- SD card mount point, media directories, supported file extensions, and
  recording output naming.
- Environment NVS keys and configuration tool usage without committing secrets.
- Pomodoro, Music, Recorder, Environment, Diagnostics, and Release Self-Check
  tool inventory.
- Recommended diagnostics workflow with and without `scan_media`.
- Hardware regression checklist for boot/storage, standby UI, navigation,
  Pomodoro, Music, Recorder, and diagnostics capture.
- Troubleshooting notes for SD card, playlist, WAV/MP3 playback, recording,
  weather/AQI, and battery limitations.
- Known limitations and release-candidate artifacts to capture.

Acceptance status:

- A future developer has a single handoff document that explains how to build,
  configure, flash, and test the feature set.
- Limitations are explicit in the handoff document instead of being hidden in
  code comments.
- Real API keys or private endpoints are not included.

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
- ui-ux-pro-max design-system search for a compact embedded touchscreen
  dashboard, selecting dense spacing, red recording/timer emphasis, and blue
  navigation/playback emphasis.
- ui-ux-pro-max design-system search for HUD / Sci-Fi FUI style, selecting a
  dense dark technology control-panel direction for all feature modules.
- Context7 LVGL documentation check for button event callbacks, callback user
  data, child label updates, and hidden object flags.
- Static inspection confirming the Lichuang feature shell now exposes bottom
  navigation buttons for Home, Timer, Music, and Record.
- Static inspection confirming bottom navigation callbacks call the same
  `SetLichuangFeatureView()` path as the swipe fallback.
- Static inspection confirming Pomodoro running/paused state hides the preset
  row and presents the dedicated countdown UI.
- Static inspection confirming `self.pomodoro.start` requests
  `Display::ShowPomodoroTimerView()` after a successful timer start.
- Static inspection confirming the Lichuang display override keeps the Timer
  page visible briefly during voice response and then falls back to normal idle
  shell visibility.
- Static inspection confirming Recorder UI uses explicit `Start` and `Stop`
  buttons wired to the existing `AudioRecorder` service.
- Static inspection confirming Pomodoro source is included in `main/CMakeLists.txt`.
- Static inspection confirming Pomodoro UI controls and MCP tools call the same
  service.
- Static inspection confirming EnvironmentService source is included in
  `main/CMakeLists.txt`.
- Static inspection confirming Home standby UI reads environment snapshots and
  never performs HTTP directly.
- Static inspection confirming environment configuration is stored in NVS and
  no real API key is committed.
- Static inspection confirming `MusicPlayer` source is included in
  `main/CMakeLists.txt`.
- Static inspection confirming Music UI buttons call the same `MusicPlayer`
  service as MCP tools.
- Static inspection confirming local music output uses audio focus and the
  existing AudioService playback queue.
- Static inspection confirming `AudioRecorder` source is included in
  `main/CMakeLists.txt`.
- Static inspection confirming Recorder UI buttons call the same
  `AudioRecorder` service as MCP tools.
- Static inspection confirming recorder input uses audio focus and the existing
  PCM observer path instead of polling the codec directly.
- Static inspection confirming recorder start retries board storage readiness
  before returning an SD-not-mounted error.
- Context7/official-source check for ESP-IDF component configuration, followed
  by local inspection of `espressif/esp_audio_codec` 2.4.1 headers and Kconfig.
- Static inspection confirming `CONFIG_AUDIO_DECODER_MP3_SUPPORT` exists and
  `esp_mp3_dec_register()` is available in the managed component.
- Static inspection confirming `ESP_AUDIO_SIMPLE_DEC_TYPE_MP3` and
  `esp_audio_simple_dec_open()` are available for MP3 stream decoding.
- Static inspection confirming Music UI and MCP tools now refer to WAV/MP3
  playlists and expose track type.
- Static inspection confirming local music resampling uses the rate-converter
  sample-point convention instead of interleaved `int16_t` count.
- Static inspection confirming the Pomodoro, Music, and Recorder LVGL pages now
  expose progress bars, dynamic button labels, and detail labels bound to the
  existing feature services.
- Static inspection confirming the Music page does not periodically rebuild the
  SD playlist while local playback is active.
- Context7 ESP-IDF documentation check for runtime heap diagnostics APIs,
  followed by local inspection of existing `SystemInfo` heap helpers.
- Static inspection confirming `self.lichuang.get_diagnostics` returns
  storage, environment, Pomodoro, music, recorder, and heap state without
  exposing environment secrets.
- Static inspection confirming diagnostic media scanning is opt-in through
  `scan_media=true`.
- Static inspection confirming `self.lichuang.run_release_self_check` produces
  pass/warn/fail release preflight checks for SD, media, environment,
  Pomodoro, music, recorder, and heap state.
- Static inspection confirming `docs/lichuang-dev-ai-features-release-handoff.md`
  documents build/configuration flow, runtime tools, diagnostics, hardware
  checklist, troubleshooting, limitations, and required handoff artifacts.
- Static inspection confirming the handoff document does not include a real
  AMap API key or private aggregate endpoint.

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
- If recording reports SD not mounted with a known-good card, check monitor
  logs for `Failed to mount SD card at /sdcard: <error>` after tapping `Start`.
  The recorder now retries mount at recording start, so this log is the next
  useful diagnostic point.
- Confirm `/sdcard/music` and `/sdcard/recordings` are created.
- Put MP3/WAV files in both directories and confirm newest-first scanner
  ordering through Music UI or `self.music.list`.
- Call `self.lichuang.get_diagnostics` and save the returned JSON as the
  baseline Phase 9 diagnostic snapshot.
- Call `self.lichuang.run_release_self_check` and confirm the returned
  `status`, `failures`, `warnings`, and `checks` match the current hardware
  setup.
- During playback/recording, call `self.lichuang.get_diagnostics` without
  `scan_media` and confirm feature state plus heap watermarks are sensible.
- After adding music files or stopping a recording, call
  `self.lichuang.get_diagnostics` with `scan_media=true` and confirm the media
  count/preview matches the SD card.
- After adding music files or stopping a recording, call
  `self.lichuang.run_release_self_check` with `scan_media=true` and confirm the
  media playlist check passes.
- Confirm display, touch, audio, camera, and BOOT button behavior are not regressed.
- Trigger wake word or chat and confirm no audio deadlock or crash appears in logs.
- Confirm idle screen shows the Home standby page.
- Tap the bottom `Home`, `Timer`, `Music`, and `Record` buttons and confirm the
  selected page changes immediately with the active navigation state highlighted.
- Confirm all four feature pages use the same dark HUD technology style:
  cyan borders, dark panels/buttons, and neon state accents.
- Swipe left/right and confirm pages still cycle through Home, Pomodoro, Music,
  and Recorder as a fallback gesture.
- On the Music page, confirm SD mounted/free-space/media-count display with a
  card inserted.
- On the Music page, confirm graceful `SD: not mounted` display without a card.
- On the Pomodoro, Music, and Recorder pages, confirm progress bars and dynamic
  button labels update correctly during start, pause, resume, and stop flows.
- Trigger wake/listen/speak from each feature page and confirm the Xiaozhi
  interaction UI appears, then returns to the standby shell when idle again.
- Test Pomodoro UI quick starts: 15m, 30m, and 45m.
- Test Pomodoro pause, resume, and cancel from the touch screen.
- Start Pomodoro by Xiaozhi AI voice and confirm the screen jumps to the Timer
  countdown UI while the voice response finishes.
- Test voice/tool calls for start, pause, resume, cancel, and status.
- Test an invalid voice/tool duration such as 181 minutes and confirm the tool
  returns the range error.
- For hardware completion testing, temporarily use the 1-minute lower bound or
  a short debug edit, then confirm the finish notification and prompt sound.
- Boot with no environment config and confirm Home shows the AMap key
  configuration hint.
- Configure environment values with `self.environment.set_config`.
- Trigger `self.environment.refresh` and confirm `self.environment.get_status`
  reports weather or a clear error.
- Confirm Home displays city/weather/temperature/humidity after a successful
  AMap weather request.
- Confirm AQI remains unavailable if no aggregate endpoint is configured.
- If an aggregate endpoint is configured, confirm AQI/category/PM2.5 display.
- Confirm `CONFIG_AUDIO_DECODER_MP3_SUPPORT=y` in the ESP-IDF configuration.
- Copy 16-bit PCM WAV files and MP3 files to `/sdcard/music`.
- On the Music page, confirm `Play` starts audible speaker playback.
- Confirm `Prev` and `Next` switch through WAV/MP3 files in newest-first order.
- Confirm `self.music.list` returns the same WAV/MP3 playlist and includes
  each track `type`.
- Confirm `self.music.play`, `self.music.pause`, `self.music.next`,
  `self.music.previous`, and `self.music.get_status` work.
- Test 24 kHz, 44.1 kHz, and 48 kHz WAV/MP3 files and confirm resampled
  playback is intelligible through the speaker.
- Test several MP3 bitrates and record whether underruns, glitches, or task
  watchdog warnings appear.
- During MP3 playback, record heap/PSRAM watermark and relevant task stack
  headroom from monitor logs if available.
- Pause and resume MP3 playback and confirm the best-effort resume point is
  acceptable.
- Wake the device or start chat during local music playback and confirm music
  pauses.
- On the Recorder page, tap `Start` and confirm elapsed seconds increase.
- Tap `Pause` and confirm elapsed seconds stop increasing.
- Tap `Pause` again and confirm recording resumes.
- Tap `Stop` and confirm a `REC_YYYYMMDD_HHMMSS.wav` file appears under
  `/sdcard/recordings`.
- Confirm `self.recorder.start`, `self.recorder.pause`,
  `self.recorder.resume`, `self.recorder.stop`, and
  `self.recorder.get_status` work.
- Switch to the Music page after stopping a recording and confirm the new WAV
  appears newest-first.
- Play the new recording from the Music page.
- Wake the device or start chat while recording and confirm the recorder pauses.
- Inspect the saved WAV header on a desktop tool if hardware logs suggest
  playback issues.

## Current Modified Files

- `main/CMakeLists.txt`
- `main/application.cc`
- `main/audio/audio_service.h`
- `main/audio/audio_service.cc`
- `main/boards/lichuang-dev/lichuang_dev_board.cc`
- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`
- `docs/lichuang-dev-ai-features-release-handoff.md`
- `main/features/audio_recorder.h`
- `main/features/audio_recorder.cc`
- `main/features/music_player.h`
- `main/features/music_player.cc`
- `main/features/pomodoro_timer.h`
- `main/features/pomodoro_timer.cc`
- `main/services/environment_service.h`
- `main/services/environment_service.cc`
- `main/storage/sd_card_service.h`
- `main/storage/sd_card_service.cc`

## Known Limitations

- No ESP-IDF build result has been recorded yet.
- No SD card hardware mount result has been recorded yet.
- No hardware touch-swipe verification result has been recorded yet.
- No `self.lichuang.get_diagnostics` hardware snapshot has been recorded yet.
- No `self.lichuang.run_release_self_check` hardware result has been recorded
  yet.
- MP3 playback is implemented through `esp_audio_codec`, but no hardware
  performance result has been recorded yet.
- MP3 pause/resume uses a best-effort consumed file offset; exact compressed
  stream resume accuracy still needs listening validation.
- Local music playback still supports only decoded 16-bit mono/stereo PCM
  output. Unsupported files should fail with an error instead of playing noise.
- WAV recording is fixed at 16000 Hz mono 16-bit PCM.
- Battery percentage remains hardware-dependent and is not implemented.
- Pomodoro completion currently uses screen notification plus success prompt
  sound, not a custom generated TTS sentence.
- Weather depends on a valid AMap key configured in NVS.
- AQI depends on a user-provided/self-hosted aggregate endpoint; AMap weather
  alone does not provide the full AQI fields.
- IP-based city detection can be inaccurate under VPN/carrier NAT; fallback
  city/adcode config is available.

## Next Recommended Phase

After `idf.py build` passes and the Phase 8/9 diagnostics and self-check paths
are confirmed on hardware, continue with:

- Phase 9: Full-system hardware verification and final release signoff

Planned next tasks:

- Run the full hardware regression checklist for standby, weather/AQI,
  Pomodoro, WAV/MP3 music, recording, SD mount, wake interruption, and status
  bar.
- Follow `docs/lichuang-dev-ai-features-release-handoff.md` on the target
  hardware and note any corrections needed.
- Capture monitor logs for at least one clean pass.
- Capture at least one `self.lichuang.get_diagnostics` baseline snapshot and
  one snapshot during music playback or recording.
- Capture at least one `self.lichuang.run_release_self_check` result with
  `scan_media=true`.
- Record MP3 heap/PSRAM/task-stack observations and decide whether any
  menuconfig defaults should be tightened before release.
- Update the handoff document with actual hardware results, accepted warnings,
  and final release notes.
