#!/usr/bin/env python3
"""Create private local database configuration without overwriting existing state."""
import os
from pathlib import Path
import secrets

root = Path(__file__).resolve().parent.parent
config = (root / ".env.example").read_text()
for key in ("MYSQL_ROOT_PASSWORD", "MYSQL_PASSWORD"):
    config = config.replace(key + "=\n", key + "=" + secrets.token_hex(24) + "\n")
try:
    descriptor = os.open(root / ".env", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
except FileExistsError:
    raise SystemExit(".env already exists; existing credentials have been preserved.")
with os.fdopen(descriptor, "w") as file:
    file.write(config)
print("Created private .env. Register the test administrator through the management page after startup.")
