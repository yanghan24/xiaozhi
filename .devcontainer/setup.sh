#!/bin/sh
set -eu
cd /workspace
if [ ! -x /home/dev/.venv/bin/python ]; then
    /usr/local/bin/python -m venv /home/dev/.venv
fi
python_inputs=$({ python --version; cat deploy/voice/requirements.txt .devcontainer/requirements-dev.txt; } | sha256sum | cut -d ' ' -f 1)
if [ "$(cat /home/dev/.venv/.xiaozhi-inputs.sha256 2>/dev/null || true)" != "$python_inputs" ]; then
    python -m pip install --upgrade pip==25.3
    python -m pip install -r deploy/voice/requirements.txt -r .devcontainer/requirements-dev.txt
    printf '%s\n' "$python_inputs" > /home/dev/.venv/.xiaozhi-inputs.sha256
fi
node_inputs=$({ node --version; cat main/manager-web/package-lock.json; } | sha256sum | cut -d ' ' -f 1)
if [ "$(cat main/manager-web/node_modules/.xiaozhi-inputs.sha256 2>/dev/null || true)" != "$node_inputs" ]; then
    npm --prefix main/manager-web ci --no-audit --no-fund
    printf '%s\n' "$node_inputs" > main/manager-web/node_modules/.xiaozhi-inputs.sha256
fi
if [ ! -f main/manager-api/target/xiaozhi-esp32-api.jar ] || [ "${1:-}" = --rebuild ]; then
    mvn -B -ntp -f main/manager-api/pom.xml package -DskipTests
fi
mkdir -p /opt/xiaozhi-dev/logs /opt/xiaozhi-dev/run
printf 'Development dependencies ready. See deploy/local-zhou/devcontainer.md.\n'
