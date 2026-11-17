"""SQL templates, safe value encoding and a statement-allowlist read-only guard.

Only bounded, read-only observation statements are generated here.  Values from
callers are always encoded as hex literals; identifiers and container names are
validated against fixed patterns before they reach a template.  The guard is a
statement allowlist, not a keyword blacklist alone.
"""
import json
import re

from .errors import CheckError


SQL_STDOUT_LIMIT = 1024 * 1024
MAX_SCRIPT_BYTES = 1024 * 1024
MAX_STATEMENTS = 200
MAX_MS = 253402271999999

HANDLE_RE = re.compile(r"^[A-Za-z0-9_-]{16,64}$")
CONTAINER_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
DATABASE_RE = re.compile(r"^[A-Za-z0-9_]{1,64}$")
ALIAS_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")
LOG_PATH_RE = re.compile(r"^/[A-Za-z0-9_./-]{1,255}$")
ISSUER_RE = re.compile(
    r"^https://[A-Za-z0-9](?:[A-Za-z0-9.-]{0,252})(?::[0-9]{1,5})?"
    r"(?:/[A-Za-z0-9._~!$&'()*+,;=:@%/-]*)?$"
)

FLOW_STATUSES = frozenset((
    "AWAITING_IDENTITY", "EXCHANGING", "IDENTITY_VERIFIED", "SESSION_ENDED",
    "IDENTITY_FAILED", "IDENTITY_REFUSED",
))
AUTHORIZATION_STATUSES = frozenset((
    "ACTIVE", "REFRESHING", "REFRESH_UNCERTAIN", "REAUTH_REQUIRED",
))
IDENTITY_BEARING_STATUSES = frozenset(("IDENTITY_VERIFIED", "SESSION_ENDED"))

# Every column created by the six customer identity changelogs, grouped by the
# compatible information_schema DATA_TYPE families.  Checking all six tables
# keeps a partial deployment from looking usable.
SCHEMA_REQUIRED = {
    "customer_device_request": (
        ("handle", "string"), ("device_id", "string"), ("purpose", "string"),
        ("permission_version", "integer"), ("expires_at_ms", "bigint"),
        ("consumed", "boolean"),
    ),
    "customer_browser": (
        ("id_hash", "string"), ("csrf", "string"), ("last_activity_ms", "bigint"),
    ),
    "customer_identity_flow": (
        ("id", "string"), ("browser_hash", "string"), ("request_handle", "string"),
        ("purpose", "string"), ("permission_version", "integer"),
        ("state_hash", "string"), ("nonce", "string"), ("verifier", "text"),
        ("expires_at_ms", "bigint"), ("status", "string"), ("created_at_ms", "bigint"),
        ("subject_key", "string"), ("issuer", "string"), ("subject", "string"),
        ("sid", "string"), ("identity_issued_at_ms", "bigint"),
        ("creation_order", "bigint"),
    ),
    "customer_authorization": (
        ("subject_key", "string"), ("generation", "string"), ("started_at_ms", "bigint"),
        ("issuer", "string"), ("subject", "string"), ("access_token", "text"),
        ("refresh_token", "text"), ("access_expires_at_ms", "bigint"),
        ("business_key", "text"), ("key_expires_at_ms", "bigint"), ("status", "string"),
        ("updated_at_ms", "bigint"), ("started_order", "bigint"),
    ),
    "customer_logout_receipt": (
        ("jti_hash", "string"), ("issuer", "string"), ("sid", "string"),
        ("subject", "string"), ("issued_at_ms", "bigint"), ("expires_at_ms", "bigint"),
        ("received_at_ms", "bigint"),
    ),
    "customer_identity_barrier": (
        ("id", "integer"),
    ),
}
_TYPE_FAMILIES = {
    "string": frozenset(("char", "varchar")),
    "bigint": frozenset(("bigint",)),
    "integer": frozenset(("int", "bigint")),
    "text": frozenset(("text", "mediumtext", "longtext")),
    "boolean": frozenset(("tinyint", "bit", "boolean")),
}
_MYSQL_ERRORS = {
    1044: "MYSQL_PERMISSION_DENIED",
    1045: "MYSQL_AUTH_FAILED",
    1049: "MYSQL_DATABASE_MISSING",
    1064: "MYSQL_SYNTAX_ERROR",
    1142: "MYSQL_PERMISSION_DENIED",
    1143: "MYSQL_PERMISSION_DENIED",
    1205: "MYSQL_TIMEOUT",
    1792: "MYSQL_READ_ONLY_VIOLATION",
    2002: "MYSQL_CONNECT_FAILED",
    2003: "MYSQL_CONNECT_FAILED",
    3024: "MYSQL_TIMEOUT",
}


