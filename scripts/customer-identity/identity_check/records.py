"""Bounded private JSON inputs. Values and paths never become error messages."""
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import re
import stat

from .errors import CheckError


MAX_BYTES = 1048576
_HANDLE = re.compile(r"[A-Za-z0-9_-]{16,64}")


@dataclass(frozen=True)
class RoundInput:
    label: str
    handle: str = field(repr=False)
    fingerprint: str = field(repr=False)
    path: Path = field(repr=False)


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise CheckError("INPUT_DUPLICATE_FIELD")
        result[key] = value
    return result


def decode_json(raw):
    try:
        result = json.loads(raw.decode("utf-8"), object_pairs_hook=_object,
                            parse_constant=lambda value: (_ for _ in ()).throw(ValueError()))
    except (UnicodeError, ValueError, RecursionError):
        raise CheckError("INPUT_INVALID_JSON") from None
    if not isinstance(result, dict):
        raise CheckError("INPUT_INVALID_STRUCTURE")
    return result


def private_json(name, root=None):
    """Read the opened regular file, not a separately re-opened pathname."""
    fd = None
    try:
        path = Path(name).absolute()
        if root is not None:
            base = Path(root).resolve(strict=True)
            try:
                path.resolve(strict=True).relative_to(base)
            except ValueError:
                raise CheckError("INPUT_OUTSIDE_ROOT") from None
        for component in (path,) + tuple(path.parents):
            if component.is_symlink():
                raise CheckError("INPUT_SYMLINK")
        fd = os.open(str(path), os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
                     | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0))
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise CheckError("INPUT_NOT_REGULAR")
        if os.name != "nt":
            allowed = {os.geteuid()}
            if os.geteuid() == 0 and os.environ.get("SUDO_UID", "").isdigit():
                allowed.add(int(os.environ["SUDO_UID"]))
            if info.st_mode & 0o077 or info.st_uid not in allowed:
                raise CheckError("INPUT_NOT_PRIVATE")
        if info.st_size > MAX_BYTES:
            raise CheckError("INPUT_TOO_LARGE")
        raw = bytearray()
        while len(raw) <= MAX_BYTES:
            chunk = os.read(fd, min(65536, MAX_BYTES + 1 - len(raw)))
            if not chunk:
                break
            raw.extend(chunk)
        if len(raw) > MAX_BYTES:
            raise CheckError("INPUT_TOO_LARGE")
        after = os.fstat(fd)
        if (info.st_size, info.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise CheckError("INPUT_CHANGED_DURING_READ")
        return decode_json(bytes(raw)), hashlib.sha256(raw).hexdigest()
    except CheckError:
        raise
    except (OSError, ValueError, TypeError):
        raise CheckError("INPUT_UNAVAILABLE") from None
    finally:
        if fd is not None:
            os.close(fd)


def load_round(path, label, root):
    if label not in ("A", "B"):
        raise CheckError("INPUT_LABEL_INVALID")
    data, fingerprint = private_json(path, root)
    if set(data) != {"schema", "request_handle"} or data.get("schema") != "xiaozhi-identity-round-v1":
        raise CheckError("RECORD_FORMAT_UNSUPPORTED")
    handle = data.get("request_handle")
    if not isinstance(handle, str) or not _HANDLE.fullmatch(handle):
        raise CheckError("RECORD_HANDLE_INVALID")
    return RoundInput(label, handle, fingerprint, Path(path))


def record_fingerprints(records):
    return {label: record.fingerprint for label, record in records.items()}
