"""Time handling tests, including T20-T22 boundaries."""
import unittest

from identity_check import timeutil


class ParseTests(unittest.TestCase):
    def test_z_and_numeric_offset_are_the_same_instant(self):
        self.assertEqual(
            timeutil.parse_timestamp("2026-11-12T01:30:00Z"),
            timeutil.parse_timestamp("2026-11-12T09:30:00+08:00"),
        )

    def test_naive_timestamp_is_rejected(self):
        with self.assertRaises(ValueError):
            timeutil.parse_timestamp("2026-11-12T09:30:00")

    def test_fraction_is_floored_to_milliseconds(self):
        self.assertEqual(
            timeutil.parse_timestamp("2026-11-12T01:30:00.123456Z"),
            timeutil.parse_timestamp("2026-11-12T01:30:00.123Z"),
        )
        self.assertEqual(
            timeutil.parse_timestamp("2026-11-12T01:30:00.999999Z"),
            timeutil.parse_timestamp("2026-11-12T01:30:00.999Z"),
        )

    def test_offset_limits(self):
        self.assertIsInstance(
            timeutil.parse_timestamp("2026-11-12T12:00:00+23:59"), int
        )
        for value in (
            "2026-11-12T12:00:00+24:00",
            "2026-11-12T12:00:00-24:00",
            "2026-11-12T12:00:00+23:60",
            "2026-11-12T12:00:00+00:60",
        ):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    timeutil.parse_timestamp(value)

    def test_invalid_calendar_and_shape_values(self):
        for value in (
            "2026-02-30T01:30:00Z",
            "2026-11-12T24:00:00Z",
            "2026-11-12T01:30:60Z",
            "2026-11-12T01:30Z",
            "2026-11-12T01:30:00.1234567Z",
            "2026-11-12T01:30:00ZZ",
            " 2026-11-12T01:30:00Z",
            "2026-11-12t01:30:00Z",
        ):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    timeutil.parse_timestamp(value)

    def test_non_string_is_rejected(self):
        with self.assertRaises(ValueError):
            timeutil.parse_timestamp(1700000000000)

    def test_epoch_and_range_validation(self):
        self.assertEqual(timeutil.parse_timestamp("1970-01-01T00:00:00Z"), 0)
        with self.assertRaises(ValueError):
            timeutil.parse_timestamp("1969-12-31T23:59:59Z")


class FormatTests(unittest.TestCase):
    def test_milliseconds_are_always_three_digits(self):
        self.assertEqual(timeutil.format_timestamp(0), "1970-01-01T00:00:00.000Z")
        self.assertEqual(timeutil.format_timestamp(999), "1970-01-01T00:00:00.999Z")

    def test_hong_kong_crosses_the_day(self):
        value = timeutil.parse_timestamp("2026-11-12T16:00:00Z")
        self.assertEqual(
            timeutil.format_timestamp(value, timeutil.HONG_KONG),
            "2026-11-13T00:00:00.000+08:00",
        )
        self.assertEqual(
            timeutil.describe_time(value),
            {
                "utc": "2026-11-12T16:00:00.000Z",
                "hong_kong": "2026-11-13T00:00:00.000+08:00",
            },
        )

    def test_round_trip(self):
        value = timeutil.parse_timestamp("2026-11-12T09:30:00.042+08:00")
        self.assertEqual(
            timeutil.format_timestamp(value), "2026-11-12T01:30:00.042Z"
        )

    def test_invalid_millisecond_values(self):
        for value in (-1, 253402272000000, True, "0"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    timeutil.format_timestamp(value)


class WindowTests(unittest.TestCase):
    def test_left_closed_right_open(self):
        start, end = timeutil.validate_window(1000, 2000)
        self.assertTrue(timeutil.in_window(1000, start, end))
        self.assertTrue(timeutil.in_window(1999, start, end))
        self.assertFalse(timeutil.in_window(2000, start, end))

    def test_invalid_windows(self):
        for start, end in ((1000, 1000), (2000, 1000), (-1, 2000), (True, 2000)):
            with self.subTest(start=start, end=end):
                with self.assertRaises(ValueError):
                    timeutil.validate_window(start, end)

    def test_second_resolution_log_lines(self):
        start, end = 1000, 3000
        self.assertEqual(timeutil.second_in_window(0, start, end), "outside")
        self.assertEqual(timeutil.second_in_window(500, start, end), "boundary")
        self.assertEqual(timeutil.second_in_window(1000, start, end), "inside")
        self.assertEqual(timeutil.second_in_window(2000, start, end), "inside")
        self.assertEqual(timeutil.second_in_window(2500, start, end), "boundary")
        self.assertEqual(timeutil.second_in_window(3000, start, end), "outside")

    def test_clock_skew(self):
        self.assertEqual(timeutil.clock_skew(11000, 10000, 12000), 0)
        self.assertEqual(timeutil.clock_skew(8999, 10000, 12000), -1)
        self.assertEqual(timeutil.clock_skew(13001, 10000, 12000), 1)
        self.assertEqual(timeutil.clock_skew(8500, 10000, 12000), -500)
        self.assertEqual(timeutil.clock_skew(13500, 10000, 12000), 500)
        with self.assertRaises(ValueError):
            timeutil.clock_skew(11000, 12000, 10000)
        with self.assertRaises(ValueError):
            timeutil.clock_skew(11000, 10000, 12000, tolerance=-1)


if __name__ == "__main__":
    unittest.main()
