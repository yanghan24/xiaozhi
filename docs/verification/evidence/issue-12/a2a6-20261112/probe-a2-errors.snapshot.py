"""Record exact MCP error code/message for A2 invalid-argument cases (no retry)."""
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
CASES = [('too_long', '问' * 301), ('empty', '')]


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
    package = types.ModuleType('issue12_a2b_mcp')
    package.__path__ = [str(path)]
    sys.modules['issue12_a2b_mcp'] = package
    spec = importlib.util.spec_from_file_location('issue12_a2b_mcp.mcp_client', path / 'mcp_client.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module.ServerMCPClient


async def main_async(root, config_path, report):
    config = json.loads(config_path.read_text(encoding='utf-8'))
    if config.get('url') != TARGET or set(config) != {'url', 'headers'}:
        raise ValueError('unexpected_config')
    client = load_client(root)({**config, 'timeout': 5, 'sse_read_timeout': 30})
    try:
        await asyncio.wait_for(client.initialize(read_timeout_seconds=timedelta(seconds=25)), 30)
        for name, question in CASES:
            entry = {'case': name, 'question_chars': len(question), 'calls': 1}
            try:
                await asyncio.wait_for(
                    client.call_tool('ask_knowledge', {'question': question},
                                     read_timeout_seconds=timedelta(seconds=25)), 30)
                entry['outcome'] = 'returned'
            except asyncio.CancelledError:
                raise
            except BaseException as error:
                entry['outcome'] = 'raised'
                entry['error_type'] = type(error).__name__
                inner = getattr(error, 'error', None)
                if inner is not None:
                    entry['code'] = getattr(inner, 'code', None)
                    message = getattr(inner, 'message', None)
                    entry['message'] = message if isinstance(message, str) else None
                else:
                    entry['code'] = getattr(error, 'code', None)
                    entry['message'] = None
            report['cases'].append(entry)
        report['status'] = 'completed'
    finally:
        try:
            await asyncio.wait_for(client.cleanup(), 22)
            report['cleanup_completed'] = True
        except BaseException as error:
            report['cleanup_completed'] = False
            report['cleanup_error_type'] = type(error).__name__
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
        'schema': 'issue12-a2-error-shape-v1',
        'started_utc': datetime.now(timezone.utc).isoformat(),
        'probe_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'scope': 'exact MCP error code/message for invalid ask_knowledge arguments; no model/TTS, no retry',
        'tools_call_submissions': len(CASES),
        'cases': [],
        'status': 'starting',
    }
    try:
        asyncio.run(main_async(args.root, args.config, report))
    except BaseException as error:
        report['status'] = 'failed'
        report['error_type'] = type(error).__name__
    report['ended_utc'] = datetime.now(timezone.utc).isoformat()
    encoded = (json.dumps(report, ensure_ascii=False, indent=2) + '\n').encode()
    fd = os.open(str(args.output / 'result.json'), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as handle:
        handle.write(encoded)
    for entry in report['cases']:
        print(json.dumps(entry, ensure_ascii=False))
    print('STATUS', report['status'], 'cleanup', report.get('cleanup_completed'))
    print('REPORT_SHA256', hashlib.sha256(encoded).hexdigest())


if __name__ == '__main__':
    raise SystemExit(main())
