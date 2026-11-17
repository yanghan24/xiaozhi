"""Bounded server data source: Docker inspection, MySQL observation and log read.

No network client is imported.  Every external command is a fixed argv list
without a shell, SQL travels over stdin, and only fixed safe codes are raised.
"""
import hashlib
import json
import re
import subprocess
import threading
import time
from collections import namedtuple

from .errors import CheckError
from . import envcheck, sql, timeutil


SQL_STDOUT_LIMIT = sql.SQL_STDOUT_LIMIT
LOG_STDOUT_LIMIT = 8 * 1024 * 1024
MAX_INPUT = 1024 * 1024
STDERR_LIMIT = 64 * 1024
TOTAL_TIMEOUT = 60.0
MAX_SQL_BYTES = sql.MAX_SCRIPT_BYTES

# The shell body is a constant.  Container credentials stay in the container
# environment; no secret is placed in argv.  MYSQL_USER/MYSQL_PASSWORD are the
# provisional database-container variable names documented for this tool.
MYSQL_COMMAND = (
    'test -n "$MYSQL_USER" && test "$MYSQL_USER" != "root" '
    '&& test -n "$MYSQL_PASSWORD" '
    '&& MYSQL_PWD="$MYSQL_PASSWORD" exec mysql --protocol=TCP '
    '--connect-timeout=5 --batch --raw --skip-column-names --skip-show-warnings '
    '--default-character-set=utf8mb4 -u"$MYSQL_USER"'
)

RunResult = namedtuple("RunResult", "returncode stdout stderr")

_ID = re.compile(r"^[0-9a-f]{12,64}$")
_DIGEST = re.compile(r"^[0-9a-f]{64}$")
_IPV4 = re.compile(
    r"^(?:25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9])"
    r"(?:\.(?:25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9])){3}$")
_IPV6 = re.compile(r"^\[?(?:[0-9A-Fa-f]{0,4}:){2,7}[0-9A-Fa-f]{0,4}\]?$")


class StreamOverflow(Exception):
    """Internal marker: a bounded stdout stream exceeded its limit."""


