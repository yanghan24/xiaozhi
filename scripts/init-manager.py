#!/usr/bin/env python3
"""Create private local database configuration without overwriting existing state."""
import os
from pathlib import Path
import secrets

root = Path(__file__).resolve().parent.parent
development = root / "deploy/development"
legacy_env = root / ".env"
if legacy_env.exists() or legacy_env.is_symlink():
    raise SystemExit("Existing root .env preserved. Use --env-file .env with the development Compose files.")
config = (development / ".env.example").read_text()
for key in ("MYSQL_ROOT_PASSWORD", "MYSQL_PASSWORD"):
    config = config.replace(key + "=\n", key + "=" + secrets.token_hex(24) + "\n")
try:
    descriptor = os.open(development / ".env", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
except FileExistsError:
    raise SystemExit("deploy/development/.env already exists; existing credentials have been preserved.")
with os.fdopen(descriptor, "w") as file:
    file.write(config)
print("Created deploy/development/.env. Register the test administrator through the management page after startup.")
