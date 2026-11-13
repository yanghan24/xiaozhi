"""Ubuntu-only synthetic restore drill. No production writes or device requests."""
import hashlib
import json
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess
import sys
import tempfile
import time

os.umask(0o077)
scripts = Path(__file__).resolve().parent
container = 'ota99-backup-drill-' + secrets.token_hex(4)
with tempfile.TemporaryDirectory(prefix='ota99-backup-drill-') as temporary:
    root = Path(temporary)
    environment = root / 'mysql.env'
    environment.write_text('MYSQL_ROOT_PASSWORD=' + secrets.token_hex(24) + '\nMYSQL_DATABASE=fixture\n')
    subprocess.run(['docker', 'run', '-d', '--name', container, '--network', 'none',
                    '--tmpfs', '/var/lib/mysql:rw,size=512m', '--env-file', str(environment),
                    'xiaozhi-offline-mysql:8.0.41-20261106'], check=True, stdout=subprocess.DEVNULL)
    def sql(query, database='fixture'):
        return subprocess.run(['docker', 'exec', '-i', container, 'sh', '-ec',
                               'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -N ' + database],
                              input=query, text=True, capture_output=True, check=True).stdout.strip()
    try:
        for attempt in range(60):
            try:
                sql('SELECT 1;')
                break
            except subprocess.CalledProcessError:
                time.sleep(1)
        else:
            raise RuntimeError('Isolated MySQL did not become ready')
        binary = bytes(range(256)) * 8192
        sql('CREATE TABLE dc_ota_firmware (id INT PRIMARY KEY,image LONGBLOB); '
            'INSERT INTO dc_ota_firmware VALUES(1,0x' + binary.hex() + '); '
            'CREATE TABLE dc_ota_operation (id INT PRIMARY KEY,status VARCHAR(40)); '
            "INSERT INTO dc_ota_operation VALUES(1,'result_unknown'); "
            'CREATE TABLE dc_ota_batch (id INT PRIMARY KEY,status VARCHAR(40)); '
            "INSERT INTO dc_ota_batch VALUES(1,'interrupted'); "
            'CREATE TABLE conversation_text (secret TEXT); '
            "INSERT INTO conversation_text VALUES('excluded fixture');")
        deployment = root / 'deployment'
        private = deployment / 'private'
        for name in ['api.env', 'oidc-client.json', 'identity.key', 'compose.json', 'voice-key',
                     'manager-nginx.conf', 'voice-data/.config.yaml', 'device-delivery/encryption-key']:
            path = private / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('synthetic fixture\n')
        proxy = root / 'public-nginx.conf'
        proxy.write_text('# synthetic fixture\n')
        journal = private / 'voice-data/product-ota/operations.sqlite3'
        journal.parent.mkdir(mode=0o700)
        with sqlite3.connect(journal) as writer:
            writer.execute('PRAGMA journal_mode=WAL')
            writer.execute('CREATE TABLE operations(id TEXT PRIMARY KEY,result TEXT,active INTEGER)')
            writer.execute("INSERT INTO operations VALUES('fixed-id','result_unknown',1)")
            writer.commit()
            output = root / 'backup'
            subprocess.run([sys.executable, str(scripts / 'backup.py'), '--deployment', str(deployment),
                            '--output', str(output), '--mysql-container', container,
                            '--public-proxy-config', str(proxy)], check=True)
        sql('CREATE DATABASE restored;')
        sql((output / 'scoped.sql').read_text(), 'restored')
        assert sql('SELECT SHA2(image,256) FROM dc_ota_firmware;', 'restored') == hashlib.sha256(binary).hexdigest()
        assert sql('SELECT status FROM dc_ota_operation;', 'restored') == 'result_unknown'
        assert sql('SELECT status FROM dc_ota_batch;', 'restored') == 'interrupted'
        assert 'conversation_text' not in sql('SHOW TABLES;', 'restored')
        with sqlite3.connect(output / 'private/voice-data/product-ota/operations.sqlite3') as db:
            assert db.execute('SELECT * FROM operations').fetchall() == [('fixed-id', 'result_unknown', 1)]
        rollback = root / 'rollback.sh'
        subprocess.run([sys.executable, str(scripts / 'prepare-rollback.py'), '--backup', str(output),
                        '--deployment', str(deployment), '--output', str(rollback),
                        '--candidate-api', 'sha256:' + '1'*64, '--candidate-voice', 'sha256:' + '2'*64,
                        '--candidate-web', 'sha256:' + '3'*64], check=True)
        subprocess.run(['bash', '-n', str(rollback)], check=True)
        # Default invocation stops at the first guard, before Docker or file writes.
        assert subprocess.run(['bash', str(rollback)], capture_output=True).returncode != 0
        print(json.dumps({'result':'pass','firmware_blob_bytes':len(binary),'firmware_sha256':hashlib.sha256(binary).hexdigest(),
                          'unknown_operation_retained':True,'batch_retained':True,'wal_active_lock_retained':True,
                          'conversation_excluded':True,'rollback_syntax':True,'rollback_default_denied':True,
                          'production_mutations':0,'physical_device_calls':0}))
    finally:
        subprocess.run(['docker', 'rm', '-f', container], check=True, stdout=subprocess.DEVNULL)
