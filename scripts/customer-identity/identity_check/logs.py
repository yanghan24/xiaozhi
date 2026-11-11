"""Parse bounded notification logs without copying addresses or raw lines."""
from datetime import datetime, timedelta, timezone
import re

from .timeutil import parse_timestamp, second_in_window


_LINE = re.compile(
    r"^(?P<time>\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,3})?(?:Z|[+-]\d{2}:\d{2}))"
    r"\s+\S+\s+(?P<method>GET|POST|HEAD|OPTIONS)\s+(?P<status>\d{3})\s+(?P<upstream>\d{3}|-)\s*$"
)
_NGINX = re.compile(
    r"^\[(?P<time>\d{2}/[A-Za-z]{3}/\d{4}:\d{2}:\d{2}:\d{2} [+-]\d{4})\]"
    r"\s+\S+\s+(?P<method>GET|POST|HEAD|OPTIONS)\s+(?P<status>\d{3})\s+(?P<upstream>\d{3}|-)\s*$"
)
_MONTHS = {name: index + 1 for index, name in enumerate(
    ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"))}


def _timestamp(match, nginx=False):
    if not nginx:
        return parse_timestamp(match.group("time"))
    text = match.group("time")
    day, month, year, hour, minute, second, sign, offset_hour, offset_minute = re.fullmatch(
        r"(\d{2})/([A-Za-z]{3})/(\d{4}):(\d{2}):(\d{2}):(\d{2}) ([+-])(\d{2})(\d{2})", text).groups()
    offset = int(offset_hour) * 60 + int(offset_minute)
    if int(offset_hour) > 23 or int(offset_minute) > 59:
        raise ValueError()
    zone = timezone(timedelta(minutes=offset * (1 if sign == "+" else -1)))
    value = datetime(int(year), _MONTHS[month], int(day), int(hour), int(minute), int(second), tzinfo=zone)
    return parse_timestamp(value.isoformat())


def parse_capture(capture, start_ms=None, end_ms=None, baseline_log=None):
    reasons = []
    result = {"status": "incomplete", "post_204_in_window": 0, "post_400_in_window": 0,
              "boundary_lines": 0, "reasons": reasons}
    state = {"inode": capture.get("inode"), "size": capture.get("size", 0),
             "last_timestamp_ms": None, "configured": capture.get("configured") is True}
    result["state"] = state
    if not capture.get("configured"):
        result["status"] = "not_configured"
        reasons.append("LOG_NOT_CONFIGURED")
    if capture.get("missing") or capture.get("text") is None:
        result["status"] = "missing"
        reasons.append("LOG_MISSING")
    if capture.get("overflow"):
        reasons.append("LOG_SIZE_LIMIT")
    if capture.get("rotated") or capture.get("changed_during_read"):
        reasons.append("LOG_CHANGED")
    if baseline_log:
        if baseline_log.get("inode") != state["inode"]:
            reasons.append("LOG_ROTATED")
        if type(baseline_log.get("size")) is not int or state["size"] < baseline_log["size"]:
            reasons.append("LOG_TRUNCATED")
        if baseline_log.get("configured") is not True:
            reasons.append("LOG_BASELINE_INCOMPLETE")
    first = previous = None
    for line in (capture.get("text") or "").splitlines():
        if not line.strip():
            continue
        match = _LINE.fullmatch(line)
        nginx = False
        if match is None:
            match = _NGINX.fullmatch(line)
            nginx = True
        try:
            if match is None:
                raise ValueError()
            when = _timestamp(match, nginx)
        except (ValueError, KeyError, OverflowError):
            reasons.append("LOG_PARSE_ERROR")
            continue
        if previous is not None and when < previous:
            reasons.append("LOG_TIME_ORDER")
        first = when if first is None else min(first, when)
        previous = when
        state["last_timestamp_ms"] = when
        if start_ms is None or end_ms is None:
            continue
        membership = second_in_window(when - when % 1000, start_ms, end_ms)
        if membership == "boundary":
            result["boundary_lines"] += 1
            reasons.append("LOG_WINDOW_BOUNDARY")
        if membership == "inside" and match.group("method") == "POST":
            if match.group("status") == "204":
                result["post_204_in_window"] += 1
            if match.group("status") == "400":
                result["post_400_in_window"] += 1
    if first is None:
        reasons.append("LOG_EMPTY")
    if start_ms is not None and (first is None or first > start_ms):
        reasons.append("LOG_START_UNCOVERED")
    result["reasons"] = sorted(set(reasons))
    if not result["reasons"]:
        result["status"] = "complete"
    return result
