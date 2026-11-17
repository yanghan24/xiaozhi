"""Ubuntu private backup. Run only in an approved deployment window, as root.

Online SQL/SQLite snapshots are individually consistent. Stop new OTA publishing
and wait for active dispatch workers before taking a coordinated release backup.
Never restore this snapshot over newer OTA reservations to permit retry.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import subprocess


def capture(command):
    return subprocess.check_output(command, text=True)


def sqlite_backup(source, destination):
    # WAL-aware, read-only source; no checkpoint or journal deletion.
    with sqlite3.connect(source.resolve().as_uri() + '?mode=ro', uri=True) as db:
        with sqlite3.connect(destination) as target:
            db.backup(target)
            if target.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
                raise RuntimeError('SQLite backup integrity failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deployment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mysql-container', default='issue29-db')
    parser.add_argument('--public-proxy-config', type=Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    args.output.mkdir(mode=0o700)  # Never overwrite a prior backup.
    private = args.deployment / 'private'
    base = ['docker', 'exec', args.mysql_container, 'sh', '-ec']
    tables = capture(base + ['MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -N "$MYSQL_DATABASE" -e "SHOW TABLES"']).splitlines()
    keep = [t for t in tables if t.startswith(('customer_', 'dc_', 'sys_', 'ai_model_')) or t in
            ('DATABASECHANGELOG', 'DATABASECHANGELOGLOCK', 'ai_device', 'ai_tts_voice', 'product_asr_settings')]
    if not keep or any(not re.fullmatch(r'[A-Za-z0-9_]+', t) for t in keep):
        raise RuntimeError('Unexpected backup table selection')
    # One transaction includes schema + data, including all dc_ota_* and binary image bytes.
    dump = 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysqldump -uroot --single-transaction --hex-blob --no-tablespaces --set-gtid-purged=OFF "$MYSQL_DATABASE" '
    with (args.output / 'scoped.sql').open('wb') as out:
        subprocess.run(base + [dump + ' '.join(keep)], stdout=out, check=True)
    copied = args.output / 'private'
    copied.mkdir(mode=0o700)
    required = ['api.env', 'oidc-client.json', 'identity.key', 'compose.json',
                'voice-key', 'manager-nginx.conf', 'voice-data/.config.yaml',
                'device-delivery/encryption-key']
    for name in required:
        source = private / name
        target = copied / name
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        shutil.copy2(source, target)
    for name in ['voice-data/.wakeup_words.yaml', 'ota-bridge-key', 'ota-download-key']:
        if (private / name).exists():
            shutil.copy2(private / name, copied / name)
    shutil.copy2(args.public_proxy_config, copied / 'public-nginx.conf')
    for directory in ['product-voice', 'product-ota', 'product-maintenance']:
        source = private / 'voice-data' / directory
        destination = copied / 'voice-data' / directory
        destination.mkdir(parents=True, mode=0o700)
        if not source.exists():
            continue
        for entry in source.iterdir():
            if entry.is_symlink():
                raise RuntimeError('Unexpected journal symlink')
            if entry.suffix == '.sqlite3':
                sqlite_backup(entry, destination / entry.name)
            elif entry.is_file() and entry.suffix == '.json':
                # Maintenance reservations are never removed during service rollback.
                shutil.copy2(entry, destination / entry.name)
    containers = ['xiaozhi-pr26-api', 'xiaozhi-pr26-voice', 'xiaozhi-pr26-web']
    # Full inspect contains credentials; only persist the safe restoration fields.
    states = json.loads(capture(['docker', 'inspect', *containers]))
    images = {s['Name'].lstrip('/'): {'image': s['Image'], 'started_at': s['State']['StartedAt']} for s in states}
    manifest = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'tables': keep, 'images': images, 'mysql_hex_blob': True,
                'conversation_content_included': False, 'audio_included': False,
                'sha256': {str(p.relative_to(args.output)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in args.output.rglob('*') if p.is_file()}}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Private scoped backup complete; binary firmware and WAL-aware journals included.')


if __name__ == '__main__':
    main()
