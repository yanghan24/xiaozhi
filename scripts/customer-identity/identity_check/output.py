"""Allowlisted reports and exclusive private files, with no raw diagnostics."""
import json
import os
import re

from .codes import PUBLIC_CODES
from .errors import CheckError
from .timeutil import describe_time


RESULTS = {"pass": 0, "unmet": 1, "insufficient": 3, "error": 4}
STATUSES = {"AWAITING_IDENTITY", "EXCHANGING", "IDENTITY_VERIFIED", "SESSION_ENDED",
            "IDENTITY_FAILED", "IDENTITY_REFUSED", "FLOW_EXPIRED"}
AUTHORIZATIONS = {"ACTIVE", "REFRESHING", "REFRESH_UNCERTAIN", "REAUTH_REQUIRED", "NONE"}
_REASON = re.compile(r"[A-Za-z][A-Za-z0-9_]{0,63}")
CHECK_IDS = frozenset("""
execution activity window_started baseline_available baseline_distinct baseline_time
baseline_target baseline_target_a baseline_target_b baseline_activity baseline_status
baseline_status_a baseline_status_b baseline_authorization_a baseline_authorization_b
environment_containers environment_db_target environment_proxy_target environment_schema
containers db_target proxy_target schema not_truncated identities_distinct target_unique
target_unique_a target_unique_b receipt_continuity receipt_continuity_gate authorization
authorization_a_info authorization_b_info authorization_stable_a clock_skew identity_present
window_available identity_verified flow_status no_prior_receipt no_prior_receipt_b
receipt_matched receipt_matched_b matched_receipts session_ended session_ended_b
entry_log_204 no_notify_supported receipt_absent_a session_held_a idle_deadline
idle_deadline_report last_activity browser_evidence log baseline_capture
""".split())
_SENSITIVE = re.compile(
    r"\beyJ[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+|\b[a-fA-F0-9]{64}\b|"
    r"(?<![A-Za-z0-9_-])[A-Za-z0-9_-]{43,}(?![A-Za-z0-9_-])|"
    r"\b(?:\d{1,3}\.){3}\d{1,3}\b|Bearer\s|password|secret", re.I)


def _count(value):
    if type(value) is not int or value < 0:
        raise CheckError("REPORT_INVALID")
    return value


def _label(value):
    if not isinstance(value, str) or not re.fullmatch(r"F[1-9][0-9]?", value):
        raise CheckError("REPORT_INVALID")
    return value


def _code(value):
    if not isinstance(value, str) or not _REASON.fullmatch(value):
        raise CheckError("REPORT_INVALID")
    return value


def _reason(value):
    if not isinstance(value, str) or value not in PUBLIC_CODES:
        raise CheckError("REPORT_INVALID")
    return value


def error_report(code):
    return {"schema": "xiaozhi-identity-check-v1", "result": "error", "exit_code": 4,
            "error": _code(code)}


def build_report(scenario, phase, observation, evaluation):
    outcome = evaluation.get("result")
    if outcome not in RESULTS or scenario not in ("login", "logout", "two-account", "idle"):
        raise CheckError("REPORT_INVALID")
    report = {"schema": "xiaozhi-identity-check-v1", "scenario": scenario, "phase": phase,
              "result": outcome, "exit_code": RESULTS[outcome],
              "observed_at": describe_time(observation["observed_at_ms"]),
              "clock_skew_ms": observation.get("clock_skew_ms", 0),
              "environment": {}, "flows": {}, "checks": []}
    if type(report["clock_skew_ms"]) is not int:
        raise CheckError("REPORT_INVALID")
    for key in ("containers", "db_target", "proxy_target", "schema"):
        value = observation.get("environment", {}).get(key, "unknown")
        if value not in ("ok", "unknown", "error"):
            raise CheckError("REPORT_INVALID")
        report["environment"][key] = value
    window = observation.get("window")
    if window:
        report["window"] = {"start": describe_time(window["start_ms"]),
                            "end": describe_time(window["end_ms"]),
                            "closed": observation["observed_at_ms"] >= window["end_ms"]}
    targets = evaluation.get("selected_targets", {})
    for label, rows in observation.get("flows", {}).items():
        if label not in ("A", "B") or len(rows) > 50:
            raise CheckError("REPORT_INVALID")
        report["flows"][label] = []
        for row in rows:
            stored = row["stored_status"]
            authorization = row["authorization"]
            if stored not in STATUSES or authorization not in AUTHORIZATIONS:
                raise CheckError("REPORT_INVALID")
            effective = "FLOW_EXPIRED" if stored in ("AWAITING_IDENTITY", "EXCHANGING") and row["expires_at_ms"] <= observation["observed_at_ms"] else stored
            flow = {"label": _label(row["label"]), "stored_status": stored, "effective_status": effective,
                    "authorization": authorization,
                    "target": targets.get(label) == row["label"],
                    "created_at": describe_time(row["created_at_ms"]),
                    "expires_at": describe_time(row["expires_at_ms"])}
            for field in ("sid_present", "identity_present"):
                if type(row[field]) is not bool:
                    raise CheckError("REPORT_INVALID")
                flow[field] = row[field]
            if scenario == "idle" and row.get("last_activity_ms") is not None:
                flow["last_activity"] = describe_time(row["last_activity_ms"])
                flow["idle_deadline"] = describe_time(row["last_activity_ms"] + 1800000)
                flow["idle_elapsed_ms"] = observation["observed_at_ms"] - row["last_activity_ms"]
                flow["database_idle_expired"] = observation["observed_at_ms"] >= row["last_activity_ms"] + 1800000
            for field in ("matched_before_window", "matched_in_window"):
                if field in row:
                    flow[field] = _count(row[field])
            if "matched_received_at_ms" in row:
                flow["matched_received_at"] = [describe_time(value) for value in row["matched_received_at_ms"][:500]]
            report["flows"][label].append(flow)
    if scenario in ("logout", "two-account"):
        report["receipts"] = {key: _count(value) for key, value in observation.get("receipts", {}).items()
                              if key in ("issuer_total", "issuer_before_window", "issuer_in_window", "other_issuer_total", "since_baseline") and value is not None}
        log = observation.get("log", {})
        status = log.get("status", "missing")
        if status not in ("complete", "incomplete", "missing", "not_configured"):
            raise CheckError("REPORT_INVALID")
        report["log"] = {"status": status, "reasons": [_reason(reason) for reason in log.get("reasons", [])]}
        for key in ("post_204_in_window", "post_400_in_window", "boundary_lines"):
            report["log"][key] = _count(log.get(key, 0))
    if scenario == "two-account":
        distinct = evaluation.get("identities_distinct", observation.get("identities_distinct"))
        if distinct is not None and type(distinct) is not bool:
            raise CheckError("REPORT_INVALID")
        report["identities_distinct"] = distinct
    for item in evaluation.get("checks", []):
        check = {"id": _code(item["id"]), "kind": item["kind"], "result": item["result"]}
        if check["id"] not in CHECK_IDS:
            raise CheckError("REPORT_INVALID")
        if check["kind"] not in ("premise", "criterion", "info") or check["result"] not in RESULTS:
            raise CheckError("REPORT_INVALID")
        if item.get("reason") is not None:
            check["reason"] = _reason(item["reason"])
        report["checks"].append(check)
    evidence = evaluation.get("browser_evidence")
    if scenario == "idle" and isinstance(evidence, dict):
        kind = evidence.get("kind")
        status = evidence.get("http_status")
        if kind not in ("captured", "user_reported") or type(status) is not int or not 100 <= status <= 599:
            raise CheckError("REPORT_INVALID")
        report["browser_evidence"] = {"kind": kind, "http_status": status,
                                      "observed_at": describe_time(evidence["observed_at_ms"]),
                                      "original_browser": evidence.get("original_browser") is True,
                                      "binding_matches": evidence.get("binding_matches") is True}
        if evidence.get("error") == "IDENTITY_REQUIRED":
            report["browser_evidence"]["error"] = "IDENTITY_REQUIRED"
    report["notes"] = [_reason(note) for note in evaluation.get("notes", [])]
    if phase == "baseline":
        report["baseline_ready"] = evaluation.get("baseline_ready") is True
    return report


