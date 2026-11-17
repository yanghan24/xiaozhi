#!/usr/bin/env bash
set -euo pipefail
: "${SPEAKER_TEST_BUILD_DIR:?Choose a build directory outside the source checkout}"
: "${CJSON_INCLUDE_DIR:?Choose the cJSON include directory}"
source_root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$SPEAKER_TEST_BUILD_DIR"
export TMPDIR="$SPEAKER_TEST_BUILD_DIR/tmp"
mkdir -p "$TMPDIR"
flags=(-std=c++17 -Wall -Wextra -Werror -I"$CJSON_INCLUDE_DIR" -I"$source_root/main/device_control")
if [[ -n ${CPP_SYSROOT:-} ]]; then flags+=(--sysroot="$CPP_SYSROOT" -B"$CPP_SYSROOT/usr/bin"); fi
sources=("$source_root/main/device_control/speaker_model.cc" "$source_root/main/device_control/control_model.cc")
"${CXX:-g++}" "${flags[@]}" "$source_root/tests/device_control/speaker_test.cc" "${sources[@]}" -lcjson -o "$SPEAKER_TEST_BUILD_DIR/speaker_test"
"$SPEAKER_TEST_BUILD_DIR/speaker_test"
"${CXX:-g++}" "${flags[@]}" -DCONFIG_XIAOZHI_SPEAKER_PROTOCOL_V2=1 '-DBOARD_TYPE="fixture-board"' \
    -I"$source_root/tests/device_control/stubs" "$source_root/tests/device_control/speaker_worker_test.cc" \
    "$source_root/main/device_control/device_control.cc" "${sources[@]}" -lcjson -pthread -o "$SPEAKER_TEST_BUILD_DIR/speaker_worker_test"
"$SPEAKER_TEST_BUILD_DIR/speaker_worker_test"
