"""Offline T18 tests for notification-log parsing and integrity.

Covers both accepted line shapes (strict ISO-8601 with an explicit zone, and
nginx ``[dd/Mon/yyyy:HH:mm:ss +0000]``), fixed field order and status counting.
Integrity cases must return a fixed reason code instead of raw lines: missing,
unconfigured, empty, parse errors, rotation/inode changes, truncation, overflow,
clock reversal and the left-closed/right-open second boundary.
"""

from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from identity_check.logs import parse_capture


BASE_MS = 1700000000000
EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)
MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")
RESULT_KEYS = {"status", "post_204_in_window", "post_400_in_window",
               "boundary_lines", "reasons", "state"}
STATE_KEYS = {"inode", "size", "last_timestamp_ms", "configured"}


def iso(ms):
    return (EPOCH + timedelta(milliseconds=ms)).isoformat(timespec="milliseconds").replace(
        "+00:00", "Z")


def line_text(timestamp, method="POST", status=204, upstream="204",
              address="10.0.0.1"):
    return "{} {} {} {} {}".format(timestamp, address, method, status, upstream)


def line(ms, method="POST", status=204, upstream="204", address="10.0.0.1"):
    return line_text(iso(ms), method, status, upstream, address)


def iso_fraction(ms, fraction):
    instant = (EPOCH + timedelta(milliseconds=ms)).replace(microsecond=0)
    return instant.strftime("%Y-%m-%dT%H:%M:%S") + fraction + "Z"


def iso_offset(ms, offset_hours):
    zone = timezone(timedelta(hours=offset_hours))
    instant = (EPOCH + timedelta(milliseconds=ms)).astimezone(zone).replace(
        microsecond=0)
    sign = "+" if offset_hours >= 0 else "-"
    return instant.strftime("%Y-%m-%dT%H:%M:%S") + "{}{:02d}:00".format(
        sign, abs(offset_hours))


def nginx_line(ms, offset_hours=0, method="POST", status=204, upstream="204",
               address="10.0.0.1"):
    zone = timezone(timedelta(hours=offset_hours))
    instant = (EPOCH + timedelta(milliseconds=ms)).astimezone(zone)
    sign = "+" if offset_hours >= 0 else "-"
    stamp = "[{:02d}/{:3s}/{:04d}:{:02d}:{:02d}:{:02d} {}{:02d}00]".format(
        instant.day, MONTHS[instant.month - 1], instant.year,
        instant.hour, instant.minute, instant.second, sign, abs(offset_hours))
    return "{} {} {} {} {}".format(stamp, address, method, status, upstream)


def capture(text, **overrides):
    value = {
        "configured": True,
        "inode": 7,
        "size": len(text.encode("utf-8")) if text is not None else 0,
        "text": text,
    }
    value.update(overrides)
    return value


def rendered(result):
    return json.dumps(result, sort_keys=True)


