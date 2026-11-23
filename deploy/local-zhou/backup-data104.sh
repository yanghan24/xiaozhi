#!/bin/bash
set -euo pipefail
umask 077
cd /home/snow/app/xiaozhi-dev
stamp=$(date +%Y%m%d-%H%M%S)
destination="$PWD/backups/$stamp"
mkdir -m 700 "$destination"
# Development data only. No shared database or AI Market instance is accessed.
docker compose exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_PASSWORD" mysqldump -uxiaozhi_dev --single-transaction --no-tablespaces --no-data xiaozhi_dev' | gzip > "$destination/schema.sql.gz"
docker compose exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_PASSWORD" mysqldump -uxiaozhi_dev --single-transaction --no-tablespaces --no-create-info --ignore-table=xiaozhi_dev.ai_agent_chat_history --ignore-table=xiaozhi_dev.ai_agent_chat_audio --ignore-table=xiaozhi_dev.ai_agent_chat_title --ignore-table=xiaozhi_dev.ai_chat_history --ignore-table=xiaozhi_dev.ai_chat_message xiaozhi_dev' | gzip > "$destination/mysql.sql.gz"
docker compose exec -T redis sh -c 'REDISCLI_AUTH=$(sed -n "s/^requirepass //p" /usr/local/etc/redis/redis.conf) redis-cli --rdb /data/backup.rdb' > /dev/null
docker compose cp redis:/data/backup.rdb "$destination/redis.rdb" > /dev/null
cp .env redis.conf compose.yaml "$destination/"
(cd "$destination" && sha256sum schema.sql.gz mysql.sql.gz redis.rdb .env redis.conf compose.yaml > SHA256SUMS)
printf 'Backup saved: %s\n' "$destination"
