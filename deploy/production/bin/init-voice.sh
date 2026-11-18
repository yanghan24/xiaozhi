#!/bin/sh
set -eu
umask 077
cd "${XIAOZHI_APP_DIR:-/data/apps/xiaozhi}"
destination=config/voice/.config.yaml
test ! -e "$destination" || exit 0

case "${XIAOZHI_DOCKER_SUDO:-1}" in
  1) set -- sudo docker compose ;;
  0) set -- docker compose ;;
  *) exit 2 ;;
esac
sql="SELECT param_value FROM sys_params WHERE param_code='server.secret' AND EXISTS (SELECT 1 FROM sys_user WHERE username='admin' AND super_admin=1)"
secret=''
for attempt in $(seq 1 90); do
  secret=$("$@" exec -T mysql sh -c 'MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=TCP -h 127.0.0.1 -u xiaozhi -D xiaozhi -NB -e "$1"' _ "$sql" 2>/dev/null) || true
  if printf '%s' "$secret" | grep -Eq '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$'; then
    break
  fi
  sleep 2
done
printf '%s' "$secret" | grep -Eq '^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$'
temp=$(mktemp config/voice/.config.XXXXXX)
trap 'rm -f "$temp"' EXIT
cat > "$temp" <<EOF
server:
  ip: 0.0.0.0
  port: 8000
  http_port: 8003
manager-api:
  url: http://api:8002/xiaozhi
  secret: "$secret"
prompt_template: agent-base-prompt.txt
EOF
mv "$temp" "$destination"