class LocalRunner(object):
    """subprocess runner with per-command and total deadlines.

    ``run`` never uses a shell and terminates a child that exceeds a timeout,
    the total deadline, or the stdout byte cap.  Raw output is returned only to
    the caller in memory; this class never prints or includes it in an error.
    """

    def __init__(self, timeout=20, total_timeout=TOTAL_TIMEOUT):
        if type(timeout) is not int or not 1 <= timeout <= 60:
            raise CheckError("COMMAND_INVALID")
        if not isinstance(total_timeout, (int, float)) or total_timeout <= 0:
            raise CheckError("COMMAND_INVALID")
        self.timeout = timeout
        self.total_timeout = float(total_timeout)
        self._deadline = None

    def begin(self):
        self._deadline = time.monotonic() + self.total_timeout

    def _remaining(self):
        if self._deadline is None:
            self.begin()
        return self._deadline - time.monotonic()

    def run(self, argv, input_text=None, timeout=None, max_stdout=SQL_STDOUT_LIMIT):
        if (not isinstance(argv, list) or not argv or len(argv) > 64
                or any(not isinstance(value, str) or "\x00" in value or len(value) > 4096
                       for value in argv)):
            raise CheckError("COMMAND_INVALID")
        if type(max_stdout) is not int or max_stdout <= 0:
            raise CheckError("COMMAND_INVALID")
        if timeout is None:
            timeout = self.timeout
        if not isinstance(timeout, (int, float)) or timeout <= 0:
            raise CheckError("COMMAND_INVALID")
        remaining = self._remaining()
        if remaining <= 0:
            raise CheckError("TOTAL_DEADLINE_EXCEEDED")
        effective = min(float(timeout), remaining)
        payload = None
        if input_text is not None:
            if not isinstance(input_text, str):
                raise CheckError("COMMAND_INVALID")
            payload = input_text.encode("utf-8")
            if len(payload) > MAX_INPUT:
                raise CheckError("COMMAND_INPUT_TOO_LARGE")
        try:
            process = subprocess.Popen(
                argv,
                stdin=subprocess.PIPE if payload is not None else subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                shell=False,
            )
        except (OSError, ValueError):
            raise CheckError("COMMAND_UNAVAILABLE") from None

        state = {"out": [], "err": [], "overflow": False, "stderr_overflow": False}

        def reader(stream, key, limit, overflow_key):
            chunks = []
            total = 0
            try:
                while True:
                    chunk = stream.read(65536)
                    if not chunk:
                        break
                    total += len(chunk)
                    if total > limit:
                        state[overflow_key] = True
                        try:
                            process.kill()
                        except OSError:
                            pass
                        break
                    chunks.append(chunk)
            except (OSError, ValueError):
                pass
            state[key] = chunks

        def writer(stream, data):
            try:
                stream.write(data)
                stream.close()
            except (OSError, ValueError):
                pass

        out_thread = threading.Thread(
            target=reader, args=(process.stdout, "out", max_stdout, "overflow"), daemon=True)
        err_thread = threading.Thread(
            target=reader, args=(process.stderr, "err", STDERR_LIMIT, "stderr_overflow"),
            daemon=True)
        in_thread = None
        out_thread.start()
        err_thread.start()
        if payload is not None:
            in_thread = threading.Thread(target=writer, args=(process.stdin, payload), daemon=True)
            in_thread.start()
        timed_out = False
        try:
            process.wait(timeout=effective)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                process.kill()
            except OSError:
                pass
            try:
                process.wait(timeout=5)
            except (subprocess.TimeoutExpired, OSError):
                pass
        except OSError:
            pass
        for thread in (out_thread, err_thread):
            thread.join(2.0)
        if in_thread is not None:
            in_thread.join(2.0)
        for stream in (process.stdout, process.stderr):
            try:
                stream.close()
            except (OSError, ValueError, AttributeError):
                pass
        if state["overflow"]:
            raise StreamOverflow()
        if timed_out:
            raise CheckError("COMMAND_TIMEOUT")
        stdout = b"".join(state["out"])
        stderr = b"".join(state["err"])
        return RunResult(process.returncode if process.returncode is not None else -1,
                         stdout, stderr)

    def mysql(self, script, container, timeout=None, max_stdout=SQL_STDOUT_LIMIT):
        container = sql.validate_container(container)
        sql.assert_read_only(script)
        argv = ["docker", "exec", "-i", container, "sh", "-c", MYSQL_COMMAND]
        return self.run(argv, input_text=script, timeout=timeout,
                        max_stdout=max_stdout)


def _now_ms():
    return time.time_ns() // 1000000


