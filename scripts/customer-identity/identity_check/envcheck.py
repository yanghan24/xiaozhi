"""Environment-file normalization and fixed, bounded Docker topology checks.

Runner interface (duck typed, implemented by source.LocalRunner and test fakes):

    run(argv, input_text=None, timeout=None, max_stdout=None)
        -> object with returncode, stdout (bytes), stderr (bytes)

    mysql(script, container, timeout=None, max_stdout=None)
        -> the same result object

No command output, environment value, config body or parser exception is ever
placed in a CheckError.  Only fixed codes leave this module.
"""
import json
import os
import re
import stat
import time

from .errors import CheckError
from . import sql


ENV_SCHEMA = "xiaozhi-identity-env-v1"
ENV_TOTAL_TIMEOUT = 60.0
MAX_ENV_BYTES = 64 * 1024
MAX_CONFIG_BYTES = 256 * 1024
MAX_PEERS = 100
MAX_SMALL_OUTPUT = 64 * 1024
_MARKER_VALUE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 ._-]{2,63}$")
_PATH_RE = re.compile(r"^/[A-Za-z0-9._/-]{1,1023}$")
_JDBC_RE = re.compile(
    r"^jdbc:mysql://([A-Za-z0-9][A-Za-z0-9_.-]{0,127})(?::([0-9]{1,5}))?"
    r"/([A-Za-z0-9_]{1,64})(?:\?[^\s]*)?$")
_PROXY_RE = re.compile(
    r"^(https?)://([A-Za-z0-9][A-Za-z0-9_.-]{0,127})(?::([0-9]{1,5}))?"
    r"(/[^\s]*)?$", re.I)
_UPSTREAM_START = re.compile(
    r"(?m)^[ \t]*upstream[ \t]+([A-Za-z0-9_.-]{1,128})[ \t]*\{")
_SERVER_RE = re.compile(r"(?m)(?:^|(?<=[;{}]))[ \t]*server[ \t]+([^;{}\r\n]+);")
_PROXY_DIRECTIVE = re.compile(
    r"^\s*proxy_pass\s+([^;]+);\s*(?:#.*)?$", re.I)
_ACCESS_DIRECTIVE = re.compile(
    r"^\s*access_log\s+([^;]+);\s*(?:#.*)?$", re.I)


def _object_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError()
        result[key] = value
    return result


def _absolute_path(value):
    if (not isinstance(value, str) or _PATH_RE.fullmatch(value) is None
            or ".." in value.split("/")):
        raise CheckError("ENV_INVALID")
    return value


def _field(value, validator):
    try:
        return validator(value)
    except CheckError:
        raise CheckError("ENV_INVALID") from None


def load_environment(path):
    """Read one bounded, allowlisted environment file.  The path is not echoed."""
    try:
        with open(path, "rb") as stream:
            raw = stream.read(MAX_ENV_BYTES + 1)
    except FileNotFoundError:
        raise CheckError("ENV_FILE_MISSING") from None
    except (OSError, TypeError, ValueError):
        raise CheckError("ENV_FILE_INVALID") from None
    if len(raw) > MAX_ENV_BYTES:
        raise CheckError("ENV_FILE_INVALID")
    try:
        data = json.loads(raw.decode("utf-8"), object_pairs_hook=_object_pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(ValueError()))
    except (UnicodeError, ValueError, RecursionError):
        raise CheckError("ENV_FILE_INVALID") from None
    return validate_environment(data)


