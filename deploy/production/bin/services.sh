#!/bin/sh
set -eu
apps=/data/apps
case "${1:-}" in
  start)
    cd "$apps/xiaozhi"
    test -s config/voice/.config.yaml
    sudo -n docker compose up -d --no-build --pull never
    cd "$apps/nginx"
    sudo -n docker compose up -d --no-build --pull never
    ;;
  stop)
    cd "$apps/nginx"
    sudo -n docker compose stop
    cd "$apps/xiaozhi"
    sudo -n docker compose stop
    ;;
  status)
    cd "$apps/xiaozhi"
    sudo -n docker compose ps
    cd "$apps/nginx"
    sudo -n docker compose ps
    ;;
  *)
    printf 'Usage: %s start|stop|status\n' "$0" >&2
    exit 2
    ;;
esac
