#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /workspace

revision=$(git rev-parse "${1:-HEAD}^{commit}")
out="/opt/xiaozhi-dev/run/release-${revision:0:12}"
if [[ -e "$out" ]]; then
  printf 'Release work directory already exists: %s\n' "$out" >&2
  exit 1
fi
mkdir -p "$out/source" "$out/logs" "$out/tmp" "$out/api" "$out/web" "$out/voice/app" "$out/voice/wheels"
export TMPDIR="$out/tmp"
git archive "$revision" main/manager-api main/manager-web | tar -x -C "$out/source"
git archive "$revision:main/xiaozhi-server" | tar -x -C "$out/voice/app"
git show "$revision:deploy/voice/requirements.txt" > "$out/voice/requirements.txt"
ln -s /workspace/main/manager-web/node_modules "$out/source/main/manager-web/node_modules"

(
  mvn -o -B -ntp -f "$out/source/main/manager-api/pom.xml" package -DskipTests
  cp "$out/source/main/manager-api/target/xiaozhi-esp32-api.jar" "$out/api/app.jar"
) > "$out/logs/api.log" 2>&1 &
api_pid=$!
(
  cd "$out/source/main/manager-web"
  npm run build
  cp -a dist "$out/web/dist"
) > "$out/logs/web.log" 2>&1 &
web_pid=$!
/home/dev/.venv/bin/python -m pip download --only-binary=:all: --timeout 30 --retries 2 \
  --dest "$out/voice/wheels" -r "$out/voice/requirements.txt" > "$out/logs/voice.log" 2>&1 &
voice_pid=$!

failed=0
for pid in "$api_pid" "$web_pid" "$voice_pid"; do
  wait "$pid" || failed=1
done
if (( failed )); then
  printf 'Build failed. See %s/logs/.\n' "$out" >&2
  exit 1
fi
printf '%s\n' "$revision" > "$out/revision"
printf '%s\n' "$out"