def validate_environment(data):
    """Return only the fixed environment schema, dropping every extra key."""
    if not isinstance(data, dict):
        raise CheckError("ENV_INVALID")
    if data.get("schema") != ENV_SCHEMA:
        raise CheckError("ENV_SCHEMA_UNSUPPORTED")
    issuer = _field(data.get("issuer"), sql.validate_issuer)
    record_root = _absolute_path(data.get("record_root"))
    database = data.get("db")
    if not isinstance(database, dict):
        raise CheckError("ENV_INVALID")
    db_container = _field(database.get("container"), sql.validate_container)
    db_name = _field(database.get("database"), sql.validate_database)
    db_alias = _field(database.get("network_alias"), sql.validate_alias)
    identity_container = _field(data.get("identity_container"), sql.validate_container)
    entry = data.get("entry")
    if not isinstance(entry, dict):
        raise CheckError("ENV_INVALID")
    entry_container = _field(entry.get("container"), sql.validate_container)
    notify_log = _field(entry.get("notify_log"), sql.validate_log_path)
    config_file = _absolute_path(entry.get("config_file"))
    marker = entry.get("marker")
    if marker is not None and (not isinstance(marker, str)
                               or _MARKER_VALUE.fullmatch(marker) is None):
        raise CheckError("ENV_INVALID")
    if len({db_container, identity_container, entry_container}) != 3:
        raise CheckError("ENV_INVALID")
    return {
        "schema": ENV_SCHEMA,
        "issuer": issuer,
        "record_root": record_root,
        "db": {"container": db_container, "database": db_name,
               "network_alias": db_alias},
        "identity_container": identity_container,
        "entry": {"container": entry_container, "notify_log": notify_log,
                  "config_file": config_file, "marker": marker},
    }


class _Deadline(object):
    def __init__(self, seconds=ENV_TOTAL_TIMEOUT):
        self.seconds = seconds
        self.end = None

    def remaining(self):
        if self.end is None:
            self.end = time.monotonic() + self.seconds
        return self.end - time.monotonic()


def _call(deadline, function, timeout, *args, **kwargs):
    remaining = deadline.remaining()
    if remaining <= 0:
        raise CheckError("ENV_CHECK_TIMEOUT")
    kwargs["timeout"] = min(float(timeout), remaining)
    try:
        return function(*args, **kwargs)
    except CheckError:
        raise
    except Exception:
        raise CheckError("ENV_CHECK_FAILED") from None


