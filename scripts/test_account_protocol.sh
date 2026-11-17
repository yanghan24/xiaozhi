#!/usr/bin/env bash
set -euo pipefail
: "${ACCOUNT_TEST_BUILD_DIR:?Choose a container runtime directory outside the source checkout}"
: "${CPP_SYSROOT:?Use the native toolchain from the Xiaozhi runtime volume}"
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ACCOUNT_TEST_BUILD_DIR/tmp"
export TMPDIR="$ACCOUNT_TEST_BUILD_DIR/tmp"
export LD_LIBRARY_PATH="$CPP_SYSROOT/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$CPP_SYSROOT/usr/bin/g++" --sysroot="$CPP_SYSROOT" -B"$CPP_SYSROOT/usr/bin" -std=c++17 -Wall -Wextra -Werror \
  -I"$CPP_SYSROOT/usr/include/cjson" -I"$root/main/account" \
  "$root/tests/account/protocol_test.cc" "$root/main/account/account_protocol.cc" -lcjson \
  -o "$ACCOUNT_TEST_BUILD_DIR/protocol-test"
LD_LIBRARY_PATH="$CPP_SYSROOT/usr/lib/x86_64-linux-gnu" "$ACCOUNT_TEST_BUILD_DIR/protocol-test"
