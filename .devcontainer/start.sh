#!/bin/sh
set -eu
mkdir -p /opt/xiaozhi-dev/logs /opt/xiaozhi-dev/run
exec supervisord -c /workspace/.devcontainer/supervisord.conf