def _invalid(code):
    raise CheckError(code)


def validate_handle(value):
    if not isinstance(value, str) or HANDLE_RE.fullmatch(value) is None:
        _invalid("HANDLE_INVALID")
    return value


def validate_container(value):
    if not isinstance(value, str) or CONTAINER_RE.fullmatch(value) is None:
        _invalid("CONTAINER_INVALID")
    return value


def validate_database(value):
    if not isinstance(value, str) or DATABASE_RE.fullmatch(value) is None:
        _invalid("DATABASE_INVALID")
    return value


def validate_alias(value):
    if not isinstance(value, str) or ALIAS_RE.fullmatch(value) is None:
        _invalid("NETWORK_ALIAS_INVALID")
    return value


def validate_log_path(value):
    if (not isinstance(value, str) or LOG_PATH_RE.fullmatch(value) is None
            or ".." in value.split("/")):
        _invalid("LOG_PATH_INVALID")
    return value


def validate_issuer(value):
    if (not isinstance(value, str) or not 8 <= len(value) <= 255
            or ISSUER_RE.fullmatch(value) is None):
        _invalid("ISSUER_INVALID")
    return value


def integer(value, minimum=0, maximum=MAX_MS):
    if type(value) is not int or not minimum <= value <= maximum:
        _invalid("NUMBER_INVALID")
    return str(value)


def _hex(value, charset):
    if not isinstance(value, str):
        _invalid("VALUE_INVALID")
    try:
        raw = value.encode("ascii" if charset == "ascii" else "utf-8")
    except UnicodeError:
        _invalid("VALUE_INVALID")
    return raw.hex().upper()


def ascii_literal(value):
    return "_ascii X'%s' COLLATE ascii_bin" % _hex(value, "ascii")


def literal(value):
    """Encode an arbitrary string without allowing it to change SQL structure."""
    return "_utf8mb4 X'%s' COLLATE utf8mb4_bin" % _hex(value, "utf8mb4")


def identifier(value):
    return "`%s`" % validate_database(value)


def _statements(script):
    if not isinstance(script, str) or "\x00" in script:
        _invalid("SQL_READ_ONLY_GUARD")
    if len(script.encode("utf-8")) > MAX_SCRIPT_BYTES:
        _invalid("SQL_READ_ONLY_GUARD")
    # Generated SQL never contains comments or executable comments.  Refusing
    # them prevents /*!...*/ payloads from hiding a second statement.
    if "--" in script or "#" in script or "/*" in script:
        _invalid("SQL_READ_ONLY_GUARD")
    result = []
    buffer = []
    quote = None
    index = 0
    while index < len(script):
        character = script[index]
        if quote is not None:
            buffer.append(character)
            if character == quote:
                if index + 1 < len(script) and script[index + 1] == quote:
                    buffer.append(script[index + 1])
                    index += 2
                    continue
                quote = None
        else:
            if character in ("'", '"', "`"):
                quote = character
                buffer.append(character)
            elif character == ";":
                statement = "".join(buffer).strip()
                if statement:
                    result.append(statement)
                buffer = []
            else:
                buffer.append(character)
        index += 1
    if quote is not None:
        _invalid("SQL_READ_ONLY_GUARD")
    tail = "".join(buffer).strip()
    if tail:
        result.append(tail)
    if not result or len(result) > MAX_STATEMENTS:
        _invalid("SQL_READ_ONLY_GUARD")
    return result


def _compact(statement):
    return re.sub(r"\s+", " ", statement).strip()


_SET_TEMPLATES = (
    re.compile(r"^SET SESSION MAX_EXECUTION_TIME = [0-9]{1,6}$", re.I),
    re.compile(r"^SET SESSION lock_wait_timeout = [0-9]{1,3}$", re.I),
    re.compile(r"^SET time_zone = '[+-][0-9]{2}:[0-9]{2}'$", re.I),
    re.compile(r"^SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ$", re.I),
)
_START = re.compile(
    r"^START TRANSACTION READ ONLY\s*,\s*WITH CONSISTENT SNAPSHOT$", re.I)
