#!/usr/bin/env python3
"""Offline atomic switch/rollback drill with the actual downloaded release.

Both valid slots contain the SAME real release. This verifies update mechanics,
not the coverage of a future monthly database that has not been published yet.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil

from location_candidates import CityCatalog, IpDatabase


def sha256(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1048576), b""):
            value.update(block)
    return value.hexdigest()


def validate(folder, expected):
    if sha256(folder / "database.mmdb") != expected:
        raise ValueError("database_checksum")
    database = IpDatabase(folder / "database.mmdb")
    try:
        metadata = database.reader.metadata()
        if metadata.ip_version != 6:
            raise ValueError("missing_ipv6_tree")
        catalog = CityCatalog(folder / "catalog.json")
        if catalog.resolve("长沙", "湖南省")["status"] != "found":
            raise ValueError("chinese_city_regression")
        return {"build_epoch": metadata.build_epoch, "ip_version": metadata.ip_version,
                "city_count": len(catalog.cities), "catalog_sha256": sha256(folder / "catalog.json")}
    finally:
        database.close()


def switch(root, folder):
    staged = root / "next"
    staged.symlink_to(folder.name, target_is_directory=True)
    os.replace(staged, root / "active")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.root.exists():
        parser.error("Use new private experiment and evidence paths")
    args.root.mkdir()
    expected = sha256(args.database)
    for name in ("version-a", "version-b"):
        folder = args.root / name
        folder.mkdir()
        shutil.copyfile(args.database, folder / "database.mmdb")
        shutil.copyfile(args.catalog, folder / "catalog.json")
    a, b = args.root / "version-a", args.root / "version-b"
    initial = validate(a, expected)
    switch(args.root, a)
    old = IpDatabase(args.root / "active/database.mmdb")
    lookup_before = old.lookup("111.13.100.92")
    bad = args.root / "invalid-candidate"
    bad.mkdir()
    (bad / "database.mmdb").write_bytes(b"intentionally invalid update fixture")
    shutil.copyfile(args.catalog, bad / "catalog.json")
    rejected = []
    for expected_hash in (expected, sha256(bad / "database.mmdb")):
        try:
            validate(bad, expected_hash)
        except Exception as error:
            rejected.append(type(error).__name__)
        else:
            raise AssertionError("Corrupt database accepted")
    assert (args.root / "active").resolve() == a.resolve()
    candidate = validate(b, expected)
    switch(args.root, b)
    new = IpDatabase(args.root / "active/database.mmdb")
    try:
        assert lookup_before == old.lookup("111.13.100.92") == new.lookup("111.13.100.92")
    finally:
        old.close()
        new.close()
    switch(args.root, a)
    assert (args.root / "active").resolve() == a.resolve()
    args.output.write_text(json.dumps({"at": datetime.now(timezone.utc).isoformat(), "passed": True,
                                       "same_actual_release_in_both_slots": True,
                                       "future_release_coverage_tested": False,
                                       "database_sha256": expected, "initial": initial, "candidate": candidate,
                                       "checksum_and_format_rejections": rejected,
                                       "existing_reader_remains_valid_during_switch": True,
                                       "reopened_reader_matches": True, "atomic_switch_and_rollback": True}, indent=2) + "\n")
    print("Offline real-release update mechanics passed; no future release claim")


if __name__ == "__main__":
    main()
