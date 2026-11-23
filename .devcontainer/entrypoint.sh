#!/bin/sh
set -eu
if [ "$(id -u)" = 0 ]; then
    for path in /home/dev/.m2 /home/dev/.npm /home/dev/.cache /home/dev/.venv \
        /opt/xiaozhi-dev /workspace/main/manager-web/node_modules \
        /workspace/main/manager-web/dist /workspace/main/manager-api/target \
        /workspace/main/xiaozhi-server/data /workspace/main/xiaozhi-server/tmp \
        /workspace/main/manager-api/uploadfile; do
        mkdir -p "$path"
        chown dev:dev "$path"
    done
    exec gosu dev "$@"
fi
exec "$@"
