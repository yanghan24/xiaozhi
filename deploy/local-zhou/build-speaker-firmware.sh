#!/usr/bin/env bash
set -euo pipefail
source_dir=${SPEAKER_FIRMWARE_SOURCE_DIR:-/opt/xiaozhi-dev/restoration-firmware}
build_dir=/opt/xiaozhi-dev/run/account-p4
sdk_dir=/opt/xiaozhi-dev/tools/idf-v5.5.5/esp-idf-v5.5.5
case "$source_dir" in /opt/xiaozhi-dev/*) ;; *) echo 'Use the container runtime source copy'; exit 1 ;; esac
test -f /workspace/.devcontainer/supervisord.conf
test -f "$source_dir/sdkconfig.defaults.account"
test -f "$source_dir/locks/phase1-p4-dependencies.lock"
cp "$source_dir/locks/phase1-p4-dependencies.lock" "$source_dir/dependencies.lock"
# The sibling checkout is outside this container's bind mount. Its reviewed
# runtime copy is synchronized explicitly; record the base and any edits being tested.
git --git-dir=/workspace/.git/worktrees/xiaozhi-firmware --work-tree="$source_dir" rev-parse HEAD
git --git-dir=/workspace/.git/worktrees/xiaozhi-firmware --work-tree="$source_dir" diff --stat
export IDF_TOOLS_PATH=/opt/xiaozhi-dev/tools/idf-tools
export IDF_PYTHON_ENV_PATH=/opt/xiaozhi-dev/tools/idf-python
export IDF_GITHUB_ASSETS=dl.espressif.com/github_assets
export IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.cn
export PYTHONPYCACHEPREFIX=/opt/xiaozhi-dev/pycache
export TMPDIR=/opt/xiaozhi-dev/run/tmp
mkdir -p "$TMPDIR"
unset VIRTUAL_ENV
export PATH=/usr/local/bin:/usr/bin:/bin
source "$sdk_dir/export.sh"
mkdir -p "$build_dir"
idf.py -C "$source_dir" -B "$build_dir" -DIDF_TARGET=esp32p4 \
  -DSDKCONFIG="$build_dir/sdkconfig" \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.account' reconfigure
# Keep this developer container's other services responsive.
ninja -C "$build_dir" -j4
