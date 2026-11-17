#!/usr/bin/env python3
"""A bounded, device-authenticated development simulator. Never calls voice/model APIs."""
import argparse
import json
import os
from pathlib import Path
import time

import requests

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--identity-file', required=True, type=Path, help='Private file containing device_id and credential from administrator registration')
parser.add_argument('--begin', action='store_true', help='Explicitly start/reuse one login transaction')
parser.add_argument('--login-id', default='', help='Observe an already started transaction')
parser.add_argument('--duration', type=int, default=90, help='Seconds, at most 600')
args = parser.parse_args()
assert os.environ.get('SPRING_DATASOURCE_DRUID_URL', '').startswith('jdbc:mysql://172.16.18.104:13306/xiaozhi_dev?'), 'Run inside the isolated development container'
assert 1 <= args.duration <= 600
assert not args.identity_file.is_symlink() and args.identity_file.stat().st_mode & 0o077 == 0, 'Identity file must be private'
identity = json.loads(args.identity_file.read_text())
headers = {'Authorization': 'Bearer ' + identity['credential'], 'Device-Id': identity['device_id']}
base = 'http://127.0.0.1:8002/xiaozhi/speaker/v1/'


def call(route, body=None):
    response = requests.request('GET' if body is None else 'POST', base + route, json=body, headers=headers, timeout=10)
    if response.status_code != 200:
        raise RuntimeError('Device request failed: HTTP ' + str(response.status_code))
    return response.json()


transaction = args.login_id
if args.begin:
    login = call('login', {})
    transaction = login.get('id', '')
    print(json.dumps({key: login.get(key) for key in ('id', 'status', 'verification_uri', 'user_code', 'expires_at_ms', 'error_code')}, ensure_ascii=False), flush=True)
deadline, heartbeat, previous = time.monotonic() + args.duration, 0, None
while time.monotonic() < deadline:
    if time.monotonic() >= heartbeat:
        call('heartbeat', {'board_type': 'protocol-simulator', 'firmware_version': 'phase1-dev-simulator'})
        heartbeat = time.monotonic() + 20
    state = call('state')
    report = {key: state.get(key) for key in ('protocol_version', 'binding_state', 'use_status', 'permission_version')}
    if transaction:
        login = call('login/' + transaction)
        report['login_status'] = login.get('status')
        report['login_error'] = login.get('error_code')
    if report != previous:
        print(json.dumps(report, ensure_ascii=False), flush=True)
        previous = report
    time.sleep(min(5, max(0, deadline - time.monotonic())))
print('Simulator stopped; ownership and authorization remain persisted.', flush=True)
