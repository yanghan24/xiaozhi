#!/usr/bin/env bash
# Run on the designated Ubuntu host, from the isolated acceptance root, as root.
# The existing funded test device and synthetic audio are private prerequisites.
set -euo pipefail
test "$(uname -s)" = Linux
test "$(id -u)" = 0
root="${WEATHER43_ROOT:?Set the isolated acceptance root}"
runtime="${WEATHER43_PYTHON_IMAGE:?Set the verified existing runtime image ID}"
audio="${WEATHER43_SYNTHETIC_AUDIO:?Set the synthetic input WAV path}"
cd "$root"
ulimit -n 8192
test -f evidence/long-load-suite-complete
test "$(cat private/fault.json)" = '{}'
test ! -e evidence/coexistence-complete

voice_phase() {
  local phase="$1"
  docker run --rm --user 0:0 --name weather43-voice-probe --network host \
    --read-only --memory 384m --cpus 1 --cap-drop ALL --security-opt no-new-privileges \
    --tmpfs /tmp:rw,nosuid,size=64m --entrypoint python \
    -v "$root/scripts/weather43/voice_coexist.py:/probe.py:ro" \
    -v "$root/private/voice-test-device.json:/private/device.json:ro" \
    -v "$audio:/private/input.wav:ro" -v "$root/evidence/voice:/evidence:rw" \
    "$runtime" /probe.py --device /private/device.json --audio /private/input.wav \
    --connect-host 192.168.168.114 --output "/evidence/$phase" --phase "$phase" --count 10 --interval 5
}

weather() {
  python3 scripts/weather43/acceptance_load.py --cities-file private/cities.json \
    --data-dir private/model-data "$@"
}

# Source code was staged while the baseline was running; only this trial service restarts.
cp scripts/weather43/cache_service.py evidence/source-versions/cache-service-v3-backlog.py
docker compose --env-file private/acceptance.env -p xiaozhi-weather43-acceptance \
  --project-directory "$root" -f deploy/weather43/compose.acceptance.yaml restart result-cache
sleep 10
for devices in 350 1000; do
  for cities in 1 35 350; do
    for round in 1 2 3; do
      weather --devices "$devices" --cities "$cities" --scenario burst \
        --output "evidence/backlog-burst-$devices-cities$cities-round$round"
    done
  done
done

# No offered weather HTTP load in baseline/after; ordinary source refresh remains enabled.
voice_phase before-weather-load
for devices in 350 1000; do
  weather --devices "$devices" --cities 350 --scenario steady --duration 1800 \
    --output "evidence/coexist-weather-$devices" &
  load_pid=$!
  sleep 60
  voice_phase "during-weather-$devices"
  wait "$load_pid"
done
voice_phase after-weather-load
date -u +%FT%TZ > evidence/coexistence-complete