_USE = re.compile(r"^USE `[A-Za-z0-9_]{1,64}`$")
_SELECT_FORBIDDEN = (
    re.compile(r"\bFOR\s+(?:UPDATE|SHARE)\b", re.I),
    re.compile(r"\bLOCK\s+IN\s+SHARE\s+MODE\b", re.I),
    re.compile(r"\bINTO\b", re.I),
    re.compile(r":="),
    re.compile(r"\b(?:INSERT|UPDATE|DELETE|REPLACE|ALTER|CREATE|DROP|TRUNCATE|"
               r"GRANT|REVOKE|CALL|LOAD|HANDLER|DO)\b", re.I),
    re.compile(r"\bPROCEDURE\b", re.I),
    re.compile(r"\b(?:SLEEP|BENCHMARK)\s*\(", re.I),
    re.compile(r"\b(?:GET_LOCK|RELEASE_LOCK|RELEASE_ALL_LOCKS|IS_FREE_LOCK|"
               r"IS_USED_LOCK|LOAD_FILE)\s*\(", re.I),
)


def assert_read_only(script):
    """Accept only fixed statement shapes plus generated SELECT templates.

    Every SELECT here is produced by this module and contains no caller text.
    The extra blockers keep a future template mistake from reaching MySQL as a
    lock, file-read, variable-assignment or persistence statement.
    """
    for statement in _statements(script):
        compact = _compact(statement)
        first = compact.split(" ", 1)[0].upper()
        if first == "SET":
            if not any(template.fullmatch(compact) for template in _SET_TEMPLATES):
                _invalid("SQL_READ_ONLY_GUARD")
        elif first == "START":
            if _START.fullmatch(compact) is None:
                _invalid("SQL_READ_ONLY_GUARD")
        elif first == "USE":
            if _USE.fullmatch(compact) is None:
                _invalid("SQL_READ_ONLY_GUARD")
        elif first == "COMMIT":
            if compact.upper() != "COMMIT":
                _invalid("SQL_READ_ONLY_GUARD")
        elif first == "SELECT":
            for pattern in _SELECT_FORBIDDEN:
                if pattern.search(compact):
                    _invalid("SQL_READ_ONLY_GUARD")
        else:
            _invalid("SQL_READ_ONLY_GUARD")
    return script


def _prefix():
    return (
        "SET SESSION MAX_EXECUTION_TIME = 5000;\n"
        "SET SESSION lock_wait_timeout = 5;\n"
        "SET time_zone = '+00:00';\n"
        "SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ;\n"
    )


def build_schema_script(database):
    """Read-only version and information_schema column inventory."""
    database = validate_database(database)
    table_list = ", ".join(literal(table) for table in sorted(SCHEMA_REQUIRED))
    script = (
        _prefix()
        + "USE %s;\n" % identifier(database)
        + "START TRANSACTION READ ONLY, WITH CONSISTENT SNAPSHOT;\n"
        + "SELECT JSON_OBJECT('kind','version','version',VERSION());\n"
        + ("SELECT JSON_OBJECT('kind','column','table',TABLE_NAME,'name',COLUMN_NAME,"
           "'type',DATA_TYPE) FROM information_schema.COLUMNS "
           "WHERE TABLE_SCHEMA = %s AND TABLE_NAME IN (%s) "
           "ORDER BY TABLE_NAME, ORDINAL_POSITION LIMIT 501;\n")
        % (literal(database), table_list)
        + "COMMIT;\n"
    )
    return assert_read_only(script)


def _match_condition(flow_alias="f", receipt_alias="r"):
    return (
        "%(r)s.issuer = %(f)s.issuer AND "
        "((%(r)s.sid IS NOT NULL AND %(r)s.sid = %(f)s.sid) OR "
        "(%(r)s.sid IS NULL AND %(r)s.subject = %(f)s.subject)) AND "
        "%(f)s.created_at_ms <= LEAST(%(r)s.issued_at_ms + 999, %(r)s.received_at_ms)"
    ) % {"f": flow_alias, "r": receipt_alias}


