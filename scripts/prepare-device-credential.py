#!/usr/bin/env python3
"""Prepare a single pre-registration for a trusted operator; never connects to a database.

Run on Ubuntu in a private directory. Import registration.sql through the existing
private database administration channel, then hand device.json to the authorized
device provisioning process. This does not specify a firmware storage location.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--output", type=Path, required=True, help="New private directory; must not exist")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Run on Ubuntu so POSIX permissions protect the device credential")
    if not re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", args.device_id):
        parser.error("device-id must be 1-128 ASCII letters, digits, underscores, dots, colons or hyphens")

    # Exclusive directory creation refuses overwrites, including symlinks and repeated registration packages.
    args.output.mkdir(mode=0o700)
    credential = secrets.token_urlsafe(32)
    digest = hashlib.sha256(credential.encode("ascii")).hexdigest()
    files = {
        "device.json": json.dumps({"device_id": args.device_id, "credential": credential}) + "\n",
        "registration.sql": (
            "-- INSERT only: an existing device must fail rather than overwrite ownership or credentials.\n"
            "INSERT INTO dc_device(device_id,credential_hash) VALUES "
            f"('{args.device_id}','{digest}');\n"
        ),
    }
    for name, contents in files.items():
        fd = os.open(args.output / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(contents)
    print("Prepared private device.json and registration.sql; no database or device was changed.")


if __name__ == "__main__":
    main()
