"""Write a guarded image-only rollback from a private backup, without running it.

Only valid BEFORE any OTA dispatch. After dispatch, keep the compatible API,
download route/key, journals and locks until each in-flight result is reconciled.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backup', type=Path, required=True)
parser.add_argument('--deployment', type=Path, required=True)
parser.add_argument('--candidate-api', required=True)
parser.add_argument('--candidate-voice', required=True)
parser.add_argument('--candidate-web', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
manifest = json.loads((args.backup / 'manifest.json').read_text())
previous = [manifest['images'][name]['image'] for name in
            ['xiaozhi-pr26-api', 'xiaozhi-pr26-voice', 'xiaozhi-pr26-web']]
expected = [args.candidate_api, args.candidate_voice, args.candidate_web]
if not all(re.fullmatch(r'sha256:[a-f0-9]{64}', value) for value in previous + expected):
    raise ValueError('Exact image IDs required')
os.umask(0o077)
compose = str(args.deployment / 'private/compose.json')
# Removing ingress requires reviewing its current exact delta; this script refuses
# to roll back before an operator records that step and publishing is disabled.
script = '''#!/bin/bash
set -euo pipefail
umask 077
test "${OTA_PUBLISHING_DISABLED:-}" = yes
test "${OTA_ARTIFACT_ROUTE_REMOVED_AND_NGINX_VALIDATED:-}" = yes
'''
for container, image in zip(['xiaozhi-pr26-api', 'xiaozhi-pr26-voice', 'xiaozhi-pr26-web'], expected):
    script += f'test "$(docker inspect --format \'{{{{.Image}}}}\' {container})" = {shlex.quote(image)}\n'
script += '''# Fail closed if any reservation exists. Keep DB and journals unchanged.
count=$(docker exec issue29-db sh -ec 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot -N "$MYSQL_DATABASE" -e "SELECT COUNT(*) FROM dc_ota_operation"')
test "$count" = 0
python3 - ''' + shlex.quote(compose) + ' ' + ' '.join(map(shlex.quote, previous)) + ''' <<'PY'
import json, os, pathlib, sys, tempfile
path=pathlib.Path(sys.argv[1]); data=json.loads(path.read_text())
for name, image in zip(['product-api','product-voice','product-web'],sys.argv[2:]):
    data['services'][name]['image']=image
fd, temporary=tempfile.mkstemp(dir=path.parent,prefix='.rollback-',text=True)
with os.fdopen(fd,'w') as stream:
    json.dump(data,stream,indent=2); stream.flush(); os.fsync(stream.fileno())
os.replace(temporary,path)
PY
docker compose -f ''' + shlex.quote(compose) + ''' up -d --no-deps product-api product-voice product-web
# Re-resolve backend addresses after recreation; preserve current ingress config.
docker exec xiaozhi-ubuntu-test-public-entry-public-entry-1 nginx -t
docker exec xiaozhi-ubuntu-test-public-entry-public-entry-1 nginx -s reload
echo 'Image rollback completed; verify normal TLS discovery and authenticated voice separately.'
'''
with args.output.open('x') as stream:
    stream.write(script)
args.output.chmod(0o700)
print('Guarded rollback script prepared; no service changed.')