def _text(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    if isinstance(value, str):
        return value
    return ""


def _run(env, runner, argv, deadline, timeout, max_stdout=MAX_SMALL_OUTPUT):
    return _call(deadline, runner.run, timeout, argv, max_stdout=max_stdout)


def _mysql(env, runner, script, deadline, timeout):
    return _call(deadline, runner.mysql, timeout, script, env["db"]["container"],
                 max_stdout=sql.SQL_STDOUT_LIMIT)


def _inspect(env, runner, name, fmt, deadline, timeout, max_stdout=MAX_SMALL_OUTPUT):
    result = _run(env, runner, ["docker", "inspect", "--format", fmt, name],
                  deadline, timeout, max_stdout=max_stdout)
    if result.returncode != 0:
        raise CheckError("ENV_INSPECT_FAILED")
    return _text(result.stdout).strip()


def _json_inspect(env, runner, name, fmt, deadline, timeout):
    text = _inspect(env, runner, name, fmt, deadline, timeout)
    try:
        value = json.loads(text)
    except (ValueError, RecursionError):
        raise CheckError("ENV_INSPECT_FAILED") from None
    return value


def _networks(env, runner, name, deadline, timeout):
    value = _json_inspect(env, runner, name, "{{json .NetworkSettings.Networks}}",
                          deadline, timeout)
    if not isinstance(value, dict):
        raise CheckError("ENV_INSPECT_FAILED")
    return value


def _aliases(network):
    aliases = network.get("Aliases") if isinstance(network, dict) else None
    return aliases if isinstance(aliases, list) else []


def _container_running(env, runner, name, deadline, timeout):
    result = _run(env, runner, ["docker", "inspect", "--format",
                                "{{json .State.Running}}", name],
                  deadline, timeout)
    if result.returncode != 0:
        raise CheckError("CONTAINER_NOT_RUNNING")
    try:
        running = json.loads(_text(result.stdout).strip())
    except (ValueError, RecursionError):
        raise CheckError("CONTAINER_NOT_RUNNING") from None
    if running is not True:
        raise CheckError("CONTAINER_NOT_RUNNING")


def _check_containers(env, runner, deadline, timeout):
    result = _run(env, runner, ["docker", "version", "--format", "{{.Server.Version}}"],
                  deadline, timeout)
    if result.returncode != 0 or not _text(result.stdout).strip():
        raise CheckError("DOCKER_UNAVAILABLE")
    for name in (env["db"]["container"], env["identity_container"],
                 env["entry"]["container"]):
        _container_running(env, runner, name, deadline, timeout)


def _check_db_alias(env, runner, deadline, timeout):
    alias = env["db"]["network_alias"]
    db_networks = _networks(env, runner, env["db"]["container"], deadline, timeout)
    identity_networks = _networks(env, runner, env["identity_container"],
                                  deadline, timeout)
    shared = [network for network in db_networks
              if network in identity_networks and alias in _aliases(db_networks[network])]
    if not shared:
        raise CheckError("DB_TARGET_MISMATCH")
    if len(shared) != 1:
        raise CheckError("DB_ALIAS_AMBIGUOUS")
    network = shared[0]
    if alias in _aliases(identity_networks[network]):
        raise CheckError("DB_ALIAS_AMBIGUOUS")
    listing = _run(env, runner, ["docker", "ps", "--format", "{{.Names}}"],
                   deadline, timeout)
    if listing.returncode != 0:
        raise CheckError("ENV_INSPECT_FAILED")
    peers = [name.strip() for name in _text(listing.stdout).splitlines() if name.strip()]
    if len(peers) > MAX_PEERS:
        raise CheckError("ENV_PEER_LIMIT")
    for peer in peers:
        if peer in (env["db"]["container"], env["identity_container"]):
            continue
        if sql.CONTAINER_RE.fullmatch(peer) is None:
            raise CheckError("ENV_INSPECT_FAILED")
        networks = _networks(env, runner, peer, deadline, timeout)
        if network in networks and alias in _aliases(networks[network]):
            raise CheckError("DB_ALIAS_AMBIGUOUS")


def _check_jdbc(env, runner, deadline, timeout):
    result = _run(env, runner,
                  ["docker", "exec", env["identity_container"], "printenv",
                   "SPRING_DATASOURCE_DRUID_URL"],
                  deadline, timeout)
    if result.returncode != 0:
        raise CheckError("DB_TARGET_MISMATCH")
    value = _text(result.stdout).strip()
    match = _JDBC_RE.fullmatch(value)
    if match is None:
        raise CheckError("DB_TARGET_MISMATCH")
    host, port, database = match.groups()
    if host != env["db"]["network_alias"] or database != env["db"]["database"]:
        raise CheckError("DB_TARGET_MISMATCH")
    # The collector uses mysql's default TCP port; a JDBC URL on another port
    # would describe a different target than the one actually queried.
    if port is not None and int(port) != 3306:
        raise CheckError("DB_TARGET_MISMATCH")


def _norm_posix(path):
    parts = []
    for part in path.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            return None
        parts.append(part)
    return "/" + "/".join(parts)


def _check_mounted(env, runner, deadline, timeout):
    # Assumption documented in the runbook: entry.config_file is the host path
    # bind-mounted as the active entry nginx configuration.  This proves the
    # mount exists; it cannot prove nginx's include graph references it without
    # dumping the active config, so an unused mounted file is an unsupported
    # layout rather than an inferred pass.
    mounts = _json_inspect(env, runner, env["entry"]["container"],
                           "{{json .Mounts}}", deadline, timeout)
    if not isinstance(mounts, list):
        raise CheckError("PROXY_CONFIG_NOT_MOUNTED")
    config = _norm_posix(env["entry"]["config_file"])
    for mount in mounts:
        if not isinstance(mount, dict) or not isinstance(mount.get("Source"), str):
            continue
        source = _norm_posix(mount["Source"])
        if source and config and (config == source or config.startswith(source + "/")):
            return
    raise CheckError("PROXY_CONFIG_NOT_MOUNTED")


def _read_host_text(path, limit):
    fd = None
    try:
        fd = os.open(path, os.O_RDONLY | getattr(os, "O_BINARY", 0))
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise CheckError("PROXY_CONFIG_UNREADABLE")
        if info.st_size > limit:
            raise CheckError("PROXY_CONFIG_TOO_LARGE")
        data = bytearray()
        while len(data) <= limit:
            chunk = os.read(fd, min(65536, limit + 1 - len(data)))
            if not chunk:
                break
            data.extend(chunk)
        if len(data) > limit:
            raise CheckError("PROXY_CONFIG_TOO_LARGE")
        return bytes(data).decode("utf-8")
    except CheckError:
        raise
    except FileNotFoundError:
        raise CheckError("PROXY_CONFIG_MISSING") from None
    except (OSError, UnicodeError, ValueError):
        raise CheckError("PROXY_CONFIG_UNREADABLE") from None
    finally:
        if fd is not None:
            os.close(fd)


NOTIFY_LOCATION = "/xiaozhi/customer/oidc/backchannel-logout"
_LOCATION_START = re.compile(r"(?m)^[ \t]*location[ \t]+([^\n{]+?)[ \t]*\{")


def _brace_block(text, brace_index):
    depth = 0
    for index in range(brace_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace_index + 1:index]
    raise CheckError("PROXY_TARGET_AMBIGUOUS")


def _locations(text):
    for match in _LOCATION_START.finditer(text):
        yield (" ".join(match.group(1).split()), _brace_block(text, match.end() - 1))


def _notify_location(args):
    parts = args.split()
    if len(parts) == 2 and parts[0] == "=":
        return _unquote(parts[1]) == NOTIFY_LOCATION
    if len(parts) == 1:
        return _unquote(parts[0]) == NOTIFY_LOCATION
    return False


def _marker_text(text, marker):
    begin = re.compile(r"^\s*#\s*BEGIN\s+" + re.escape(marker) + r"\s*$", re.I)
    end = re.compile(r"^\s*#\s*END\s+" + re.escape(marker) + r"\s*$", re.I)
    lines = text.splitlines()
    begins = [index for index, line in enumerate(lines) if begin.fullmatch(line)]
    ends = [index for index, line in enumerate(lines) if end.fullmatch(line)]
    if not begins and not ends:
        raise CheckError("PROXY_TARGET_MISMATCH")
    if len(begins) != 1 or len(ends) != 1 or begins[0] >= ends[0]:
        raise CheckError("PROXY_TARGET_AMBIGUOUS")
    return "\n".join(lines[begins[0] + 1:ends[0]])


def _enclosing_notify_location(text, marker):
    """True when the single marker block sits inside the exact notify location."""
    begin = re.compile(r"(?m)^[ \t]*#[ \t]*BEGIN[ \t]+" + re.escape(marker) + r"[ \t]*$", re.I)
    found = begin.search(text)
    if found is None:
        return False
    position = found.start()
    for match in _LOCATION_START.finditer(text):
        brace = match.end() - 1
        if brace >= position:
            break
        inner = _brace_block(text, brace)
        if brace + 1 + len(inner) > position:
            return _notify_location(" ".join(match.group(1).split()))
    return False


def _select_notify_location(text, marker):
    """Select the exact notification location, never the login route."""
    if marker:
        block = _marker_text(text, marker)
        matches = [inner for args, inner in _locations(block) if _notify_location(args)]
        if len(matches) > 1:
            raise CheckError("PROXY_TARGET_AMBIGUOUS")
        if len(matches) == 1:
            return matches[0]
        # A marker may be nested inside the notification location; a proxy
        # directive alone does not show which route the block belongs to.
        if (_directives(block.splitlines(), _PROXY_DIRECTIVE)
                and not list(_locations(block))
                and _enclosing_notify_location(text, marker)):
            return block
        raise CheckError("PROXY_TARGET_MISMATCH")
    matches = [inner for args, inner in _locations(text) if _notify_location(args)]
    if not matches:
        raise CheckError("PROXY_TARGET_MISMATCH")
    if len(matches) != 1:
        raise CheckError("PROXY_TARGET_AMBIGUOUS")
    return matches[0]


def _directives(lines, pattern):
    values = []
    for line in lines:
        if line.lstrip().startswith("#"):
            continue
        match = pattern.fullmatch(line)
        if match:
            values.append(match.group(1).strip())
    return values


def _unquote(value):
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        return value[1:-1]
    return value


def _upstreams(text):
    result = {}
    for match in _UPSTREAM_START.finditer(text):
        brace = match.end() - 1
        result.setdefault(match.group(1), []).append(_brace_block(text, brace))
    return result


def _upstream_target(blocks, identity_names, allowed_ports):
    servers = []
    for block in blocks:
        for match in _SERVER_RE.finditer(block):
            args = match.group(1).split()
            if args:
                servers.append(args[0])
    if len(servers) != 1:
        raise CheckError("PROXY_TARGET_AMBIGUOUS")
    target = servers[0]
    if "$" in target or target.startswith("unix:") or target.startswith("["):
        raise CheckError("PROXY_TARGET_MISMATCH")
    match = re.fullmatch(r"([A-Za-z0-9][A-Za-z0-9_.-]*)(?::([0-9]{1,5}))?", target)
    if match is None or match.group(1) not in identity_names:
        raise CheckError("PROXY_TARGET_MISMATCH")
    # An upstream server without a port defaults to 80 regardless of the
    # proxy_pass scheme (nginx upstream module).
    port = int(match.group(2)) if match.group(2) else 80
    if not 1 <= port <= 65535 or port not in allowed_ports:
        raise CheckError("PROXY_TARGET_MISMATCH")
    return match.group(1)


def _parse_entry_config(text, env, identity_names, allowed_ports):
    """Return (log configured, the host name nginx will resolve)."""
    block = _select_notify_location(text, env["entry"].get("marker"))
    access_logs = _directives(block.splitlines(), _ACCESS_DIRECTIVE)
    configured = any(_unquote(value.split()[0]) == env["entry"]["notify_log"]
                     for value in access_logs if value.split())
    proxies = _directives(block.splitlines(), _PROXY_DIRECTIVE)
    if len(proxies) != 1:
        raise CheckError("PROXY_TARGET_AMBIGUOUS")
    target = proxies[0]
    if "$" in target or len(target.split()) != 1:
        raise CheckError("PROXY_TARGET_MISMATCH")
    match = _PROXY_RE.fullmatch(target)
    if match is None:
        raise CheckError("PROXY_TARGET_MISMATCH")
    scheme, host, port_text = match.group(1).lower(), match.group(2), match.group(3)
    upstreams = _upstreams(text)
    if host in upstreams:
        # A named upstream is referenced without a port; its member decides
        # the real target and port.
        if port_text:
            raise CheckError("PROXY_TARGET_MISMATCH")
        resolved = _upstream_target(upstreams[host], identity_names, allowed_ports)
        return configured, resolved
    port = int(port_text) if port_text else (443 if scheme == "https" else 80)
    if not 1 <= port <= 65535 or port not in allowed_ports:
        raise CheckError("PROXY_TARGET_MISMATCH")
    if host not in identity_names:
        raise CheckError("PROXY_TARGET_MISMATCH")
    return configured, host


def _identity_reachability(env, runner, deadline, timeout):
    identity_networks = _networks(env, runner, env["identity_container"],
                                  deadline, timeout)
    entry_networks = _networks(env, runner, env["entry"]["container"],
                               deadline, timeout)
    shared = set(identity_networks) & set(entry_networks)
    if not shared:
        raise CheckError("PROXY_TARGET_MISMATCH")
    names = {env["identity_container"]}
    for network in shared:
        for alias in _aliases(identity_networks[network]):
            if isinstance(alias, str) and alias:
                names.add(alias)
    exposed = _inspect(env, runner, env["identity_container"],
                       "{{json .Config.ExposedPorts}}", deadline, timeout)
    try:
        value = json.loads(exposed) if exposed else None
    except (ValueError, RecursionError):
        raise CheckError("ENV_INSPECT_FAILED") from None
    ports = set()
    if isinstance(value, dict):
        for key in value:
            if isinstance(key, str) and key.endswith("/tcp"):
                number = key[:-4]
                if number.isdigit() and 1 <= int(number) <= 65535:
                    ports.add(int(number))
    if not ports:
        raise CheckError("PROXY_TARGET_MISMATCH")
    return frozenset(names), frozenset(ports)


def _entry_configuration(env, runner, deadline, timeout, identity_names, allowed_ports):
    _check_mounted(env, runner, deadline, timeout)
    text = _read_host_text(env["entry"]["config_file"], MAX_CONFIG_BYTES)
    configured, host = _parse_entry_config(text, env, identity_names, allowed_ports)
    _check_proxy_host_unique(env, runner, deadline, timeout, host)
    return {"configured": configured}


def _check_proxy_host_unique(env, runner, deadline, timeout, host):
    """No other container may answer to the proxy host on an entry network.

    Docker DNS on a user network returns every container carrying the alias,
    so a duplicate could route the notification to another backend.
    """
    entry_networks = set(_networks(env, runner, env["entry"]["container"],
                                   deadline, timeout))
    listing = _run(env, runner, ["docker", "ps", "--format", "{{.Names}}"],
                   deadline, timeout)
    if listing.returncode != 0:
        raise CheckError("ENV_INSPECT_FAILED")
    peers = [name.strip() for name in _text(listing.stdout).splitlines() if name.strip()]
    if len(peers) > MAX_PEERS:
        raise CheckError("ENV_PEER_LIMIT")
    for peer in peers:
        if peer == env["identity_container"]:
            continue
        if sql.CONTAINER_RE.fullmatch(peer) is None:
            raise CheckError("ENV_INSPECT_FAILED")
        networks = _networks(env, runner, peer, deadline, timeout)
        for network in entry_networks & set(networks):
            if peer == host or host in _aliases(networks[network]):
                raise CheckError("PROXY_TARGET_AMBIGUOUS")


def _check_schema(env, runner, deadline, timeout):
    script = sql.build_schema_script(env["db"]["database"])
    result = _mysql(env, runner, script, deadline, timeout)
    if getattr(result, "overflow", False):
        raise CheckError("SQL_OUTPUT_OVERFLOW")
    if result.returncode != 0:
        raise CheckError(sql.mysql_error_name(getattr(result, "stderr", b"")))
    rows = sql.parse_json_rows(result.stdout)
    if sql.version_major(rows) != 8:
        raise CheckError("DB_VERSION_UNSUPPORTED")
    if sql.schema_mismatches(rows):
        raise CheckError("SCHEMA_MISMATCH")


def check_environment(env, runner, timeout=20):
    """Return {'containers','db_target','proxy_target','schema'} or raise."""
    env = validate_environment(env)
    deadline = _Deadline(ENV_TOTAL_TIMEOUT)
    _check_containers(env, runner, deadline, timeout)
    _check_db_alias(env, runner, deadline, timeout)
    _check_jdbc(env, runner, deadline, timeout)
    identity_names, allowed_ports = _identity_reachability(
        env, runner, deadline, timeout)
    _entry_configuration(env, runner, deadline, timeout,
                         identity_names, allowed_ports)
    _check_schema(env, runner, deadline, timeout)
    return {"containers": "ok", "db_target": "ok",
            "proxy_target": "ok", "schema": "ok"}


def entry_log_configured(env, runner, timeout=20):
    """Whether the trusted notify location still configures the log."""
    env = validate_environment(env)
    deadline = _Deadline(ENV_TOTAL_TIMEOUT)
    identity_names, allowed_ports = _identity_reachability(
        env, runner, deadline, timeout)
    configuration = _entry_configuration(
        env, runner, deadline, timeout, identity_names, allowed_ports)
    return configuration["configured"] is True
