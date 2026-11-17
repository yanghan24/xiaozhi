# Lichuang Dev AI Features Release Handoff

Date: 2026-07-08

This handoff covers the Lichuang Dev ESP32-S3 AI feature set:

- Standby Home UI with time, weather, and AQI
- Pomodoro timer
- Local WAV/MP3 music playback
- WAV recording
- SD card storage foundation
- Audio focus interruption behavior
- Release diagnostics and self-check tools

ESP-IDF build, flash, monitor, and hardware verification are intentionally
manual steps.

## Build Target

Target:

- ESP32-S3
- Board type: `CONFIG_BOARD_TYPE_LICHUANG_DEV_S3`

Recommended build flow:

```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py flash monitor
```

In `menuconfig`, confirm the Lichuang Dev S3 board is selected. Also confirm
MP3 decoder support remains enabled:

- `CONFIG_AUDIO_DECODER_MP3_SUPPORT=y`

The MP3 decoder option comes from the managed `espressif/esp_audio_codec`
component and defaults to enabled in the checked local component Kconfig.

## SD Card Layout

The firmware mounts the MicroSD card at:

```text
/sdcard
```

Required media directories are created automatically after a successful mount:

```text
/sdcard/music
/sdcard/recordings
```

Supported local music formats:

- `.wav`
- `.WAV`
- `.mp3`
- `.MP3`

Playlist behavior:

- Scans `/sdcard/music` and `/sdcard/recordings`
- Sorts newest modified files first
- Limits returned scan results to 200 files
- Saved recordings under `/sdcard/recordings` appear in the music playlist

Recording output:

```text
/sdcard/recordings/REC_YYYYMMDD_HHMMSS.wav
```

## Environment Configuration

Weather uses AMap/Gaode Web Service APIs. AQI uses an optional user-provided
aggregate endpoint. No real API key or private endpoint is committed in source.

Configuration is stored in NVS namespace `env` with these keys:

- `amap_key`
- `aqi_url`
- `refresh_min`
- `fb_adcode`
- `fb_city`
- `cache`

Use the user-only tool to configure environment data:

```json
{
  "name": "self.environment.set_config",
  "arguments": {
    "amap_api_key": "<your-amap-key>",
    "aggregate_url": "",
    "refresh_interval_minutes": 30,
    "fallback_adcode": "",
    "fallback_city": ""
  }
}
```

Then trigger a refresh:

```json
{
  "name": "self.environment.refresh",
  "arguments": {}
}
```

Check status:

```json
{
  "name": "self.environment.get_status",
  "arguments": {}
}
```

If no AMap key is configured, Home should show a clear configuration hint
rather than failing boot or blocking the UI.

## Runtime Tools

Pomodoro:

- `self.pomodoro.start`
- `self.pomodoro.pause`
- `self.pomodoro.resume`
- `self.pomodoro.cancel`
- `self.pomodoro.get_status`

Music:

- `self.music.play`
- `self.music.pause`
- `self.music.next`
- `self.music.previous`
- `self.music.list`
- `self.music.get_status`

Recorder:

- `self.recorder.start`
- `self.recorder.pause`
- `self.recorder.resume`
- `self.recorder.stop`
- `self.recorder.get_status`

Environment:

- `self.environment.set_config`
- `self.environment.refresh`
- `self.environment.get_status`

Release readiness:

- `self.lichuang.get_diagnostics`
- `self.lichuang.run_release_self_check`

## Diagnostics Workflow

Before a hardware test pass, capture a baseline diagnostic snapshot:

```json
{
  "name": "self.lichuang.get_diagnostics",
  "arguments": {
    "scan_media": false
  }
}
```

Run the release self-check:

```json
{
  "name": "self.lichuang.run_release_self_check",
  "arguments": {
    "scan_media": false
  }
}
```

After copying media files or stopping a recording, run both tools with media
scan enabled:

```json
{
  "name": "self.lichuang.get_diagnostics",
  "arguments": {
    "scan_media": true
  }
}
```

```json
{
  "name": "self.lichuang.run_release_self_check",
  "arguments": {
    "scan_media": true
  }
}
```

Guidance:

- Treat any self-check `fail` as a release blocker until understood.
- Treat `warn` as an item to fix, intentionally accept, or document.
- Do not use `scan_media=true` during sensitive playback timing tests unless
  the SD scan itself is part of the test.

## Hardware Regression Checklist

Boot and storage:

