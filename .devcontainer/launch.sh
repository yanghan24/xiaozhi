#!/bin/sh
set -eu
if [ -f /opt/xiaozhi-dev/ready ]; then
    exec supervisord -n -c /workspace/.devcontainer/supervisord.conf
fi
exec sleep infinity
