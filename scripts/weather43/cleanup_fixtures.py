#!/usr/bin/env python3
"""Remove only the two exact synthetic weather43 fixtures and their own flows.

Keep customer authorization, browser sessions, and every real device. Defaults
to a read-only count. Database configuration is read privately from the running
API container; credentials and SQL results containing identities are not logged.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import urlparse


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--execute", action="store_true")
    args = p.parse_args()
    assert os.name == "posix" and os.geteuid() == 0
    root = args.root.resolve()
    devices = [json.loads((root / "private" / name).read_text())["device_id"] for name in
               ("voice-test-device.json", "voice-test-device-account2.json")]
    assert len(set(devices)) == 2
    assert all(re.fullmatch(r"weather43-coexist-[a-f0-9]{16}", device) for device in devices)
    identities = ",".join("'" + device + "'" for device in devices)
    item = json.loads(subprocess.check_output(["docker", "inspect", "xiaozhi-pr26-api"]))[0]
    env = dict(value.split("=", 1) for value in item["Config"]["Env"] if "=" in value)
    url = urlparse(env["SPRING_DATASOURCE_DRUID_URL"][5:])
    config = ("[client]\nuser=" + env["SPRING_DATASOURCE_DRUID_USERNAME"] + "\npassword=" +
              env["SPRING_DATASOURCE_DRUID_PASSWORD"] + "\ndatabase=" + url.path.strip("/") + "\n")

    def query(sql):
        result = subprocess.run(["docker", "exec", "-i", "issue29-db", "mysql",
                                 "--defaults-file=/dev/stdin", "-N", "-B", "-e", sql],
                                input=config, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, "exact_fixture_database_operation_failed_no_private_output"
        return result.stdout.strip().splitlines()

    counts = ("SELECT " + ",".join("(SELECT COUNT(*) FROM " + table + " WHERE device_id IN (" + identities + "))"
              for table in ("dc_device", "dc_request", "dc_voice_token", "customer_device_session")) + ";")
    before = list(map(int, query(counts)[0].split("\t")))
    if not args.execute:
        print(json.dumps({"read_only": True, "counts_device_request_token_browser_device_session": before}))
        return
    assert (root / "evidence/coexistence-finished").exists(), "finish_all_real_voice_attempts_first"
    assert (root / "evidence/recovery-complete").exists()
    assert "weather43-voice-probe" not in subprocess.check_output(
        ["docker", "ps", "--format", "{{.Names}}"], text=True).splitlines()
    assert before[0] == 2
    # A delivery record would mean the manually created fixture assumptions changed.
    assert query("SELECT COUNT(*) FROM dc_delivery_operation WHERE device_id IN (" + identities + ");") == ["0"]
    # No other device may use a flow belonging to these exact test requests.
    outsiders = ("SELECT COUNT(*) FROM customer_device_session s JOIN customer_identity_flow f ON f.id=s.flow_id "
                 "JOIN customer_device_request r ON r.handle=f.request_handle JOIN dc_request q ON q.request_id=r.control_request_id "
                 "WHERE q.device_id IN (" + identities + ") AND s.device_id NOT IN (" + identities + ");")
    assert query(outsiders) == ["0"]
    sql = """START TRANSACTION;
SELECT id FROM customer_identity_barrier WHERE id=1 FOR UPDATE;
CREATE TEMPORARY TABLE weather43_flows AS
 SELECT f.id FROM customer_identity_flow f JOIN customer_device_request r ON r.handle=f.request_handle
 JOIN dc_request q ON q.request_id=r.control_request_id WHERE q.device_id IN ({ids});
DELETE b FROM customer_binding_operation b LEFT JOIN customer_device_session s ON s.id=b.device_session_id
 LEFT JOIN weather43_flows f ON f.id=b.flow_id WHERE s.device_id IN ({ids}) OR f.id IS NOT NULL;
DELETE o FROM customer_device_operation o JOIN customer_device_session s ON s.id=o.device_session_id
 WHERE s.device_id IN ({ids});
DELETE FROM customer_device_session WHERE device_id IN ({ids});
DELETE f FROM customer_identity_flow f JOIN weather43_flows w ON w.id=f.id;
DELETE r FROM customer_device_request r JOIN dc_request q ON q.request_id=r.control_request_id
 WHERE q.device_id IN ({ids});
DELETE FROM dc_request_operation WHERE device_id IN ({ids});
DELETE FROM dc_request WHERE device_id IN ({ids});
DELETE s FROM dc_voice_session s JOIN dc_voice_token t ON t.token_hash=s.token_hash WHERE t.device_id IN ({ids});
DELETE FROM dc_voice_token WHERE device_id IN ({ids});
DELETE FROM dc_state_ack_history WHERE device_id IN ({ids});
DELETE FROM dc_state_ack WHERE device_id IN ({ids});
DELETE FROM dc_device WHERE device_id IN ({ids});
COMMIT;""".format(ids=identities)
    query(sql)
    after = list(map(int, query(counts)[0].split("\t")))
    assert after == [0, 0, 0, 0]
    report = {"at": datetime.now(timezone.utc).isoformat(), "only_exact_test_fixtures_deleted": True,
              "device_id_sha256": [hashlib.sha256(device.encode()).hexdigest() for device in devices],
              "before_counts_device_request_token_browser_device_session": before,
              "after_counts_device_request_token_browser_device_session": after,
              "customer_authorization_modified": False, "customer_browser_modified": False,
              "only_flows_linked_to_exact_test_requests_deleted": True,
              "business_services_restarted_by_cleanup": False,
              "recording_scope": "synthetic WAV; decoded output audio hashed in memory",
              "temporary_device_is_not_physical_speaker_acceptance": True,
              "cleanup_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    with (root / "evidence/voice/fixture-cleanup.json").open("x") as output:
        json.dump(report, output, indent=2)
    print("Cleaned two exact synthetic fixtures and their own flows; preserved account authorizations and browsers")


if __name__ == "__main__":
    main()
