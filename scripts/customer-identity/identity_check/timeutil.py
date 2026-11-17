"""Integer-millisecond timestamps, compatible with Python 3.8.

The module deliberately implements its own strict RFC 3339 subset instead of
delegating fractional seconds to ``datetime.fromisoformat``: the 3.8 parser
only accepts the exact shapes produced by ``isoformat``.  Parsing here accepts
one to six fractional digits and floors the result to whole milliseconds.
"""
from datetime import datetime, timedelta, timezone
import re


UTC = timezone.utc
HONG_KONG = timezone(timedelta(hours=8), "Asia/Hong_Kong")
EPOCH = datetime(1970, 1, 1, tzinfo=UTC)
MAX_MS = 253402271999999
_TIMESTAMP = re.compile(
    r"^(?P<year>\d{4})-(?P<month>\d{2})-(?P<day>\d{2})"
    r"T(?P<hour>\d{2}):(?P<minute>\d{2}):(?P<second>\d{2})"
    r"(?:\.(?P<fraction>\d{1,6}))?"
    r"(?P<zone>Z|(?P<sign>[+-])(?P<offset_hour>\d{2}):(?P<offset_minute>\d{2}))$"
)


def millis(value):
    """Return a validated epoch-millisecond integer, rejecting bools."""
    if type(value) is not int or not 0 <= value <= MAX_MS:
        raise ValueError("Invalid millisecond timestamp")
    return value


def _offset(sign, hours, minutes):
    if hours > 23 or minutes > 59:
        raise ValueError("Invalid timezone offset")
    delta = timedelta(hours=hours, minutes=minutes)
    return delta if sign == "+" else -delta


def parse_timestamp(value):
    """Parse a timestamp with an explicit zone into epoch milliseconds.

    Accepted shape: ``YYYY-MM-DDTHH:MM:SS[.ffffff](Z|+HH:MM|-HH:MM)``.
    Naive timestamps, offsets beyond 23:59, and values before 1970 are
    rejected.  Fractional seconds are truncated to milliseconds.
    """
    if not isinstance(value, str):
        raise ValueError("Timestamp must be a string")
    match = _TIMESTAMP.fullmatch(value)
    if match is None:
        raise ValueError("Timestamp must include seconds and a timezone")
    fraction = match.group("fraction") or ""
    microsecond = int(fraction.ljust(6, "0")) if fraction else 0
    zone = match.group("zone")
    if zone == "Z":
        offset = timedelta(0)
    else:
        offset = _offset(
            match.group("sign"),
            int(match.group("offset_hour")),
            int(match.group("offset_minute")),
        )
    try:
        instant = datetime(
            int(match.group("year")),
            int(match.group("month")),
            int(match.group("day")),
            int(match.group("hour")),
            int(match.group("minute")),
            int(match.group("second")),
            microsecond,
            tzinfo=timezone(offset),
        )
        delta = instant - EPOCH
        total = delta.days * 86400000 + delta.seconds * 1000 + delta.microseconds // 1000
        return millis(total)
    except (OverflowError, ValueError):
        raise ValueError("Invalid timestamp") from None


def format_timestamp(value, zone=UTC):
    """Format epoch milliseconds as UTC ``Z`` or an explicit numeric offset."""
    try:
        instant = (EPOCH + timedelta(milliseconds=millis(value))).astimezone(zone)
    except (OverflowError, TypeError, ValueError):
        raise ValueError("Invalid timestamp") from None
    text = instant.isoformat(timespec="milliseconds")
    return text[:-6] + "Z" if zone == UTC else text


def describe_time(value):
    return {"utc": format_timestamp(value), "hong_kong": format_timestamp(value, HONG_KONG)}


def validate_window(start, end):
    """Validate a left-closed, right-open window and return it."""
    millis(start)
    millis(end)
    if start >= end:
        raise ValueError("Window start must precede end")
    return start, end


def in_window(value, start, end):
    """True when ``value`` is in the left-closed, right-open window."""
    return start <= value < end


def second_in_window(second_ms, start, end):
    """Classify a second-resolution log line against a millisecond window.

    The log line covers ``[second_ms, second_ms + 1000)``.  Only a line fully
    inside the window is ``inside``.  A line with partial overlap is
    ``boundary`` and must not be counted.  No overlap is ``outside``.
    """
    millis(second_ms)
    validate_window(start, end)
    if second_ms >= start and second_ms + 1000 <= end:
        return "inside"
    if second_ms < end and second_ms + 1000 > start:
        return "boundary"
    return "outside"


def clock_skew(observed, before, after, tolerance=1000):
    """Return signed milliseconds by which ``observed`` misses the host range.

    ``observed`` should be the database clock reading; ``before`` and ``after``
    are host-clock readings taken before and after the query.  The allowed
    interval is ``[before - tolerance, after + tolerance]`` clamped to the
    representable millisecond range.  Zero means within tolerance.  A positive
    result means the observed clock is ahead; a negative result means behind.
    """
    millis(observed)
    millis(before)
    millis(after)
    if type(tolerance) is not int or tolerance < 0:
        raise ValueError("Invalid tolerance")
    if before > after:
        raise ValueError("Host clock moved backwards")
    lower = max(0, before - tolerance)
    upper = min(MAX_MS, after + tolerance)
    if observed < lower:
        return observed - lower
    if observed > upper:
        return observed - upper
    return 0
