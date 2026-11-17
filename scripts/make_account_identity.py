#!/usr/bin/env python3
"""Prepare a private factory NVS image, never flash it or erase a device.

For an already provisioned speaker, OTA preserves NVS: do not write this image.
Use inside xiaozhi-dev after registering the device in the administration UI.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--identity', required=True, type=Path, help='Private JSON file; never pass credentials on the command line')
parser.add_argument('--output-dir', required=True, type=Path, help='New private directory under /opt/xiaozhi-dev')
args = parser.parse_args()
assert Path('/workspace/.devcontainer/supervisord.conf').exists(), 'Run in xiaozhi-dev'
assert args.output_dir.resolve().is_relative_to('/opt/xiaozhi-dev'), 'Use the runtime volume'
assert args.identity.stat().st_mode & 0o077 == 0, 'Private input must have mode 0600'
identity = json.loads(args.identity.read_text())
assert set(identity) == {'version', 'hardware_id', 'device_id', 'credential', 'ota_url', 'control_origin'}, 'Invalid identity fields'
assert identity['version'] == 1 and re.fullmatch('[0-9a-f]{12}', identity['hardware_id']), 'Invalid hardware ID'
assert re.fullmatch('[A-Za-z0-9_.:-]{1,128}', identity['device_id']), 'Invalid device ID'
assert re.fullmatch('[A-Za-z0-9_-]{43,128}', identity['credential']), 'Invalid device credential'
origin = urlparse(identity['control_origin'])
assert origin.scheme == 'https' and origin.hostname and not origin.username and not origin.password and not origin.path and not origin.query and not origin.fragment, 'HTTPS origin required'
assert identity['ota_url'] == identity['control_origin'] + '/xiaozhi/ota/', 'OTA must use the same origin'
os.umask(0o077)
args.output_dir.mkdir(mode=0o700, parents=True, exist_ok=False)
with tempfile.TemporaryDirectory(dir=args.output_dir) as temporary:
    private = Path(temporary) / 'identity.json'
    private.write_text(json.dumps(identity, separators=(',', ':')))
    table = Path(temporary) / 'nvs.csv'
    with table.open('w') as stream:
        writer = csv.writer(stream)
        writer.writerows([['key', 'type', 'encoding', 'value'], ['device_ctrl', 'namespace', '', ''],
                         ['device_id', 'data', 'string', identity['device_id']], ['identity_v1', 'file', 'binary', str(private)]])
    sdk = Path('/opt/xiaozhi-dev/tools/idf-v5.5.5/esp-idf-v5.5.5')
    subprocess.run(['/opt/xiaozhi-dev/tools/idf-python/bin/python', str(sdk / 'components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py'),
                    'generate', str(table), str(args.output_dir / 'factory-nvs.bin'), '0x4000'],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print('Private factory NVS image prepared. No serial access or flash operation was performed.')