class StrictLineFormatTest(unittest.TestCase):
    def test_t18_iso_lines_accept_z_and_numeric_offsets(self):
        text = "\n".join((
            line(BASE_MS),
            line_text(iso_fraction(BASE_MS + 1000, ".1")),
            line_text(iso_fraction(BASE_MS + 2000, ".123")),
            line_text(iso_offset(BASE_MS + 3000, 8)),
            line_text(iso_offset(BASE_MS + 4000, -5)),
        ))

        result = parse_capture(capture(text))

        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["reasons"], [])
        self.assertEqual(result["state"]["last_timestamp_ms"], BASE_MS + 4000)

    def test_t18_iso_line_requires_seconds_and_explicit_zone(self):
        bad_lines = (
            "2023-11-14T22:13:20 10.0.0.1 POST 204 204",
            "2023-11-14T22:13:20.1234Z 10.0.0.1 POST 204 204",
            "2023-11-14T22:13:20z 10.0.0.1 POST 204 204",
            "2023-11-14T22:13:20+00 10.0.0.1 POST 204 204",
            "2023-11-14T22:13:20+24:00 10.0.0.1 POST 204 204",
            "2023-11-14T22:13:20+00:60 10.0.0.1 POST 204 204",
            "2023-11-14T22:13:60Z 10.0.0.1 POST 204 204",
            "2023-13-14T22:13:20Z 10.0.0.1 POST 204 204",
            "2023-11-14 22:13:20Z 10.0.0.1 POST 204 204",
        )
        for bad in bad_lines:
            with self.subTest(line=bad):
                result = parse_capture(capture(bad))
                self.assertIn("LOG_PARSE_ERROR", result["reasons"])
                self.assertNotEqual(result["status"], "complete")

    def test_t18_line_fields_are_strict_and_ordered(self):
        bad_lines = (
            line(BASE_MS, method="post"),
            line(BASE_MS, method="DELETE"),
            line(BASE_MS, status=20),
            line(BASE_MS, status=2040),
            line(BASE_MS, upstream="20"),
            line(BASE_MS, upstream="2040"),
            line(BASE_MS) + " extra",
            "{} 10.0.0.1 POST 204".format(iso(BASE_MS)),
            "{} 10.0.0.1 POST 204 - trailing".format(iso(BASE_MS)),
        )
        for bad in bad_lines:
            with self.subTest(line=bad):
                result = parse_capture(capture(bad))
                self.assertIn("LOG_PARSE_ERROR", result["reasons"])
                self.assertNotEqual(result["status"], "complete")

    def test_t18_nginx_lines_convert_offsets_to_the_same_instant(self):
        text = "\n".join((
            nginx_line(BASE_MS, 0),
            nginx_line(BASE_MS, 8),
            nginx_line(BASE_MS, -5),
            nginx_line(BASE_MS, 0, method="GET", status=500, upstream="-"),
        ))

        result = parse_capture(capture(text), BASE_MS, BASE_MS + 2000)

        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["post_204_in_window"], 3)
        self.assertEqual(result["boundary_lines"], 0)
        self.assertEqual(result["state"]["last_timestamp_ms"], BASE_MS)

    def test_t18_nginx_line_rejects_unknown_month_day_and_offset(self):
        bad_lines = (
            "[14/Foo/2023:22:13:20 +0000] 10.0.0.1 POST 204 204",
            "[14/nov/2023:22:13:20 +0000] 10.0.0.1 POST 204 204",
            "[00/Nov/2023:22:13:20 +0000] 10.0.0.1 POST 204 204",
            "[32/Nov/2023:22:13:20 +0000] 10.0.0.1 POST 204 204",
            "[14/Nov/2023:22:13:20 +2400] 10.0.0.1 POST 204 204",
            "[14/Nov/2023:22:13:20 +0060] 10.0.0.1 POST 204 204",
            "[14/Nov/2023:22:13:20] 10.0.0.1 POST 204 204",
        )
        for bad in bad_lines:
            with self.subTest(line=bad):
                result = parse_capture(capture(bad))
                self.assertIn("LOG_PARSE_ERROR", result["reasons"])
                self.assertNotEqual(result["status"], "complete")

    def test_t18_only_post_204_and_400_inside_window_are_counted(self):
        text = "\n".join((
            line(BASE_MS, "POST", 204),
            line(BASE_MS + 1000, "POST", 400),
            line(BASE_MS + 2000, "POST", 500),
            line(BASE_MS + 2500, "GET", 204),
            line(BASE_MS + 2999, "POST", 200),
            line(BASE_MS + 3000, "POST", 204),
        ))

        result = parse_capture(capture(text), BASE_MS, BASE_MS + 3000)

        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["post_204_in_window"], 1)
        self.assertEqual(result["post_400_in_window"], 1)
        self.assertEqual(result["boundary_lines"], 0)
        self.assertEqual(result["reasons"], [])

    def test_t18_partial_second_overlap_is_boundary_and_not_counted(self):
        text = "\n".join((
            line(BASE_MS + 100, "POST", 204),
            line(BASE_MS + 600, "POST", 204),
            line(BASE_MS + 1500, "POST", 204),
            line(BASE_MS + 2400, "POST", 400),
            line(BASE_MS + 2500, "POST", 204),
        ))

        result = parse_capture(capture(text), BASE_MS + 500, BASE_MS + 2500)

        self.assertEqual(result["post_204_in_window"], 1)
        self.assertEqual(result["post_400_in_window"], 0)
        # The line at +2500ms still belongs to second [+2000,+3000), which
        # overlaps the unaligned end and is therefore a boundary line too.
        self.assertEqual(result["boundary_lines"], 4)
        self.assertIn("LOG_WINDOW_BOUNDARY", result["reasons"])
        self.assertEqual(result["status"], "incomplete")

    def test_t18_window_is_left_closed_and_right_open(self):
        text = "\n".join((
            line(BASE_MS - 1000, "POST", 204),
            line(BASE_MS - 1, "POST", 204),
            line(BASE_MS, "POST", 204),
            line(BASE_MS + 1999, "POST", 204),
            line(BASE_MS + 2000, "POST", 204),
        ))

        result = parse_capture(capture(text), BASE_MS, BASE_MS + 2000)

        self.assertEqual(result["post_204_in_window"], 2)
        self.assertEqual(result["post_400_in_window"], 0)
        self.assertEqual(result["boundary_lines"], 0)
        self.assertEqual(result["status"], "complete")
        self.assertEqual(result["state"]["last_timestamp_ms"], BASE_MS + 2000)

    def test_t18_clock_backwards_marks_time_order(self):
        text = "\n".join((line(BASE_MS + 1000), line(BASE_MS)))

        result = parse_capture(capture(text), BASE_MS, BASE_MS + 2000)

        self.assertIn("LOG_TIME_ORDER", result["reasons"])
        self.assertEqual(result["status"], "incomplete")
        self.assertEqual(result["state"]["last_timestamp_ms"], BASE_MS)
        self.assertEqual(result["post_204_in_window"], 2)

    def test_t18_parse_errors_are_deduplicated_codes(self):
        result = parse_capture(capture("raw one\nraw two\n"))

        self.assertEqual(result["reasons"], ["LOG_EMPTY", "LOG_PARSE_ERROR"])
        self.assertNotEqual(result["status"], "complete")


