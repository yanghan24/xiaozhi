"""A2 probe: real no-hit and invalid-parameter behaviour, no model/TTS, no retry."""
import argparse
import ast
import asyncio
from datetime import timedelta
import hashlib
import importlib.util
import json
import logging
import os
from pathlib import Path
import re
import sys
import types

TARGET = 'https://aimarket.hncig.cn:4191/mcp/service-669a396c73/sse'

# Existing public sample (already used) plus cases required by A2. Each runs at most once.
CASES = [
    ('no_hit', '今天火星基地的食堂几点开饭'),
    ('too_long', '问' * 301),
    ('empty', ''),
]


class SafeLogger:
    def bind(self, **kwargs):
        return self

    def __getattr__(self, name):
        return lambda *args, **kwargs: None


def load_client(root):
    config = types.ModuleType('config')
    config.__path__ = []
    sys.modules['config'] = config
    logger = types.ModuleType('config.logger')
    logger.setup_logging = SafeLogger
    sys.modules['config.logger'] = logger
    for name in ('core', 'core.utils'):
        package = types.ModuleType(name)
        package.__path__ = []
        sys.modules[name] = package
    util_path = root / 'main/xiaozhi-server/core/utils/util.py'
    tree = ast.parse(util_path.read_text(encoding='utf-8'))
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'sanitize_tool_name')
    namespace = {'re': re}
    exec(compile(ast.Module(body=[function], type_ignores=[]), str(util_path), 'exec'), namespace)
    util = types.ModuleType('core.utils.util')
    util.sanitize_tool_name = namespace['sanitize_tool_name']
    sys.modules['core.utils.util'] = util
    path = root / 'main/xiaozhi-server/core/providers/tools/server_mcp'
    package = types.ModuleType('issue12_a2_mcp')
    package.__path__ = [str(path)]
    sys.modules['issue12_a2_mcp'] = package
    spec = importlib.util.spec_from_file_location('issue12_a2_mcp.mcp_client', path / 'mcp_client.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.ServerMCPClient


def describe(result):
    """Structural description only: no answer text, no resource identifiers."""
    if result is None:
        return {'kind': 'none'}
    info = {'kind': type(result).__name__}
    missing = object()
    for name in ('is_error', 'isError'):
        value = getattr(result, name, missing)
        if value is not missing:
            info['is_error_field'] = name
            info['is_error'] = value if isinstance(value, bool) else type(value).__name__
            break
    content = getattr(result, 'content', None)
    info['content_is_list'] = isinstance(content, list)
    if isinstance(content, list):
        info['content_blocks'] = len(content)
        info['content_types'] = [getattr(b, 'type', None) if not isinstance(b, dict) else b.get('type')
                                 for b in content[:4]]
        info['text_chars'] = [len(getattr(b, 'text', '') or '') if not isinstance(b, dict)
                              else len(b.get('text', '') or '') for b in content[:4]]
    structured = getattr(result, 'structured_content', None)
    if structured is None:
        structured = getattr(result, 'structuredContent', None)
    info['has_structured'] = isinstance(structured, dict)
    if isinstance(structured, dict):
        info['structured_keys'] = sorted(structured)
        info['observed'] = {k: structured.get(k) for k in
                            ('status', 'mode', 'generated', 'modelCalled', 'model',
                             'returned', 'topK', 'cacheHit', 'elapsedMs', 'note') if k in structured}
        sources = structured.get('sources')
        info['sources_count'] = len(sources) if isinstance(sources, list) else None
        answer = structured.get('answer')
        info['answer_chars'] = len(answer) if isinstance(answer, str) else None
        chunks = structured.get('chunks')
        info['chunks_count'] = len(chunks) if isinstance(chunks, list) else None
    return info


async def run_case(client, name, question, report):
    entry = {'case': name, 'question_chars': len(question), 'calls': 1}
    try:
        result = await asyncio.wait_for(
            client.call_tool('ask_knowledge', {'question': question},
                             read_timeout_seconds=timedelta(seconds=25)), 30)
        entry['outcome'] = 'returned'
        entry['result'] = describe(result)
    except asyncio.CancelledError:
        raise
    except BaseException as error:
        entry['outcome'] = 'raised'
        entry['error_type'] = type(error).__name__
        text = str(error)
        # Keep only a bounded, clearly non-secret shape marker.
        entry['error_chars'] = len(text)
        entry['error_mentions_validation'] = any(
            word in text.lower() for word in ('invalid', 'validation', 'minimum', 'maximum',
                                              'too long', 'required', 'schema', '-32602'))
    report['cases'].append(entry)


async def main_async(root, config_path, report):
    config = json.loads(config_path.read_text(encoding='utf-8'))
    if config.get('url') != TARGET or set(config) != {'url', 'headers'}:
        raise ValueError('unexpected_config')
    headers = config.get('headers')
    if not isinstance(headers, dict) or set(headers) != {'Authorization'}:
        raise ValueError('unexpected_auth_fields')
    auth = headers['Authorization']
    if not isinstance(auth, str) or not auth.startswith('Bearer ') or '\n' in auth or '\r' in auth:
        raise ValueError('unexpected_auth_shape')
    client = load_client(root)({**config, 'timeout': 5, 'sse_read_timeout': 30})
    try:
        report['initialization_attempts'] = 1
        await asyncio.wait_for(client.initialize(read_timeout_seconds=timedelta(seconds=25)), 30)
        report['ask_knowledge_found'] = any(
            t.get('function', {}).get('name') == 'ask_knowledge' for t in client.get_available_tools())
        if not report['ask_knowledge_found']:
            report['status'] = 'expected_tool_missing'
            return
        for name, question in CASES:
            await run_case(client, name, question, report)
        report['status'] = 'cases_completed'
    finally:
        try:
            await asyncio.wait_for(client.cleanup(), 22)
            report['cleanup_completed'] = True
        except BaseException as error:
            report['cleanup_completed'] = False
            report['cleanup_error_type'] = type(error).__name__
        report['connected_after_cleanup'] = client.is_connected()
        report['worker_reference_cleared'] = client._worker_task is None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(mode=0o700, exist_ok=False)
    logging.disable(logging.CRITICAL)
    from datetime import datetime, timezone
    report = {
        'schema': 'issue12-a2-behaviour-probe-v1',
        'started_utc': datetime.now(timezone.utc).isoformat(),
        'python': sys.version.split()[0],
        'probe_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'scope': 'real product ServerMCPClient + installed SDK; A2 no-hit and invalid-argument shapes only; '
                 'no model, no TTS, no retry, each case submitted at most once',
        'tools_call_submissions': 0,
        'model_requests': 0,
        'tts_requests': 0,
        'retries': 0,
        'cases': [],
        'status': 'starting',
    }
    manifest = json.loads((args.root / 'test-manifest.json').read_text())
    report['source_sha256'] = manifest['files']
    try:
        if any(hashlib.sha256((args.root / name).read_bytes()).hexdigest() != expected
               for name, expected in manifest['files'].items()):
            raise ValueError('source_hash_mismatch')
        report['tools_call_submissions'] = len(CASES)
        asyncio.run(main_async(args.root, args.config, report))
    except BaseException as error:
        report['status'] = 'failed'
        report['error_type'] = type(error).__name__
    report['ended_utc'] = datetime.now(timezone.utc).isoformat()
    encoded = (json.dumps(report, ensure_ascii=False, indent=2) + '\n').encode()
    fd = os.open(str(args.output / 'result.json'), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as handle:
        handle.write(encoded)
    summary = {'status': report.get('status'), 'error_type': report.get('error_type'),
               'cleanup_completed': report.get('cleanup_completed'),
               'ask_knowledge_found': report.get('ask_knowledge_found')}
    print(json.dumps(summary, ensure_ascii=False))
    for entry in report['cases']:
        print(json.dumps(entry, ensure_ascii=False))
    print('REPORT_SHA256', hashlib.sha256(encoded).hexdigest())
    return 0 if report['status'] == 'cases_completed' and report.get('cleanup_completed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