def guard(text, sensitive=()):
    for value in sensitive:
        if not isinstance(value, str) or not value:
            continue
        if value in text or value.encode("utf-8").hex().lower() in text.lower():
            raise CheckError("OUTPUT_REDACTION_FAILED")
    if _SENSITIVE.search(text):
        raise CheckError("OUTPUT_REDACTION_FAILED")
    return text


def serialize(report, sensitive=()):
    return guard(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False), sensitive)


def summary(report):
    labels = {"pass": "通过", "unmet": "未满足", "insufficient": "证据不足", "error": "执行错误"}
    lines = ["结论：{}（退出码 {}）".format(labels[report["result"]], report["exit_code"])]
    if "scenario" in report:
        lines.insert(0, "场景：{}；阶段：{}".format(report["scenario"], report["phase"]))
        lines.append("观察时间：{}（香港 {}）".format(report["observed_at"]["utc"], report["observed_at"]["hong_kong"]))
    if "error" in report:
        lines.append("错误代码：" + report["error"])
    for label, flows in report.get("flows", {}).items():
        for flow in flows:
            lines.append("记录 {} {}{}：{}；sid {}；授权 {}".format(
                label, flow["label"], "（目标）" if flow["target"] else "", flow["effective_status"],
                "存在" if flow["sid_present"] else "不存在", flow["authorization"]))
            if "idle_deadline" in flow:
                lines.append("最后活动 {}；30 分钟截止 {}；数据库已超时 {}".format(
                    flow["last_activity"]["utc"], flow["idle_deadline"]["utc"], flow["database_idle_expired"]))
    if "receipts" in report:
        receipt = report["receipts"]
        lines.append("本 issuer 历史收据 {}；本轮 {}；其他 issuer 历史 {}".format(
            receipt.get("issuer_total", 0), receipt.get("issuer_in_window", 0), receipt.get("other_issuer_total", 0)))
    if "log" in report:
        lines.append("日志 {}；窗口内 POST 204 {} 次、400 {} 次".format(
            report["log"]["status"], report["log"]["post_204_in_window"], report["log"]["post_400_in_window"]))
    for check in report.get("checks", []):
        if check["kind"] != "info" and check["result"] != "pass":
            lines.append("{}：{}{}".format(check["id"], labels[check["result"]], " " + check["reason"] if "reason" in check else ""))
    if report.get("phase") == "baseline":
        lines.append("仅采集前基线，不代表业务验收通过。")
    return "\n".join(lines)


def write_private(path, text):
    fd = None
    try:
        fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
        if os.name != "nt" and os.geteuid() == 0:
            uid, gid = os.environ.get("SUDO_UID", ""), os.environ.get("SUDO_GID", "")
            if uid.isdigit() and gid.isdigit():
                os.fchown(fd, int(uid), int(gid))
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            fd = None
            stream.write(text + "\n")
            stream.flush()
            os.fsync(stream.fileno())
    except (OSError, ValueError, TypeError):
        raise CheckError("OUTPUT_FILE_UNAVAILABLE") from None
    finally:
        if fd is not None:
            os.close(fd)
