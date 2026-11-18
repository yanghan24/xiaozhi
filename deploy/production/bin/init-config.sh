#!/bin/sh
set -eu
umask 077
app=${XIAOZHI_APP_DIR:-/data/apps/xiaozhi}
release=${1:?Usage: init-config.sh RELEASE_TAG}
case "$release" in *[!a-zA-Z0-9_.-]*|'' ) exit 2;; esac
cd "$app"
test ! -e .env
mkdir -p config/api config/voice mysql/data redis/data storage logs/api logs/voice
for folder in mysql/data redis/data; do
  test -z "$(ls -A "$folder")"
done

random_password() {
  printf 'Az1'
  openssl rand -hex 24
}

{
  printf 'RELEASE_TAG=%s\nRUN_UID=%s\nRUN_GID=%s\n' "$release" "$(id -u)" "$(id -g)"
  for name in MYSQL_ROOT_PASSWORD MYSQL_PASSWORD REDIS_PASSWORD; do
    printf '%s=%s\n' "$name" "$(random_password)"
  done
} > .env
test -e config/api/speaker.properties || cp speaker.properties.example config/api/speaker.properties
for f in config/api/identity.key config/api/bootstrap-admin.password; do
  test ! -e "$f"
done
openssl rand -base64 32 > config/api/identity.key
random_password > config/api/bootstrap-admin.password
