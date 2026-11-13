"""Stage a private OTA Compose/proxy delta; never install or restart anything.

API/web image IDs may be omitted to review configuration independently. Such a
bundle is explicitly not deployable until final tested image IDs are supplied.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import subprocess


def inspect(name):
    return json.loads(subprocess.check_output(['docker', 'inspect', name]))[0]


def environment(service):
    value = service.get('environment', {})
    if isinstance(value, list):
        value = dict(item.split('=', 1) for item in value)
    return dict(value)


def insert_once(text, marker, block):
    if text.count(marker) != 1:
        raise ValueError('Current proxy no longer matches reviewed insertion point')
    return text.replace(marker, block + '\n' + marker, 1)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--deployment', type=Path, required=True)
parser.add_argument('--public-config', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--api-image')
parser.add_argument('--web-image')
parser.add_argument('--voice-image', required=True)
args = parser.parse_args()
for image in [args.api_image, args.web_image, args.voice_image]:
    if image is not None and not re.fullmatch(r'sha256:[a-f0-9]{64}', image):
        raise ValueError('Immutable image IDs required')
private = args.deployment / 'private'
sources = {'compose.json': private/'compose.json', 'manager-nginx.conf': private/'manager-nginx.conf',
           'public-nginx.conf': args.public_config}
original = {name: path.read_bytes() for name, path in sources.items()}
compose = json.loads(original['compose.json'])
states = {role: inspect('xiaozhi-pr26-'+role) for role in ['api', 'voice', 'web']}
expected_voice = 'sha256:22c9e780db3fe00d2f7d78982b6d84cfc6f32d7f7679744f0f6d6ac193a47585'
assert states['voice']['Image'] == expected_voice, 'Live voice changed: rebase candidate first'
for image in [args.api_image, args.web_image, args.voice_image]:
    if image:
        subprocess.run(['docker', 'image', 'inspect', image], check=True, stdout=subprocess.DEVNULL)
identity = compose['networks']['identity']
assert identity.get('external') and identity['name'] == 'issue29-test'
ip = states['voice']['NetworkSettings']['Networks']['issue29-test']['IPAddress']
assert ip == '172.26.0.4', 'Private interface changed: review binding first'
voice = compose['services']['product-voice']
assert not voice.get('ports'), 'Voice host ports would expose bridge'
networks = voice['networks']
if isinstance(networks, list):
    networks = {key: {} for key in networks}
networks['identity'] = dict(networks.get('identity') or {}, ipv4_address=ip)
voice['networks'] = networks
os.umask(0o077)
args.output.mkdir(mode=0o700)
bridge_key, download_key = secrets.token_hex(32), secrets.token_hex(32)
api = compose['services']['product-api']
env = environment(api)
runtime_env = dict(item.split('=', 1) for item in states['api']['Config']['Env'])
spring = json.loads(runtime_env.get('SPRING_APPLICATION_JSON', '{}'))
spring.update({'product.ota.bridge-url':'http://product-voice:8097/internal/product-ota/v1/control',
               'product.ota.bridge-key':bridge_key, 'product.ota.download-key':download_key,
               'product.ota.download-origin':'https://bridge.hncig.cn:18443'})
env['SPRING_APPLICATION_JSON'] = json.dumps(spring, separators=(',', ':'))
api['environment'] = env
env = environment(voice)
env.update({'PRODUCT_OTA_BRIDGE_PORT':'8097','PRODUCT_OTA_BRIDGE_HOST':ip,
            'PRODUCT_OTA_BRIDGE_DIRECTORY':'/app/data/product-ota',
            'PRODUCT_OTA_BRIDGE_KEY':bridge_key,
            'PRODUCT_OTA_BRIDGE_DOWNLOAD_ORIGIN':'https://bridge.hncig.cn:18443'})
voice['environment'] = env
for role, image in [('api',args.api_image), ('voice',args.voice_image), ('web',args.web_image)]:
    if image:
        compose['services']['product-'+role]['image'] = image
public = original['public-nginx.conf'].decode()
assert 'ota-artifacts' not in public, 'Existing OTA route requires explicit reconciliation'
public = insert_once(public, '    location = /xiaozhi/v1/', '''    location ~ "^/xiaozhi/ota-artifacts/[a-f0-9]{32}/[a-f0-9]{64}\\\\.bin$" {
        limit_except GET { deny all; }
        proxy_pass http://product-api:8002;
        proxy_set_header Host $http_host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 30s;
        proxy_buffering off;
        proxy_max_temp_file_size 0;
        access_log off;
        error_log /dev/null crit;
    }
''')
manager = original['manager-nginx.conf'].decode()
assert '/xiaozhi/admin/product-ota/firmware' not in manager
location = re.search(r'location /xiaozhi/\s*\{(.*?)\n\s*\}', manager, re.S)
assert location, 'Manager proxy no longer matches reviewed structure'
proxy = re.search(r'proxy_pass\s+([^;]+);', location[1])[1]
manager_marker = re.search(r'(?m)^[ \t]*location /xiaozhi/', manager)[0]
manager = insert_once(manager, manager_marker, '''    location = /xiaozhi/admin/product-ota/firmware {
        proxy_pass ''' + proxy + ''';
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_connect_timeout 5s;
        proxy_read_timeout 300s;
    }
''')
(args.output/'compose.json').write_text(json.dumps(compose, indent=2)+'\n')
(args.output/'public-nginx.conf').write_text(public)
(args.output/'manager-nginx.conf').write_text(manager)
(args.output/'ota-bridge-key').write_text(bridge_key+'\n')
(args.output/'ota-download-key').write_text(download_key+'\n')
# Keep exact private originals for review; apply-time guard must compare hashes.
before = args.output/'before'
before.mkdir(mode=0o700)
for name, raw in original.items():
    (before/name).write_bytes(raw)
manifest = {'deployable':bool(args.api_image and args.web_image), 'production_mutations':0,
            'binding':{'network':'issue29-test','ipv4_address':ip,'port':8097,'host_port_published':False},
            'before_images':{role:state['Image'] for role,state in states.items()},
            'candidate_images':{'api':args.api_image,'voice':args.voice_image,'web':args.web_image},
            'before_sha256':{name:hashlib.sha256(raw).hexdigest() for name,raw in original.items()},
            'staged_sha256':{name:hashlib.sha256((args.output/name).read_bytes()).hexdigest() for name in original},
            'still_required':['final tested API/web images' if not args.api_image or not args.web_image else 'apply-time image/config recheck',
                              'approved deployment window','coordinated private production backup',
                              'install keys and create root-owned 0700 voice-data/product-ota',
                              'normal TLS and signed download boundary verification after deployment']}
(args.output/'manifest-public.json').write_text(json.dumps(manifest,indent=2)+'\n')
assert all(path.read_bytes() == original[name] for name,path in sources.items())
print(json.dumps(manifest,indent=2))