def build_observation_script(database, issuer, handles, start_ms=None, end_ms=None,
                             since_ms=None):
    """Build one consistent-snapshot script for all bounded observation rows."""
    database = validate_database(database)
    issuer = validate_issuer(issuer)
    if not isinstance(handles, dict) or not handles or set(handles) - {"A", "B"}:
        _invalid("RECORDS_INVALID")
    if "A" not in handles:
        _invalid("RECORDS_INVALID")
    for handle in handles.values():
        validate_handle(handle)
    if (start_ms is None) != (end_ms is None):
        _invalid("WINDOW_INVALID")
    if start_ms is not None:
        integer(start_ms)
        integer(end_ms)
        if start_ms >= end_ms:
            _invalid("WINDOW_INVALID")
    if since_ms is not None:
        integer(since_ms)

    issuer_sql = literal(issuer)
    lines = [
        _prefix().rstrip("\n"),
        "USE %s;" % identifier(database),
        "START TRANSACTION READ ONLY, WITH CONSISTENT SNAPSHOT;",
        ("SELECT JSON_OBJECT('kind','observed','value',"
         "CAST(UNIX_TIMESTAMP(NOW(3))*1000 AS SIGNED));"),
    ]
    match = _match_condition()
    for label in ("A", "B"):
        if label not in handles:
            continue
        handle_sql = ascii_literal(handles[label])
        if start_ms is None:
            before_sql = "0"
            window_sql = "0"
        else:
            before_sql = ("(SELECT COUNT(*) FROM customer_logout_receipt r WHERE %s "
                          "AND r.received_at_ms < %s)" % (match, start_ms))
            window_sql = ("(SELECT COUNT(*) FROM customer_logout_receipt r WHERE %s "
                          "AND r.received_at_ms >= %s AND r.received_at_ms < %s)"
                          % (match, start_ms, end_ms))
        lines.append(
            "SELECT JSON_OBJECT("
            "'kind','flow','record','%s','order',f.creation_order,"
            "'status',f.status,'created_at_ms',f.created_at_ms,"
            "'expires_at_ms',f.expires_at_ms,"
            "'identity_issued_at_ms',f.identity_issued_at_ms,"
            "'sid_present',IF(f.sid IS NOT NULL,1,0),"
            "'identity_present',IF(f.subject_key IS NOT NULL,1,0),"
            "'issuer_matches',IF(f.issuer = %s,1,0),"
            "'authorization',COALESCE(a.status,'NONE'),"
            "'last_activity_ms',b.last_activity_ms,"
            "'matched_before_window',%s,"
            "'matched_in_window',%s) "
            "FROM customer_identity_flow f "
            "LEFT JOIN customer_authorization a ON a.subject_key = f.subject_key "
            "LEFT JOIN customer_browser b ON b.id_hash = f.browser_hash "
            "WHERE f.request_handle = %s "
            "ORDER BY f.creation_order LIMIT 51;"
            % (label, issuer_sql, before_sql, window_sql, handle_sql)
        )
        if start_ms is not None:
            lines.append(
                "SELECT JSON_OBJECT('kind','matched','record','%s',"
                "'order',f.creation_order,'received_at_ms',r.received_at_ms) "
                "FROM customer_identity_flow f "
                "JOIN customer_logout_receipt r ON %s "
                "WHERE f.request_handle = %s "
                "AND r.received_at_ms >= %s AND r.received_at_ms < %s "
                "ORDER BY f.creation_order, r.received_at_ms LIMIT 501;"
                % (label, match, handle_sql, start_ms, end_ms)
            )
    if start_ms is None:
        before_total = "0"
        window_total = "0"
    else:
        before_total = ("(SELECT COUNT(*) FROM customer_logout_receipt "
                        "WHERE issuer = %s AND received_at_ms < %s)" % (issuer_sql, start_ms))
        window_total = ("(SELECT COUNT(*) FROM customer_logout_receipt "
                        "WHERE issuer = %s AND received_at_ms >= %s AND received_at_ms < %s)"
                        % (issuer_sql, start_ms, end_ms))
    lines.append(
        "SELECT JSON_OBJECT('kind','receipts',"
        "'issuer_total',(SELECT COUNT(*) FROM customer_logout_receipt WHERE issuer = %s),"
        "'issuer_before_window',%s,"
        "'issuer_in_window',%s,"
        "'other_issuer_total',(SELECT COUNT(*) FROM customer_logout_receipt "
        "WHERE issuer <> %s));" % (issuer_sql, before_total, window_total, issuer_sql)
    )
    if start_ms is not None:
        lines.append(
            "SELECT JSON_OBJECT('kind','receipt_window','received_at_ms',r.received_at_ms) "
            "FROM customer_logout_receipt r WHERE r.issuer = %s "
            "AND r.received_at_ms >= %s AND r.received_at_ms < %s "
            "ORDER BY r.received_at_ms LIMIT 501;" % (issuer_sql, start_ms, end_ms)
        )
    if since_ms is not None:
        lines.append(
            "SELECT JSON_OBJECT('kind','since_baseline','value',"
            "(SELECT COUNT(*) FROM customer_logout_receipt WHERE issuer = %s "
            "AND received_at_ms > %s));" % (issuer_sql, since_ms)
        )
    if "B" in handles:
        lines.append(
            "SELECT JSON_OBJECT('kind','pair','a_order',a.creation_order,"
            "'b_order',b.creation_order,'distinct',"
            "IF(a.subject_key <> b.subject_key,1,0)) "
            "FROM customer_identity_flow a JOIN customer_identity_flow b "
            "ON b.request_handle = %s "
            "WHERE a.request_handle = %s "
            "AND a.subject_key IS NOT NULL AND b.subject_key IS NOT NULL "
            "ORDER BY a.creation_order, b.creation_order LIMIT 2501;"
            % (ascii_literal(handles["B"]), ascii_literal(handles["A"]))
        )
    lines.append("COMMIT;")
    return assert_read_only("\n".join(lines) + "\n")


