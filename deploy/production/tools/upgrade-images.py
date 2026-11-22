#!/usr/bin/env python3
"""Pack application layers for the Docker Engine used by production."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile


def docker(*args):
    return subprocess.check_output(('docker', *args), text=True).strip()


def inspect_image(name):
    return json.loads(docker('image', 'inspect', name))[0]


def check(condition, message):
    if not condition:
        raise ValueError(message)


def checksum(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def add_bytes(archive, name, content):
    entry = tarfile.TarInfo(name)
    entry.size = len(content)
    entry.mode = 0o644
    archive.addfile(entry, io.BytesIO(content))


def pack(args):
    check(re.fullmatch(r'[0-9a-f]{40}', args.revision), 'Use the full target Git SHA')
    bases = {}
    for spec in args.base:
        service, separator, base_name = spec.partition('=')
        check(separator and service in ('api', 'web', 'voice') and service not in bases,
              f'Expected a distinct api, web or voice BASE entry, got {spec!r}')
        check(re.fullmatch(rf'xiaozhi-release-{service}:[0-9a-f]{{12}}', base_name),
              f'Invalid baseline tag for {service}')
        bases[service] = base_name

    metadata = {'revision': args.revision, 'base_images': {}, 'images': {}}
    records = []
    args.output.mkdir(mode=0o700)
    package = args.output / 'services.tar'
    with tarfile.open(args.archive) as source, tarfile.open(package, 'w') as target:
        candidates = json.load(source.extractfile('manifest.json'))
        for service, base_name in bases.items():
            base = inspect_image(base_name)
            base_layers = base['RootFS']['Layers']
            base_revision = base['Config']['Labels']['org.opencontainers.image.revision']
            check(re.fullmatch(r'[0-9a-f]{40}', base_revision)
                  and base_name.endswith(':' + base_revision[:12]),
                  f'Baseline revision mismatch for {service}')
            image_name = f'xiaozhi-release-{service}:{args.revision[:12]}'
            matches = [entry for entry in candidates if image_name in (entry.get('RepoTags') or [])]
            check(len(matches) == 1, f'Expected one archive entry for {image_name}')
            candidate = matches[0]
            config_bytes = source.extractfile(candidate['Config']).read()
            config = json.loads(config_bytes)
            layers = config['rootfs']['diff_ids']
            check(layers[:len(base_layers)] == base_layers and len(layers) > len(base_layers),
                  f'{service} must contain the complete baseline followed by new layers')
            check(len(candidate['Layers']) == len(layers)
                  and all(re.fullmatch(r'sha256:[0-9a-f]{64}', layer) for layer in layers),
                  f'Invalid layer list for {service}')
            check(config['os'] == base['Os'] and config['architecture'] == base['Architecture'],
                  f'Platform changed for {service}')
            check(config['config']['Labels']['org.opencontainers.image.revision'] == args.revision,
                  f'Target revision mismatch for {service}')

            digest = hashlib.sha256(config_bytes).hexdigest()
            config_name = digest + '.json'
            add_bytes(target, config_name, config_bytes)
            archive_names = [f'layer-{layer[7:]}.tar' for layer in layers]
            for index in range(len(base_layers), len(layers)):
                layer = source.getmember(candidate['Layers'][index])
                entry = tarfile.TarInfo(archive_names[index])
                entry.size = layer.size
                entry.mode = 0o644
                target.addfile(entry, source.extractfile(layer))

            records.append({'Config': config_name, 'RepoTags': [image_name], 'Layers': archive_names})
            metadata['base_images'][service] = {'image': base_name, 'revision': base_revision,
                                                'diff_ids': base_layers}
            metadata['images'][service] = {'image': image_name, 'revision': args.revision,
                                          'id': 'sha256:' + digest, 'diff_ids': layers,
                                          'os': config['os'], 'architecture': config['architecture']}

        add_bytes(target, 'manifest.json', json.dumps(records).encode())

    manifest = args.output / 'release-manifest.json'
    manifest.write_text(json.dumps(metadata, indent=2) + '\n')
    (args.output / 'SHA256SUMS').write_text(
        ''.join(f'{checksum(path)}  {path.name}\n' for path in (package, manifest)))


def verify(path, base):
    manifest = json.loads(path.read_text())
    check(manifest['base_images'] and manifest['base_images'].keys() == manifest['images'].keys(),
          'Baseline and target service lists differ')
    entries = manifest['base_images'] if base else manifest['images']
    for service, expected in entries.items():
        image = inspect_image(expected['image'])
        check(image['RootFS']['Layers'] == expected['diff_ids'], f'Layer mismatch for {service}')
        check(image['Config']['Labels']['org.opencontainers.image.revision'] == expected['revision'],
              f'Revision mismatch for {service}')
        target = manifest['images'][service]
        check(image['Os'] == target['os'] and image['Architecture'] == target['architecture'],
              f'Platform mismatch for {service}')
        if base:
            container_id = docker('compose', 'ps', '-q', service)
            check(container_id and '\n' not in container_id, f'Expected one running {service} container')
            container = json.loads(docker('inspect', container_id))[0]
            check(container['State']['Running'] and container['Config']['Image'] == expected['image']
                  and container['Image'] == image['Id'], f'Running image mismatch for {service}')
        else:
            check(expected['revision'] == manifest['revision'] and image['Id'] == expected['id'],
                  f'Target image ID or release revision mismatch for {service}')
        print(f'{service}: {expected["image"]} verified', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    package = actions.add_parser('pack')
    package.add_argument('--revision', required=True)
    package.add_argument('--archive', required=True, type=Path)
    package.add_argument('--output', required=True, type=Path, help='new release directory')
    package.add_argument('--base', required=True, action='append', metavar='SERVICE=IMAGE')
    for action in ('verify-base', 'verify-target'):
        actions.add_parser(action).add_argument('manifest', type=Path)
    args = parser.parse_args()
    try:
        if args.action == 'pack':
            pack(args)
        else:
            verify(args.manifest, base=args.action == 'verify-base')
    except (KeyError, OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        sys.exit(f'Upgrade check failed: {error}')


if __name__ == '__main__':
    main()
