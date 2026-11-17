import datetime
import hashlib
import json
import os
import pathlib
import stat
import subprocess

HOME_DIR = pathlib.Path('/home/jt')
OUT = HOME_DIR / ('issue12-minimal-verification-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
OUT.mkdir(mode=0o700, exist_ok=False)
os.chown(OUT, 1000, 1000)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def read_json(path):
    return json.loads(path.read_bytes())

def command(args, **kwargs):
    result = subprocess.run(args, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError('command failed: ' + args[0])
    return result.stdout

def save(name, data, private=False):
    p = OUT / name
    p.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
    p.chmod(0o600)
    os.chown(p, 1000, 1000)
    return str(p)

expected = read_json(HOME_DIR / 'issue12-source-audit-20261113.json')
candidate = '830b11a30550de9533065706c1c9d275484b4cbb'
reports = {}
report_specs = {
    'installed01': ('issue12-main6bf-20261112/results/installed01/result.json', None),
    'installed02': ('issue12-main6bf-20261112/results/installed02/result.json', None),
    'discovery': ('issue12-stage3-discovery-20261112/discovery01/discovery.json', 'a8ac41feb034101973e42ab07a83de232f25eb40dadaab06e36ff04ce8dd6d10'),
    'normal': ('issue12-sdk-final81-20261112/results/live-tool05/result.json', '311426c20915fcc074b90be4ae2072107ec47a17497a3b520033573168f7ba8f'),
    'behaviour': ('issue12-a2-20261112/run01/result.json', 'a8be4d736a5a3711859ed07216cd91217deda9a07993578f6488ec160c057d31'),
    'invalid_input': ('issue12-a2-20261112/errors01/result.json', 'b5262ba7dc487997f561f6ee6a3e339816a5f0febcfb84113977fef9407aac43'),
}
documents = {}
for name, (rel, historical_hash) in report_specs.items():
    p = HOME_DIR / rel
    b = p.read_bytes()
    documents[name] = json.loads(b)
    reports[name] = {'path': str(p), 'bytes': len(b), 'sha256': sha(b),
                     'historical_document_sha256': historical_hash,
                     'matches_historical_document_hash': sha(b) == historical_hash if historical_hash else None}

source_results = []
for item in expected['files']:
    p = HOME_DIR / 'issue12-main6bf-20261112/src' / item['path']
    b = p.read_bytes()
    row = {'path': item['path'], 'server_raw_sha256': sha(b), 'server_lf_sha256': sha(b.replace(b'\r\n', b'\n')),
           'candidate_lf_sha256': item['current_lf_sha256'], 'historical_raw_sha256': item['historical_raw_sha256']}
    row['matches_historical_raw'] = row['server_raw_sha256'] == item['historical_raw_sha256']
    row['matches_candidate_content'] = row['server_lf_sha256'] == item['current_lf_sha256']
    row['both_reports_reference_actual_source'] = all(documents[n]['source_sha256'][item['path']] == sha(b) for n in ['installed01','installed02'])
    row['server_converted_to_crlf_matches_historical_manifest'] = sha(b.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')) == item['historical_raw_sha256']
    if not (row['server_converted_to_crlf_matches_historical_manifest'] and row['matches_candidate_content'] and row['both_reports_reference_actual_source']):
        print(json.dumps(row))
        raise RuntimeError('source comparison mismatch')
    source_results.append(row)

regressions = {}
for name in ['installed01','installed02']:
    d = documents[name]
    assert (d['run'], d['pass'], d['fail'], d['error'], d['skip']) == (81, 81, 0, 0, 0)
    assert d['guard_denials'] == {} and len(d['cases']) == 81
    regressions[name] = {k:d[k] for k in ['run','pass','fail','error','skip','sdk','python','installed_metadata','network','guard_denials','real_mcp_requests','real_model_requests','remote_cancellation_verified']}
    regressions[name]['case_ids_unique'] = len({c['id'] for c in d['cases']}) == 81
    original = HOME_DIR / report_specs[name][0]
    copied = OUT / (name + '-result.json')
    copied.write_bytes(original.read_bytes())
    copied.chmod(0o600)
    os.chown(copied,1000,1000)

discovery = documents['discovery']
tool = next(t for t in discovery['tools_list_result']['tools'] if t['name'] == 'ask_knowledge')
protocol = {'initialize_result': discovery['initialize_result'],
            'tool_name':tool['name'], 'input_schema': tool['inputSchema'], 'annotations':tool.get('annotations'),
            'tool_version':'not_exposed', 'observation_time':discovery['ended_utc'], 'fresh_remote_discovery':False}
normal = documents['normal']
normal_summary = {k:normal[k] for k in ['started_utc','ended_utc','python','mcp_distribution','probe_sha256','tools_call_submissions','model_requests','tts_requests','retries','status','initialization_attempts','connected_after_initialize','tool_result_summary','cleanup_completed','connected_after_cleanup','worker_reference_cleared']}
assert normal_summary['tools_call_submissions'] == 1 and normal_summary['retries'] == 0 and normal_summary['cleanup_completed']
normal_summary['candidate_mcp_source_matches'] = {}
for item in expected['files']:
    if '/server_mcp/' in item['path']:
        p = HOME_DIR / 'issue12-sdk-final81-20261112/src' / item['path']
        b = p.read_bytes()
        matches = sha(b) == normal['source_sha256'][item['path']] and sha(b.replace(b'\r\n',b'\n')) == item['current_lf_sha256']
        normal_summary['candidate_mcp_source_matches'][item['path']] = matches
        assert matches
probe_matches = []
for folder in HOME_DIR.glob('issue12-*'):
    if folder.is_dir():
        for p in folder.rglob('*.py'):
            b = p.read_bytes()
            if sha(b) == normal['probe_sha256']:
                probe_matches.append({'path':str(p),'sha256':sha(b),'contains_fixed_public_question':'怎么申请成立社会团体' in b.decode('utf-8-sig')})
normal_summary['probe_files_matching_report_hash'] = probe_matches

behaviour = documents['behaviour']
behaviour_summary = {'status':behaviour['status'],'cleanup_completed':behaviour['cleanup_completed'], 'cases':[]}
for c in behaviour['cases']:
    r = c.get('result',{})
    behaviour_summary['cases'].append({**{k:c[k] for k in ['case','question_chars','calls','outcome']}, 'result':{k:r[k] for k in ['is_error','content_blocks','observed','sources_count','chunks_count'] if k in r}})
invalid = documents['invalid_input']
invalid_summary = {'status':invalid['status'],'cleanup_completed':invalid['cleanup_completed'],
                   'cases':[{k:c[k] for k in ['case','question_chars','calls','outcome','error_type','code']} for c in invalid['cases']]}
assert sorted(c['question_chars'] for c in invalid_summary['cases']) == [0,301]
assert all(c['code'] == -32602 for c in invalid_summary['cases'])

services = {}
voice_inspection = None
for name in ['xiaozhi-pr26-api','xiaozhi-pr26-voice']:
    d = json.loads(command(['docker','inspect',name]))[0]
    services[name] = {'image':d['Config']['Image'],'image_id':d['Image'],'running':d['State']['Running'],'started_at':d['State']['StartedAt']}
    if name == 'xiaozhi-pr26-voice':
        voice_inspection = d
test_image = 'sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5'
image = json.loads(command(['docker','image','inspect',test_image]))[0]
assert image['Id'] == test_image
metadata_code = "import json,sys,importlib.metadata as m; print(json.dumps({'python':sys.version,'installed_metadata':{n:m.version(n) for n in ['mcp','mcp-types','httpx2','httpx','openai','anyio']}}))"
test_metadata = json.loads(command(['docker','run','--rm','--network','none','--read-only','--cap-drop','ALL','--security-opt','no-new-privileges','--entrypoint','python',test_image,'-I','-B','-c',metadata_code]))
assert test_metadata['installed_metadata'] == documents['installed01']['installed_metadata']
test_metadata['image_id'] = test_image
test_metadata['network'] = 'none'
test_metadata['action'] = 'metadata inspection only; no regression rerun'
runtime_code = "import json,pathlib,hashlib,sys,importlib.metadata as m; root=pathlib.Path('/app/core/providers/tools/server_mcp'); print(json.dumps({'python':sys.version,'mcp':m.version('mcp'),'module_sha256':{p.name:hashlib.sha256(p.read_bytes().replace(b'\\r\\n',b'\\n')).hexdigest() for p in root.glob('*.py')}}))"
runtime = json.loads(command(['docker','exec','xiaozhi-pr26-voice','python','-B','-c',runtime_code]))

config_path = HOME_DIR / '.private-mcp/config.json'
cfg = read_json(config_path)
st = config_path.stat()
config = {'path':str(config_path),'mode':oct(stat.S_IMODE(st.st_mode)),'uid':st.st_uid,
          'url_matches_contract':cfg.get('url') == 'https://aimarket.hncig.cn:4191/mcp/service-669a396c73/sse',
          'authorization_set':bool(cfg.get('headers',{}).get('Authorization')),
          'config_mtime_utc':datetime.datetime.fromtimestamp(st.st_mtime,datetime.timezone.utc).isoformat()}
assert config['url_matches_contract'] and config['authorization_set'] and stat.S_IMODE(st.st_mode) == 0o600
env = dict(v.split('=',1) for v in voice_inspection['Config']['Env'] if '=' in v)
device_id = env['PRODUCT_MAINTENANCE_DEVICE_ID']
assert len(device_id) <= 128 and all(c.isalnum() or c in '-_:' for c in device_id)
sql = ("SET SESSION TRANSACTION READ ONLY; START TRANSACTION WITH CONSISTENT SNAPSHOT; "
       "SELECT device_id,owner_subject,admitted,local_enabled,reason,permission_version,ownership_version "
       "FROM dc_device WHERE device_id='" + device_id + "'; COMMIT;")
rows = command(['docker','exec','-i','issue29-db','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" mysql -uroot --batch --raw "$MYSQL_DATABASE"'],input=sql,text=True).splitlines()
assert len(rows) == 2
device = dict(zip(rows[0].split('\t'),rows[1].split('\t')))
assert device['owner_subject'] not in ['', 'NULL']
scope = {'recorded_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'device_label':'P4-B', 'account_label':'B-current-owner-20261113',
         'authorization_source':'Direct human reply in Codex chat 01a1d297-23e1-7bc0-ab82-2b6c4d172e2a on 2026-11-13',
         'authorization_text':'确认使用 B 及其当前归属测试账号，按上述范围测试',
         'device_mapping_source':'Existing B PRODUCT_MAINTENANCE_DEVICE_ID and current dc_device row',
         'config_reference':str(config_path),'service':'service-669a396c73','tool':'ask_knowledge','fixed_public_question':'怎么申请成立社会团体',
         'permission_boundary':'Only this device and its current owner; no other customers, model calls, TTS, firmware, or deployment',
         'private_device_record':device,'config_sha256_private':sha(config_path.read_bytes())}
scope_path = save('scope-private.json',scope,private=True)
scope_public = {k:v for k,v in scope.items() if k not in ['private_device_record','config_sha256_private']}
scope_public['private_mapping_path'] = scope_path
scope_public['current_device_state'] = {k:device[k] for k in ['admitted','local_enabled','reason','permission_version','ownership_version']}
scope_public['has_owner'] = True
scope_public['model_or_oidc_authorization_revalidated'] = False

manifest = HOME_DIR / 'issue12-main6bf-20261112/src/test-manifest.json'
result = {'schema':'issue12-server-evidence-audit-v1','observed_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'candidate_commit':candidate,'candidate_deployed':False,'services':services,'runtime_voice':runtime,'test_environment':test_metadata,
          'config':config,'scope':scope_public,'original_reports':reports,'source_comparison':source_results,
          'source_manifest':{'path':str(manifest),'raw_sha256':sha(manifest.read_bytes()),'lf_sha256':sha(manifest.read_bytes().replace(b'\r\n',b'\n'))},
          'regressions':regressions,'discovery':protocol,'normal_tool':normal_summary,'behaviour':behaviour_summary,'invalid_input':invalid_summary,
          'historical_hash_discrepancy':'Two A2 report hashes differ from historical document strings. Current files were re-read and checked; historical immutability is not established.',
          'remote_cancellation':'unknown','new_mcp_calls':0,'new_model_calls':0,'new_tts_calls':0,'regression_rerun':False,
          'service_restarts':0,'business_database_writes':0,'physical_device_actions':0}
save('audit.json',result)
print(json.dumps(result,ensure_ascii=False,indent=2))