def parse_json_rows(raw):
    """Parse one JSON object per output line; never echo a malformed line."""
    if isinstance(raw, bytes):
        try:
            text = raw.decode("utf-8")
        except UnicodeError:
            _invalid("DATABASE_RESPONSE_INVALID")
    elif isinstance(raw, str):
        text = raw
    else:
        _invalid("DATABASE_RESPONSE_INVALID")
    rows = []
    for line in text.splitlines():
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except (ValueError, RecursionError):
            _invalid("DATABASE_RESPONSE_INVALID")
        if not isinstance(row, dict) or not isinstance(row.get("kind"), str):
            _invalid("DATABASE_RESPONSE_INVALID")
        rows.append(row)
    return rows


def version_major(rows):
    for row in rows:
        if row.get("kind") == "version":
            value = row.get("version")
            if isinstance(value, str):
                match = re.match(r"^([0-9]{1,2})\.", value)
                if match:
                    return int(match.group(1))
    return None


def schema_mismatches(rows):
    """Return safe (table, column) pairs that are missing or wrongly typed."""
    observed = {}
    for row in rows:
        if row.get("kind") != "column":
            continue
        table, name, kind = row.get("table"), row.get("name"), row.get("type")
        if isinstance(table, str) and isinstance(name, str) and isinstance(kind, str):
            observed[(table, name)] = kind.lower()
    missing = []
    for table in sorted(SCHEMA_REQUIRED):
        for column, family in SCHEMA_REQUIRED[table]:
            kind = observed.get((table, column))
            if kind not in _TYPE_FAMILIES[family]:
                missing.append((table, column))
    return missing


def mysql_error_name(stderr):
    """Map only a four-digit MySQL error number to a fixed safe code."""
    if isinstance(stderr, bytes):
        data = stderr
    elif isinstance(stderr, str):
        data = stderr.encode("latin-1", "ignore")
    else:
        data = b""
    match = re.search(rb"(?:ERROR|error)\s+([0-9]{4})", data)
    if match:
        return _MYSQL_ERRORS.get(int(match.group(1)), "MYSQL_COMMAND_FAILED")
    if not data.strip():
        return "MYSQL_CREDENTIALS_MISSING"
    return "MYSQL_COMMAND_FAILED"


def normalize_flow_status(value):
    return value if value in FLOW_STATUSES else "UNKNOWN"


def normalize_authorization_status(value):
    return value if value == "NONE" or value in AUTHORIZATION_STATUSES else "UNKNOWN"


def receipt_matches(flow_issuer, flow_sid, flow_subject, flow_created_at_ms,
                    receipt_issuer, receipt_sid, receipt_subject,
                    receipt_issued_at_ms, receipt_received_at_ms):
    """Pure model of the SQL receipt-to-flow join used by the service."""
    if flow_issuer != receipt_issuer:
        return False
    if receipt_sid is not None:
        if flow_sid is None or receipt_sid != flow_sid:
            return False
    else:
        if flow_subject is None or receipt_subject is None or receipt_subject != flow_subject:
            return False
    return flow_created_at_ms <= min(receipt_issued_at_ms + 999, receipt_received_at_ms)


def receipt_matches_callback(flow_issuer, flow_sid, flow_subject, flow_created_at_ms,
                             receipt_issuer, receipt_sid, receipt_subject,
                             receipt_issued_at_ms, receipt_received_at_ms):
    """Pure model of the callback-time receipt lookup used by the service."""
    if flow_issuer != receipt_issuer:
        return False
    if receipt_sid is not None:
        if flow_sid is None or receipt_sid != flow_sid:
            return False
    else:
        if flow_subject is None or receipt_subject is None or receipt_subject != flow_subject:
            return False
    return (flow_created_at_ms <= receipt_received_at_ms
            and receipt_issued_at_ms >= (flow_created_at_ms // 1000) * 1000)
