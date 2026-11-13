"""One authorized public question through the existing client and real SDK."""
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


class SafeLogger:
    def bind(self, **kwargs):
        return self

    def __getattr__(self, name):
        return lambda *args, **kwargs: None


def load_client(root):
    # Avoid importing unrelated audio/config bootstrap; transports and MCP SDK
    # below are the real installed modules, not the offline test harness.
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
    package = types.ModuleType('issue12_live_mcp')
    package.__path__ = [str(path)]
    sys.modules['issue12_live_mcp'] = package
    spec = importlib.util.spec_from_file_location('issue12_live_mcp.mcp_client', path / 'mcp_client.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.ServerMCPClient


async def discover(root, config_path, report, output):
    config = json.loads(config_path.read_text(encoding='utf-8'))
    if config.get('url') != TARGET or set(config) != {'url', 'headers'}:
        raise ValueError('unexpected_config')
    headers = config.get('headers')
    if not isinstance(headers, dict) or set(headers) != {'Authorization'}:
        raise ValueError('unexpected_auth_fields')
    auth = headers['Authorization']
    if not isinstance(auth, str) or not auth.startswith('Bearer ') or '\n' in auth or '\r' in auth:
        raise ValueError('unexpected_auth_shape')
    config = {**config, 'timeout': 5, 'sse_read_timeout': 30}
    from mcp import ClientSession
    original_initialize = ClientSession.initialize

    async def record_initialize(session, *args, **kwargs):
        initialized = await original_initialize(session, *args, **kwargs)
        report['initialize_result'] = initialized.model_dump(mode='json', by_alias=True, exclude_none=True)
        return initialized

    ClientSession.initialize = record_initialize
    client = load_client(root)(config)
    try:
        report['initialization_attempts'] = 1
        await asyncio.wait_for(client.initialize(read_timeout_seconds=timedelta(seconds=25)), 30)
        report['connected_after_initialize'] = client.is_connected()
        tools = client.get_available_tools()
        report['tool_definitions'] = tools
        report['ask_knowledge_found'] = any(t.get('function', {}).get('name') == 'ask_knowledge' for t in tools)
        report['status'] = 'discovered' if report['ask_knowledge_found'] else 'expected_tool_missing'
        if report['ask_knowledge_found']:
            definition = next(t['function'] for t in tools if t.get('function', {}).get('name') == 'ask_knowledge')
            schema = definition['parameters']
            if schema.get('required') != ['question'] or schema.get('additionalProperties') is not False:
                raise ValueError('tool_schema_changed')
            report['tools_call_submissions'] = 1
            result = await asyncio.wait_for(client.call_tool('ask_knowledge',
                {'question': '怎么申请成立社会团体'}, read_timeout_seconds=timedelta(seconds=25)), 30)
            raw = (json.dumps(result.model_dump(mode='json', by_alias=True, exclude_none=True), ensure_ascii=False, indent=2) + '\n').encode()
            raw_path = output / 'tool-result-private.json'
            fd = os.open(str(raw_path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, 'wb') as handle:
                handle.write(raw)
            report['private_response'] = {'filename': raw_path.name, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}
            structured = getattr(result, 'structured_content', None)
            if structured is None:
                structured = getattr(result, 'structuredContent', None)
            error = getattr(result, 'is_error', getattr(result, 'isError', None))
            report['tool_result_summary'] = {
                'object_type': type(result).__name__, 'is_error': error,
                'structured_keys': sorted(structured) if isinstance(structured, dict) else None,
                'content_blocks': len(result.content) if isinstance(getattr(result, 'content', None), list) else None,
                'business_complete': None,
            }
            if isinstance(structured, dict):
                report['tool_result_summary']['observed_labels'] = {
                    key: structured.get(key) for key in ('status', 'mode', 'generated', 'modelCalled',
                        'model', 'returned', 'topK', 'cacheHit', 'elapsedMs')}
                report['tool_result_summary']['sources_count'] = len(structured.get('sources', []))
            report['status'] = 'tool_response_received' if error is False else 'tool_protocol_error'

    finally:
        try:
            await asyncio.wait_for(client.cleanup(), 22)
            report['cleanup_completed'] = True
        except BaseException as error:
            report['cleanup_completed'] = False
            report['cleanup_error_type'] = type(error).__name__
            report['status'] = 'cleanup_failed'
        report['connected_after_cleanup'] = client.is_connected()
        report['worker_reference_cleared'] = client._worker_task is None
        ClientSession.initialize = original_initialize


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(mode=0o700, exist_ok=False)
    logging.disable(logging.CRITICAL)
    from importlib import metadata
    from datetime import datetime, timezone
    report = {
        'schema': 'issue12-product-client-live-discovery-v1',
        'started_utc': datetime.now(timezone.utc).isoformat(),
        'python': sys.version.split()[0],
        'mcp_distribution': metadata.version('mcp'),
        'probe_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'scope': 'real product ServerMCPClient and installed transport/session; one public question, no model/TTS; safe result summary only',
        'tools_call_submissions': 0, 'model_requests': 0, 'tts_requests': 0,
        'retries': 0, 'status': 'starting',
    }
    manifest = json.loads((args.root / 'test-manifest.json').read_text())
    report['source_sha256'] = manifest['files']
    try:
        if any(hashlib.sha256((args.root / name).read_bytes()).hexdigest() != expected
               for name, expected in manifest['files'].items()):
            raise ValueError('source_hash_mismatch')
        asyncio.run(discover(args.root, args.config, report, args.output))
    except BaseException as error:
        report['status'] = 'failed'
        report['error_type'] = type(error).__name__
    report['ended_utc'] = datetime.now(timezone.utc).isoformat()
    # No raw exception, config, dynamic session endpoint, header or source text.
    encoded = (json.dumps(report, ensure_ascii=False, indent=2) + '\n').encode()
    fd = os.open(str(args.output / 'result.json'), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as handle:
        handle.write(encoded)
    print(json.dumps({k: report.get(k) for k in ('status', 'error_type', 'cleanup_error_type',
          'ask_knowledge_found', 'cleanup_completed', 'connected_after_cleanup', 'worker_reference_cleared')}, ensure_ascii=False))
    print('REPORT_SHA256', hashlib.sha256(encoded).hexdigest())
    return 0 if report['status'] == 'tool_response_received' and report.get('cleanup_completed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
