#!/usr/bin/env python3
"""Observe first TTS request through the product WebSocket and external fixtures."""
import asyncio
import importlib.util
import json
import sys
from pathlib import Path


path = Path(__file__).with_name('check-product-knowledge.py')
spec = importlib.util.spec_from_file_location('product_knowledge_fixture', path)
fixture_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture_module)


if __name__ == '__main__':
    Path('/app/data').mkdir(exist_ok=True)
    Path('/app/data/.config.yaml').write_text('{}\n')
    from config.settings import check_config_file
    check_config_file()
    cases = {
        'earliest-first-pause': ('你好，我是语音助手，可以帮助你。', '你好'),
        'quoted-pause': ('欢迎来到“甲，乙”的世界，我是语音助手。', '欢迎来到甲乙的世界'),
        'last-flush': ('你好我是语音助手', '你好我是语音助手'),
    }
    case = sys.argv[1] if len(sys.argv) > 1 else 'earliest-first-pause'
    fixture_module.ORDINARY_ANSWER, expected_first = cases[case]
    fixture = fixture_module.BoundaryFixture('ordinary')
    with fixture_module.external_services(fixture):
        asyncio.run(fixture_module.run(fixture))
    assert fixture_module.normalized_speech(fixture.synthesis[0]) == expected_first, (
        'First speech must start at the earliest safe pause', fixture.synthesis[0])
    assert fixture_module.normalized_speech(''.join(fixture.synthesis)) == (
        fixture_module.normalized_speech(fixture_module.ORDINARY_ANSWER)), 'Answer lost or duplicated text'
    print(json.dumps({'result': 'pass', 'case': case,
                      'complete_answer': True, 'real_external_requests': 0}))
