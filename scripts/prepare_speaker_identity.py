#!/usr/bin/env python3
"""Generate the private NVS partition image for a registered speaker."""

import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory
from urllib.parse import urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('identity', type=Path, help='Private identity JSON, readable only by its owner')
    parser.add_argument('output', type=Path, help='New image path outside the source checkout')
    args = parser.parse_args()
    if args.identity.stat().st_mode & 0o077:
        parser.error('restrict the identity file to its owner')
    for path in (args.identity, args.output):
        if path.resolve().is_relative_to(Path(__file__).resolve().parents[1]):
            parser.error('keep the identity and output outside the source checkout')
    if args.output.exists():
        parser.error('choose a new output file')
    data = json.loads(args.identity.read_text(encoding='utf-8'))
    fields = {'version', 'hardware_id', 'device_id', 'credential', 'control_origin', 'ota_url'}
    if set(data) != fields or data['version'] != 1 or any(
        not isinstance(data[key], str) for key in fields - {'version'}
    ):
        parser.error('invalid device identity')
    origin = urlsplit(data['control_origin'])
    if (origin.scheme != 'https' or not origin.hostname or origin.username is not None
            or (origin.path or origin.query or origin.fragment)
            or data['ota_url'] != data['control_origin'] + '/xiaozhi/ota/'):
        parser.error('invalid management API origin')
    patterns = {'hardware_id': '[0-9a-f]{12}', 'device_id': '[A-Za-z0-9_.:-]{1,128}',
                'credential': '[A-Za-z0-9_-]{43,128}'}
    if any(not re.fullmatch(pattern, data[key]) for key, pattern in patterns.items()):
        parser.error('invalid hardware ID, device ID, or credential')
    os.umask(0o077)
    with TemporaryDirectory(dir=args.output.parent) as tmp:
        path = Path(tmp)
        record = path / 'identity.json'
        record.write_text(json.dumps(data, separators=(',', ':')), encoding='utf-8')
        table = path / 'partition.csv'
        with table.open('w', encoding='utf-8', newline='') as stream:
            csv.writer(stream).writerows([
                ['key', 'type', 'encoding', 'value'],
                ['device_ctrl', 'namespace', '', ''],
                ['device_id', 'data', 'string', data['device_id']],
                ['identity_v1', 'file', 'binary', str(record)],
            ])
        subprocess.run([sys.executable, '-m', 'esp_idf_nvs_partition_gen', 'generate',
                        str(table), str(args.output.resolve()), '0x4000'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print('Private provisioning image generated.')


if __name__ == '__main__':
    main()
