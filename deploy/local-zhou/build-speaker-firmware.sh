#!/usr/bin/env bash
set -euo pipefail

runtime=/opt/xiaozhi-dev
source_dir=${SPEAKER_FIRMWARE_SOURCE_DIR:-$runtime/run/p4x-260-rebuild/product}
sdk=$runtime/tools/idf-v6.1-f011425
host_tools=$runtime/tools/idf-tools/tools

case "$source_dir" in
    "$runtime"/*) ;;
    *) echo 'Use the container runtime source copy'; exit 1 ;;
esac

test -f /workspace/.devcontainer/supervisord.conf
test -f "$source_dir/locks/esp32p4-v2.6.0.lock"
test -f "$sdk/source-archive/export.sh"

# The sibling worktree is outside the container mount. Build the explicitly
# synchronized source copy and record the worktree commit and any edits.
git --git-dir=/workspace/.git/worktrees/xiaozhi-firmware --work-tree="$source_dir" rev-parse HEAD
git --git-dir=/workspace/.git/worktrees/xiaozhi-firmware --work-tree="$source_dir" diff --stat

export IDF_TOOLS_PATH="$sdk/tools"
export IDF_GITHUB_ASSETS=dl.espressif.com/github_assets
export IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.cn
export PYTHONPYCACHEPREFIX="$runtime/pycache"
export TMPDIR="$runtime/run/tmp"
export PATH="$host_tools/cmake/3.30.2/bin:/usr/local/bin:/usr/bin:/bin"
unset VIRTUAL_ENV IDF_PYTHON_ENV_PATH

source "$sdk/source-archive/export.sh"
build_tools=$runtime/run/p4x-260-rebuild/bin
mkdir -p "$TMPDIR" "$build_tools"
printf '%s\n' '#!/bin/sh' "exec $host_tools/ninja/1.12.1/ninja -j4 \"\$@\"" > "$build_tools/ninja"
chmod +x "$build_tools/ninja"
export PATH="$build_tools:$PATH"

cd "$source_dir"
cp locks/esp32p4-v2.6.0.lock dependencies.lock
python scripts/build.py waveshare/esp32-p4-wifi6-touch-lcd \
    --name esp32-p4x-wifi6-touch-lcd-4.3-market --language zh-CN --zip