def _text(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    if isinstance(value, str):
        return value
    return ""


def _redact_token(token):
    core = token
    if len(core) >= 2 and core[0] == core[-1] and core[0] in ("'", '"'):
        core = core[1:-1]
    if _IPV4.fullmatch(core):
        return token.replace(core, "<ip>")
    if _IPV6.fullmatch(core):
        colon_count = core.count(":")
        letters = any(character.lower() in "abcdef" for character in core)
        if "::" in core or colon_count >= 3 or letters:
            return token.replace(core, "<ip>")
    return token


def redact_log_text(text):
    """Replace recognised address tokens before log text leaves the collector."""
    if not isinstance(text, str):
        return ""
    return re.sub(r"\S+", lambda match: _redact_token(match.group(0)), text)


class ServerSource(object):
    """Single collection pass for one scenario observation."""

    def __init__(self, env, timeout=20):
        self.env = envcheck.validate_environment(env)
        self.timeout = timeout
        self.runner = LocalRunner(timeout, TOTAL_TIMEOUT)

    def __repr__(self):
        return "ServerSource()"

    # -- entry point ---------------------------------------------------
    def collect(self, records, start_ms=None, end_ms=None, baseline=None):
        self.runner.begin()
        handles = self._handles(records)
        start_ms, end_ms = self._window(start_ms, end_ms)
        baseline = self._baseline(baseline)
        environment = envcheck.check_environment(self.env, self.runner, timeout=self.timeout)
        log_configured = envcheck.entry_log_configured(
            self.env, self.runner, timeout=self.timeout)
        fingerprint = self._environment_fingerprint()
        before = _now_ms()
        script = sql.build_observation_script(
            self.env["db"]["database"], self.env["issuer"], handles, start_ms, end_ms,
            since_ms=None if baseline is None else baseline["observed_at_ms"])
        try:
            result = self.runner.mysql(
                script, self.env["db"]["container"], timeout=self.timeout)
        except StreamOverflow:
            raise CheckError("SQL_OUTPUT_OVERFLOW") from None
        after = _now_ms()
        if result.returncode != 0:
            raise CheckError(sql.mysql_error_name(getattr(result, "stderr", b"")))
        rows = sql.parse_json_rows(result.stdout)
        observation = self._assemble(rows, handles, start_ms, end_ms, baseline,
                                     environment, before, after)
        observation["environment_fingerprint"] = fingerprint
        observation["log_capture"] = self._capture_log(log_configured, baseline)
        return observation

    # -- input validation ---------------------------------------------
    def _handles(self, records):
        if not isinstance(records, dict) or not records or "A" not in records:
            raise CheckError("RECORDS_INVALID")
        if set(records) - {"A", "B"}:
            raise CheckError("RECORDS_INVALID")
        handles = {}
        for label in ("A", "B"):
            if label not in records:
                continue
            record = records[label]
            if getattr(record, "label", None) != label:
                raise CheckError("RECORDS_INVALID")
            handle = getattr(record, "handle", None)
            handles[label] = sql.validate_handle(handle)
        if "B" in handles and handles["A"] == handles["B"]:
            raise CheckError("RECORDS_NOT_DISTINCT")
        return handles

    @staticmethod
    def _window(start_ms, end_ms):
        if start_ms is None and end_ms is None:
            return None, None
        if type(start_ms) is not int or type(end_ms) is not int:
            raise CheckError("WINDOW_INVALID")
        if not 0 <= start_ms < end_ms <= sql.MAX_MS:
            raise CheckError("WINDOW_INVALID")
        return start_ms, end_ms

    @staticmethod
    def _baseline(baseline):
        if baseline is None:
            return None
        if not isinstance(baseline, dict):
            raise CheckError("BASELINE_INVALID")
        observed = baseline.get("observed_at_ms")
        total = baseline.get("issuer_total")
        log_state = baseline.get("log_state", {})
        if type(observed) is not int or not 0 <= observed <= sql.MAX_MS:
            raise CheckError("BASELINE_INVALID")
        if type(total) is not int or total < 0:
            raise CheckError("BASELINE_INVALID")
        if not isinstance(log_state, dict):
            raise CheckError("BASELINE_INVALID")
        inode = log_state.get("inode")
        size = log_state.get("size")
        if inode is not None and (type(inode) is not int or inode < 0):
            raise CheckError("BASELINE_INVALID")
        if size is not None and (type(size) is not int or size < 0):
            raise CheckError("BASELINE_INVALID")
        # Temporal validity against a window is a premise left to
        # state.bind_baseline/evaluate; this layer checks shape only.
        return {"observed_at_ms": observed, "issuer_total": total,
                "log_state": {"inode": inode, "size": size}}

    # -- environment binding ------------------------------------------
    def _environment_fingerprint(self):
        identifiers = {}
        for label, name in (("db", self.env["db"]["container"]),
                            ("identity", self.env["identity_container"]),
                            ("entry", self.env["entry"]["container"])):
            try:
                result = self.runner.run(
                    ["docker", "inspect", "--format", "{{.Id}}", name],
                    timeout=self.timeout, max_stdout=4096)
            except StreamOverflow:
                raise CheckError("ENV_IDENTITY_UNAVAILABLE") from None
            value = _text(getattr(result, "stdout", b"")).strip()
            if result.returncode != 0 or _ID.fullmatch(value) is None:
                raise CheckError("ENV_IDENTITY_UNAVAILABLE")
            identifiers[label] = value
        payload = {
            "issuer": self.env["issuer"],
            "db": self.env["db"],
            "identity_container": self.env["identity_container"],
            "entry": self.env["entry"],
            "container_ids": identifiers,
            "server_uuid_digest": self._server_uuid_digest(),
        }
        encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
        return hashlib.sha256(encoded).hexdigest()

    def _server_uuid_digest(self):
        # The database hashes its server identity itself; the raw UUID is never
        # returned to the tool or placed in the observation.  A missing digest
        # means the baseline cannot be bound to this database instance.
        script = ("SET SESSION MAX_EXECUTION_TIME = 2000;\n"
                  "SET time_zone = '+00:00';\n"
                  "SELECT JSON_OBJECT('kind','server_uuid_digest','value',"
                  "SHA2(@@server_uuid,256));\n")
        try:
            result = self.runner.mysql(
                script, self.env["db"]["container"], timeout=self.timeout)
        except StreamOverflow:
            raise CheckError("ENV_IDENTITY_UNAVAILABLE") from None
        if result.returncode != 0:
            raise CheckError("ENV_IDENTITY_UNAVAILABLE")
        try:
            rows = sql.parse_json_rows(result.stdout)
        except CheckError:
            raise CheckError("ENV_IDENTITY_UNAVAILABLE") from None
        for row in rows:
            value = row.get("value")
            if (row.get("kind") == "server_uuid_digest" and isinstance(value, str)
                    and _DIGEST.fullmatch(value.lower())):
                return value.lower()
        raise CheckError("ENV_IDENTITY_UNAVAILABLE")

    # -- observation assembly -----------------------------------------
    def _assemble(self, rows, handles, start_ms, end_ms, baseline,
                  environment, before, after):
        observed = None
        receipts = None
        flows = {label: [] for label in handles}
        matched = {label: [] for label in handles}
        receipt_window = 0
        since_baseline_value = None
        pairs = []
        pair_count = 0
        pair_truncated = False
        for row in rows:
            kind = row.get("kind")
            if kind == "observed":
                if observed is not None:
                    raise CheckError("DATABASE_RESPONSE_INVALID")
                observed = self._timestamp(row.get("value"))
            elif kind == "flow":
                label = row.get("record")
                if label not in handles:
                    raise CheckError("DATABASE_RESPONSE_INVALID")
                flows[label].append(self._flow_row(row))
            elif kind == "matched":
                label = row.get("record")
                if label not in handles:
                    raise CheckError("DATABASE_RESPONSE_INVALID")
                matched[label].append((self._positive(row.get("order")),
                                       self._timestamp(row.get("received_at_ms"))))
            elif kind == "receipts":
                if receipts is not None:
                    raise CheckError("DATABASE_RESPONSE_INVALID")
                receipts = {
                    "issuer_total": self._count(row.get("issuer_total")),
                    "issuer_before_window": self._count(row.get("issuer_before_window")),
                    "issuer_in_window": self._count(row.get("issuer_in_window")),
                    "other_issuer_total": self._count(row.get("other_issuer_total")),
                }
            elif kind == "receipt_window":
                self._timestamp(row.get("received_at_ms"))
                receipt_window += 1
            elif kind == "since_baseline":
                if since_baseline_value is not None:
                    raise CheckError("DATABASE_RESPONSE_INVALID")
                since_baseline_value = self._count(row.get("value"))
            elif kind == "pair":
                pair_count += 1
                pairs.append((self._positive(row.get("a_order")),
                              self._positive(row.get("b_order")),
                              self._boolean(row.get("distinct"))))
            else:
                raise CheckError("DATABASE_RESPONSE_INVALID")
        if observed is None or receipts is None:
            raise CheckError("DATABASE_RESPONSE_INVALID")
        truncated = False
        if len(matched["A"]) >= 501 or (len(handles) == 2 and len(matched["B"]) >= 501):
            truncated = True
        if receipt_window >= 501:
            truncated = True
        if pair_count >= 2501:
            pair_truncated = True

        labels = {}
        output_flows = {}
        for label in ("A", "B"):
            if label not in handles:
                continue
            ordered = sorted(flows[label], key=lambda item: item["creation_order"])
            if len(ordered) > 50:
                truncated = True
                ordered = ordered[:50]
            order_to_label = {}
            for index, flow in enumerate(ordered, 1):
                name = "F%d" % index
                order = flow["creation_order"]
                if order in order_to_label:
                    raise CheckError("DATABASE_RESPONSE_INVALID")
                order_to_label[order] = name
            matched_by_order = {}
            for order, received in matched[label]:
                matched_by_order.setdefault(order, []).append(received)
            for flow in ordered:
                times = sorted(matched_by_order.get(flow["creation_order"], []))
                if len(times) > 500:
                    times = times[:500]
                    truncated = True
                flow["label"] = order_to_label[flow["creation_order"]]
                flow["matched_received_at_ms"] = times
            labels[label] = order_to_label
            output_flows[label] = ordered

        pair_rows = []
        seen_pairs = set()
        for a_order, b_order, distinct in pairs:
            a_label = labels.get("A", {}).get(a_order)
            b_label = labels.get("B", {}).get(b_order)
            if a_label is None or b_label is None:
                continue
            key = (a_label, b_label)
            if key in seen_pairs:
                continue
            seen_pairs.add(key)
            pair_rows.append({"a_label": a_label, "b_label": b_label,
                              "distinct": distinct})
        if pair_truncated:
            truncated = True

        identities_distinct = None
        if "B" in handles:
            left = [flow for flow in output_flows.get("A", [])
                    if flow["stored_status"] in sql.IDENTITY_BEARING_STATUSES
                    and flow["identity_present"]]
            right = [flow for flow in output_flows.get("B", [])
                     if flow["stored_status"] in sql.IDENTITY_BEARING_STATUSES
                     and flow["identity_present"]]
            if len(left) == 1 and len(right) == 1:
                for pair in pair_rows:
                    if pair["a_label"] == left[0]["label"] and pair["b_label"] == right[0]["label"]:
                        identities_distinct = pair["distinct"]
                        break

        since_baseline = None
        if baseline is not None:
            if since_baseline_value is None:
                raise CheckError("DATABASE_RESPONSE_INVALID")
            since_baseline = since_baseline_value
        receipts["since_baseline"] = since_baseline

        try:
            skew = timeutil.clock_skew(observed, before, after)
        except ValueError:
            raise CheckError("CLOCK_UNSTABLE") from None
        return {
            "observed_at_ms": observed,
            "clock_skew_ms": skew,
            "window": None if start_ms is None else {"start_ms": start_ms, "end_ms": end_ms},
            "environment": environment,
            "flows": output_flows,
            "identities_distinct": identities_distinct,
            "receipts": receipts,
            "truncated": truncated,
            "identity_pairs": pair_rows,
        }

    @staticmethod
    def _flow_row(row):
        status = sql.normalize_flow_status(row.get("status"))
        authorization = sql.normalize_authorization_status(row.get("authorization"))
        identity_present = ServerSource._boolean(row.get("identity_present"))
        issuer_matches = ServerSource._boolean(row.get("issuer_matches"))
        if status == "UNKNOWN":
            raise CheckError("STATUS_UNKNOWN")
        if authorization == "UNKNOWN":
            raise CheckError("AUTHORIZATION_UNKNOWN")
        if identity_present and not issuer_matches:
            raise CheckError("ENV_ISSUER_MISMATCH")
        identity_issued = row.get("identity_issued_at_ms")
        last_activity = row.get("last_activity_ms")
        return {
            "creation_order": ServerSource._positive(row.get("order")),
            "stored_status": status,
            "created_at_ms": ServerSource._timestamp(row.get("created_at_ms")),
            "expires_at_ms": ServerSource._timestamp(row.get("expires_at_ms")),
            "identity_issued_at_ms": (None if identity_issued is None
                                      else ServerSource._timestamp(identity_issued)),
            "sid_present": ServerSource._boolean(row.get("sid_present")),
            "identity_present": identity_present,
            "authorization": authorization,
            "last_activity_ms": (None if last_activity is None
                                 else ServerSource._timestamp(last_activity)),
            "matched_before_window": ServerSource._count(row.get("matched_before_window")),
            "matched_in_window": ServerSource._count(row.get("matched_in_window")),
        }

    @staticmethod
    def _timestamp(value):
        if type(value) is not int or not 0 <= value <= sql.MAX_MS:
            raise CheckError("DATABASE_RESPONSE_INVALID")
        return value

    @staticmethod
    def _count(value):
        if type(value) is not int or value < 0:
            raise CheckError("DATABASE_RESPONSE_INVALID")
        return value

    @staticmethod
    def _positive(value):
        if type(value) is not int or value < 1:
            raise CheckError("DATABASE_RESPONSE_INVALID")
        return value

    @staticmethod
    def _boolean(value):
        if type(value) is bool:
            return value
        if type(value) is int and value in (0, 1):
            return bool(value)
        raise CheckError("DATABASE_RESPONSE_INVALID")

    # -- notification log ---------------------------------------------
    def _capture_log(self, configured, baseline):
        capture = {"configured": configured is True, "missing": False, "text": "",
                   "inode": None, "size": 0, "overflow": False, "rotated": False,
                   "changed_during_read": False}
        if not capture["configured"]:
            return capture
        entry = self.env["entry"]["container"]
        path = self.env["entry"]["notify_log"]
        before = self._stat_log(entry, path)
        if before is None:
            capture["missing"] = True
            capture["rotated"] = (self._has_rotated(entry, path)
                                  or self._baseline_rotated(baseline, None, 0))
            return capture
        inode, size = before
        capture["inode"] = inode
        capture["size"] = size
        rotated = self._has_rotated(entry, path) or self._baseline_rotated(
            baseline, inode, size)
        if size > LOG_STDOUT_LIMIT:
            capture["overflow"] = True
            capture["rotated"] = rotated
            return capture
        try:
            result = self.runner.run(
                ["docker", "exec", entry, "cat", path],
                timeout=self.timeout, max_stdout=LOG_STDOUT_LIMIT)
        except StreamOverflow:
            capture["overflow"] = True
            capture["rotated"] = rotated
            return capture
        if result.returncode != 0:
            raise CheckError("LOG_READ_FAILED")
        after = self._stat_log(entry, path)
        if after is None:
            capture["changed_during_read"] = True
        else:
            capture["inode"], capture["size"] = after
            if after != before:
                capture["changed_during_read"] = True
        capture["rotated"] = rotated
        capture["text"] = redact_log_text(_text(result.stdout))
        return capture

    def _stat_log(self, entry, path):
        try:
            result = self.runner.run(
                ["docker", "exec", entry, "stat", "-c", "%i %s", path],
                timeout=self.timeout, max_stdout=4096)
        except StreamOverflow:
            raise CheckError("LOG_STATE_INVALID") from None
        if result.returncode != 0:
            return None
        parts = _text(result.stdout).split()
        if len(parts) != 2:
            raise CheckError("LOG_STATE_INVALID")
        try:
            inode, size = int(parts[0]), int(parts[1])
        except ValueError:
            raise CheckError("LOG_STATE_INVALID") from None
        if inode < 0 or size < 0:
            raise CheckError("LOG_STATE_INVALID")
        return inode, size

    def _has_rotated(self, entry, path):
        for suffix in (".1", ".2", ".gz", ".1.gz"):
            state = self._stat_log(entry, path + suffix)
            if state is not None and state[1] > 0:
                return True
        return False

    @staticmethod
    def _baseline_rotated(baseline, inode, size):
        if not baseline:
            return False
        state = baseline.get("log_state", {})
        if not isinstance(state, dict):
            return False
        previous_inode = state.get("inode")
        previous_size = state.get("size")
        if inode is None:
            return previous_inode is not None
        if type(previous_inode) is int and previous_inode != inode:
            return True
        if type(previous_size) is int and size < previous_size:
            return True
        return False
