#!/bin/bash
# Ubuntu only. Builds/tests an isolated overlay; never changes production services.
set -euo pipefail
umask 077
source_root=${1:?source checkout path required}
output=${2:?new private output directory required}
baseline=sha256:22c9e780db3fe00d2f7d78982b6d84cfc6f32d7f7679744f0f6d6ac193a47585
test "$(docker inspect --format '{{.Image}}' xiaozhi-pr26-voice)" = "$baseline"
# This archived candidate is the 2249e3c overlay; later code needs a new identity.
python3 - "$source_root/main/xiaozhi-server/core/product_ota_bridge.py" <<'PY'
import hashlib, pathlib, sys
raw=pathlib.Path(sys.argv[1]).read_bytes().replace(b'\r\n', b'\n')
assert hashlib.sha256(raw).hexdigest() == '2de71e0e57a87a6111b66ac7666f1e4cff660e5791dd398a8a5cab4757baf39b'
PY
mkdir "$output"
mkdir -p "$output/build/main/xiaozhi-server/core" "$output/results"
cp -a /home/jt/issue14-wake-repro-20261113/candidate/checks "$output/checks"
cp "$source_root/main/xiaozhi-server/tests/test_product_ota_bridge.py" "$output/checks/"
docker cp xiaozhi-pr26-voice:/app/core/websocket_server.py "$output/build/main/xiaozhi-server/core/websocket_server.py"
cd "$output/build"
echo 'a1dc1452f70694ad84cbb1f3cf073cfe46038950c45ccb295f5f5e49dd6bb047  main/xiaozhi-server/core/websocket_server.py' | sha256sum -c -
git apply --unidiff-zero --check "$source_root/scripts/ota99/websocket-lifecycle.patch"
git apply --unidiff-zero "$source_root/scripts/ota99/websocket-lifecycle.patch"
cp "$source_root/main/xiaozhi-server/core/product_ota_bridge.py" main/xiaozhi-server/core/
printf 'FROM %s\nCOPY main/xiaozhi-server/core/ /app/core/\n' "$baseline" > Dockerfile
docker build --network none -t xiaozhi-product-voice:ota99-2249e3c . > "$output/build.log" 2>&1
image=$(docker image inspect --format '{{.Id}}' xiaozhi-product-voice:ota99-2249e3c)
printf '%s\n' "$image" > "$output/image.txt"
common=(--rm --network none --read-only --cap-drop ALL --security-opt no-new-privileges --tmpfs /tmp:rw,nosuid,size=256m --tmpfs /app/data:rw,nosuid,size=64m --tmpfs /app/tmp:rw,nosuid,size=64m --tmpfs /app/config/assets:rw,nosuid,size=16m --mount "type=bind,src=$output/checks,dst=/checks,readonly" --entrypoint python)
timeout 120s docker run "${common[@]}" --mount "type=bind,src=$output/results,dst=/results" "$image" -I -B /checks/candidate-units.py > "$output/results/units.log" 2>&1
for scenario in wake-start listen-start abort-new-turn wake-start-twice wake-abort-start happy ordinary other-owner tool-error revoked empty-answer init-failed device-tools; do
  timeout 80s docker run "${common[@]}" "$image" -I -B /checks/check-product-knowledge.py "$scenario" > "$output/results/$scenario.log" 2>&1
done
python3 - "$output" "$baseline" <<'PY'
import datetime, hashlib, json, pathlib, sys
root=pathlib.Path(sys.argv[1]); results=root/'results'
cases=[json.loads(line) for p in sorted(results.glob('*.log')) for line in p.read_text().splitlines() if line.startswith('{"result":')]
units=json.loads((results/'units.json').read_text())
assert len(cases)==13 and all(c['result']=='pass' for c in cases)
assert units['success'] and units['tests']>66 and not units['skipped']
summary={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'base_image':sys.argv[2], 'candidate_image':(root/'image.txt').read_text().strip(),'ota_source':'2249e3c','units':units,'integration_cases':cases,'production_mutations':0,'physical_device_calls':0,'network':'none','source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'build/main/xiaozhi-server/core').glob('*.py')},'evidence_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in results.iterdir()}}
(root/'summary-public.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
PY
chown -R jt:jt "$output"