class IntegrityReasonTest(unittest.TestCase):
    def test_t18_missing_log_is_incomplete(self):
        result = parse_capture(
            {"configured": True, "inode": 7, "size": 0, "missing": True, "text": None},
            BASE_MS, BASE_MS + 1000)

        self.assertEqual(result["status"], "missing")
        self.assertIn("LOG_MISSING", result["reasons"])
        self.assertIn("LOG_EMPTY", result["reasons"])
        self.assertIn("LOG_START_UNCOVERED", result["reasons"])

    def test_t18_unconfigured_log_is_incomplete(self):
        result = parse_capture(capture("", configured=False))

        self.assertEqual(result["status"], "not_configured")
        self.assertIn("LOG_NOT_CONFIGURED", result["reasons"])
        self.assertNotEqual(result["status"], "complete")

    def test_t18_empty_log_is_incomplete(self):
        result = parse_capture(capture(""), BASE_MS, BASE_MS + 1000)

        self.assertEqual(result["status"], "incomplete")
        self.assertIn("LOG_EMPTY", result["reasons"])
        self.assertIn("LOG_START_UNCOVERED", result["reasons"])
        self.assertIsNone(result["state"]["last_timestamp_ms"])

    def test_t18_earliest_line_after_window_start_is_uncovered(self):
        result = parse_capture(capture(line(BASE_MS + 1000)),
                               BASE_MS, BASE_MS + 2000)

        self.assertEqual(result["status"], "incomplete")
        self.assertIn("LOG_START_UNCOVERED", result["reasons"])
        self.assertNotIn("LOG_PARSE_ERROR", result["reasons"])

    def test_t18_inode_change_is_rotation(self):
        baseline = {"inode": 6, "size": 10, "last_timestamp_ms": BASE_MS,
                    "configured": True}
        result = parse_capture(capture(line(BASE_MS)), BASE_MS, BASE_MS + 1000,
                               baseline)

        self.assertEqual(result["status"], "incomplete")
        self.assertIn("LOG_ROTATED", result["reasons"])

    def test_t18_capture_rotation_or_read_change_is_changed(self):
        for field in ("rotated", "changed_during_read"):
            with self.subTest(field=field):
                result = parse_capture(capture(line(BASE_MS), **{field: True}),
                                       BASE_MS, BASE_MS + 1000)
                self.assertEqual(result["status"], "incomplete")
                self.assertIn("LOG_CHANGED", result["reasons"])

    def test_t18_smaller_size_is_truncation(self):
        baseline = {"inode": 7, "size": 200, "last_timestamp_ms": BASE_MS,
                    "configured": True}
        result = parse_capture(capture(line(BASE_MS), size=100),
                               BASE_MS, BASE_MS + 1000, baseline)

        self.assertIn("LOG_TRUNCATED", result["reasons"])
        self.assertEqual(result["status"], "incomplete")

    def test_t18_non_integer_baseline_size_is_truncation(self):
        for size in (True, "100", 100.0, None):
            with self.subTest(size=size):
                baseline = {"inode": 7, "size": size,
                            "last_timestamp_ms": BASE_MS, "configured": True}
                result = parse_capture(capture(line(BASE_MS)),
                                       BASE_MS, BASE_MS + 1000, baseline)
                self.assertIn("LOG_TRUNCATED", result["reasons"])

    def test_t18_incomplete_baseline_is_reported(self):
        baseline = {"inode": 7, "size": 0, "last_timestamp_ms": None,
                    "configured": False}
        result = parse_capture(capture(line(BASE_MS)), BASE_MS, BASE_MS + 1000,
                               baseline)

        self.assertIn("LOG_BASELINE_INCOMPLETE", result["reasons"])
        self.assertEqual(result["status"], "incomplete")

    def test_t18_overflow_is_incomplete_and_not_silently_truncated(self):
        result = parse_capture(capture(line(BASE_MS), overflow=True),
                               BASE_MS, BASE_MS + 1000)

        self.assertIn("LOG_SIZE_LIMIT", result["reasons"])
        self.assertEqual(result["status"], "incomplete")

    def test_t18_same_size_in_place_rewrite_is_a_documented_limitation(self):
        """Pins the inode+size-only continuity limit for main to improve later.

        ``parse_capture`` compares the baseline inode and size, not a content
        digest or the baseline last timestamp.  A same-size, same-inode rewrite
        (including an unseen rotate-and-restore) is therefore indistinguishable
        and is reported complete.  A baseline prefix digest would close this.
        """
        baseline = {"inode": 7, "size": len(line(BASE_MS).encode("utf-8")),
                    "last_timestamp_ms": BASE_MS + 5000, "configured": True}
        replacement = line(BASE_MS)  # same byte length, older than baseline tail

        result = parse_capture(capture(replacement), BASE_MS, BASE_MS + 1000,
                               baseline)

        self.assertEqual(result["status"], "complete")
        self.assertNotIn("LOG_ROTATED", result["reasons"])
        self.assertNotIn("LOG_TRUNCATED", result["reasons"])
        self.assertNotIn("LOG_TIME_ORDER", result["reasons"])
        self.assertEqual(result["state"]["last_timestamp_ms"], BASE_MS)

    def test_t18_same_size_rotation_with_new_inode_is_still_detected(self):
        baseline = {"inode": 7, "size": len(line(BASE_MS).encode("utf-8")),
                    "last_timestamp_ms": BASE_MS, "configured": True}
        result = parse_capture(capture(line(BASE_MS), inode=8),
                               BASE_MS, BASE_MS + 1000, baseline)

        self.assertIn("LOG_ROTATED", result["reasons"])
        self.assertEqual(result["status"], "incomplete")