- Boot with SD card inserted and confirm SD mount success log.
- Boot without SD card and confirm boot continues with a warning.
- Confirm `/sdcard/music` and `/sdcard/recordings` are created.
- Copy WAV/MP3 files to `/sdcard/music`.
- Run `self.lichuang.run_release_self_check` with `scan_media=true`.

Standby UI:

- Confirm idle state shows the Home standby screen.
- Confirm time/date/weekday render after system time is valid.
- Confirm Wi-Fi/status bar still updates.
- Confirm weather/AQI placeholders are graceful when unconfigured.
- Configure environment values and confirm Home updates after refresh.

Navigation:

- Swipe left/right through Home, Pomodoro, Music, and Recorder.
- From each page, wake Xiaozhi or start chat.
- Confirm the normal interaction UI appears and returns to standby when idle.

Pomodoro:

- Tap `15m`, `30m`, and `45m`; confirm countdown starts.
- Pause, resume, and cancel from touch UI.
- Call Pomodoro tools and confirm UI/tool state stays shared.
- Test invalid custom duration such as 181 minutes.
- Confirm completion notification and prompt sound.

Music:

- Confirm the Music page shows SD/free-space/media count.
- Tap `Play`; confirm speaker playback starts.
- Tap `Pause`, `Prev`, and `Next`.
- Test mixed WAV/MP3 playlist ordering.
- Test 24 kHz, 44.1 kHz, and 48 kHz files.
- Wake Xiaozhi during playback and confirm music pauses.
- Capture diagnostics during playback without media scanning.

Recorder:

- Tap `Rec`; confirm elapsed time and written size increase.
- Tap `Pause`; confirm elapsed time stops.
- Tap `Pause` again; confirm recording resumes.
- Tap `Stop`; confirm WAV header finalizes.
- Confirm a `REC_YYYYMMDD_HHMMSS.wav` file appears under recordings.
- Switch to Music and play the new recording.
- Wake Xiaozhi during recording and confirm recorder pauses.

Release diagnostics:

- Capture one baseline `self.lichuang.get_diagnostics` result.
- Capture one `self.lichuang.get_diagnostics` result during playback or
  recording.
- Capture one `self.lichuang.run_release_self_check` result with
  `scan_media=true`.
- Save monitor logs for at least one clean full pass.

## Troubleshooting

| Symptom | Check |
|---|---|
| SD not mounted | Confirm GPIO48 is SD CMD, GPIO47 is CLK, GPIO21 is D0; check card format and monitor log from `InitializeSdCard()`. |
| Music playlist empty | Confirm files are under `/sdcard/music` or `/sdcard/recordings` and use `.wav` or `.mp3` extension. Run diagnostics with `scan_media=true`. |
| WAV fails to play | Confirm it is 16-bit PCM mono/stereo. Compressed or non-16-bit WAV files are intentionally rejected. |
| MP3 glitches or underruns | Capture heap diagnostics during playback, test lower bitrates, and watch monitor logs for queue underruns or task watchdog warnings. |
| Recording cannot start | Confirm SD is mounted and at least 1 MB free. Check recorder status and last error. |
| Recording stops early | Check SD free space; ongoing recording stops below the safety threshold. |
| Weather unavailable | Confirm AMap key is configured, Wi-Fi is connected, and `self.environment.get_status` has no API error. |
| AQI unavailable | Confirm an aggregate endpoint is configured and returns supported `air` fields. |
| Battery percentage missing | This board revision has no confirmed battery ADC/PMIC path; do not treat missing percentage as a firmware regression. |

## Known Limitations

- ESP-IDF build and flash results are not recorded by Codex.
- Hardware SD mount, touch, audio, and camera checks must be run manually.
- MP3 playback is implemented but hardware performance must still be measured.
- MP3 resume uses a best-effort compressed file offset.
- WAV playback supports decoded 16-bit mono/stereo PCM output.
- WAV recording is fixed at 16000 Hz mono 16-bit PCM.
- Pomodoro completion uses a screen notification plus the existing success
  prompt sound.
- Weather depends on a valid AMap key.
- AQI depends on a user-provided aggregate endpoint.
- Battery percentage remains hardware-dependent and is not implemented.

## Handoff Artifacts To Capture

For a release candidate, save:

- `idf.py build` result
- Flash and monitor log
- `self.lichuang.get_diagnostics` baseline JSON
- `self.lichuang.get_diagnostics` playback or recording JSON
- `self.lichuang.run_release_self_check` JSON with `scan_media=true`
- MP3 heap/PSRAM/task-stack observations
- Notes for any accepted warnings or known hardware limitations
