#!/bin/sh
set -eu
cd /home/snow/app/xiaozhi-dev
test "$(id -un)" = snow
test -f .env
test -f redis.conf
test -d data/mysql
test -d data/redis
docker compose config --quiet
docker compose up -d --wait --wait-timeout 240
docker compose ps