class LogPrivacyTest(unittest.TestCase):
    def test_t18_results_never_contain_raw_lines_or_addresses(self):
        sensitive = "synthetic-secret-token 203.0.113.7"
        cases = (
            capture(sensitive),
            capture(""),
            {"configured": True, "inode": 7, "size": 0, "missing": True,
             "text": None},
            capture(line(BASE_MS) + "\n" + sensitive),
            capture(line(BASE_MS), overflow=True),
        )
        for case in cases:
            with self.subTest(case=case):
                result = parse_capture(case, BASE_MS, BASE_MS + 1000)
                text = rendered(result)
                self.assertNotIn(sensitive, text)
                self.assertNotIn("203.0.113.7", text)
                self.assertNotIn("10.0.0.1", text)
                self.assertNotIn("synthetic-secret-token", text)

    def test_t18_result_shape_is_fixed_and_contains_no_line_text(self):
        result = parse_capture(capture(line(BASE_MS)), BASE_MS, BASE_MS + 1000)

        self.assertEqual(set(result), RESULT_KEYS)
        self.assertEqual(set(result["state"]), STATE_KEYS)
        self.assertIsInstance(result["reasons"], list)
        for reason in result["reasons"]:
            self.assertRegex(reason, r"^[A-Z][A-Z0-9_]+$")


if __name__ == "__main__":
    unittest.main()
