# Wake greeting and interruption

Run `python3 tests/wake_word/check_greeting.py` from the firmware root.
The runner compiles the actual `Application` wake handlers with recording
boundaries for audio, transport and device state. It checks that:

- An idle wake sends one `listen/detect` request for the server's greeting.
- A wake during playback aborts the old answer and clears buffered audio before
  entering the next listening turn, without a greeting request or popup.
- Revoked devices and failed audio connections do not request a greeting.

The test fails on the `5cfe406` baseline because interruption enters listening
without clearing the old playback queue and schedules a popup. It passes with
the silent interruption change.

This is a host control-flow test. It does not exercise the P4 microphone,
WakeNet recognition during playback, audio drivers or physical stopping latency.
