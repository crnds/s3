import math
import unittest
from datetime import datetime, timezone

import helpers
from helpers import u


class SanitizeNote(unittest.TestCase):
    def test_non_string_is_empty(self):
        for v in (None, 5, b"x", ["a"]):
            self.assertEqual(u.sanitize_note(v), "")

    def test_newlines_and_tabs_fold(self):
        self.assertEqual(u.sanitize_note("a\r\nb\rc\td"), "a\nb\nc d")

    def test_non_ascii_becomes_question_mark_not_dropped(self):
        self.assertEqual(u.sanitize_note("aกb\U0001F600c"), "a?b?c")

    def test_control_chars_become_question_mark(self):
        self.assertEqual(u.sanitize_note("a\x00b\x7fc"), "a?b?c")

    def test_caps_at_note_max(self):
        self.assertEqual(len(u.sanitize_note("x" * 1000)), u.NOTE_MAX_CHARS)


class UsAqi(unittest.TestCase):
    def test_missing_readings(self):
        self.assertIsNone(u.us_aqi("PM25", None))
        self.assertIsNone(u.us_aqi("PM25", -1))

    def test_pm25_breakpoints(self):
        cases = [(0, 0), (9.0, 50), (9.05, 50), (9.1, 51), (28.0, 86),
                 (35.4, 100), (35.45, 100), (35.5, 101), (500, 500)]
        for conc, want in cases:
            self.assertEqual(u.us_aqi("PM25", conc), want, conc)

    def test_one_decimal_truncation_is_stable(self):
        # floor(c*10)/10 must give every one-decimal value back unchanged
        # (float noise like 0.29*10 = 2.8999 would otherwise shift a row).
        for i in range(0, 3300):
            c = i / 10
            self.assertEqual(math.floor(c * 10) / 10, round(c, 1), c)

    def test_pm10_and_o3(self):
        self.assertEqual(u.us_aqi("PM10", 54.9), 50)
        self.assertEqual(u.us_aqi("PM10", 55), 51)
        self.assertEqual(u.us_aqi("O3", 70.9), 100)
        self.assertEqual(u.us_aqi("O3", 250), 300)  # clamped to the table's top


class Distance(unittest.TestCase):
    def test_zero_and_one_degree(self):
        self.assertAlmostEqual(u._km(13.75, 100.5, 13.75, 100.5), 0.0)
        self.assertAlmostEqual(u._km(0, 0, 1, 0), 111.19, delta=0.2)


class Weather(unittest.TestCase):
    def test_wmo_condition(self):
        want = {None: "", -1: "", 0: "Clear", 1: "Mainly Clear", 2: "Partly Cloudy",
                3: "Overcast", 45: "Fog", 53: "Drizzle", 63: "Rain", 73: "Snow",
                81: "Showers", 86: "Snow Showers", 95: "Thunderstorm", 99: "Thunderstorm"}
        for code, text in want.items():
            self.assertEqual(u.wmo_condition(code), text, code)

    def test_iso_hour(self):
        self.assertEqual(u._iso_hour("2026-07-20T17:00"), 17)
        self.assertEqual(u._iso_hour("2026-07-20 05:00"), 5)
        self.assertIsNone(u._iso_hour(""))
        self.assertIsNone(u._iso_hour("2026-07-20"))
        self.assertIsNone(u._iso_hour("2026-07-20Txx:00"))

    def test_iso_wday_is_sunday_zero(self):
        self.assertEqual(u._iso_wday("2026-11-22"), 0)  # Sunday
        self.assertEqual(u._iso_wday("2026-11-24"), 2)  # Tuesday
        self.assertEqual(u._iso_wday("2026-11-28"), 6)  # Saturday
        self.assertEqual(u._iso_wday("garbage"), 0)

    def test_weather_payload_shape(self):
        p = u.weather_payload(33.3, 3, high=35, low=26)
        self.assertEqual(set(p), {"tempC", "code", "condition", "high", "low", "hourly", "daily"})
        self.assertEqual((p["condition"], p["hourly"], p["daily"]), ("Overcast", [], []))
        self.assertEqual(u.weather_payload(1.0, None)["code"], -1)


class Pricing(unittest.TestCase):
    def test_pricing_for_model(self):
        self.assertIs(u.pricing_for_model("claude-opus-4-8"), u.PRICING["opus"])
        self.assertIs(u.pricing_for_model("CLAUDE-HAIKU-4-5-20251001"), u.PRICING["haiku"])
        self.assertIs(u.pricing_for_model("who-knows"), u.DEFAULT_PRICING)
        self.assertIs(u.pricing_for_model(None), u.DEFAULT_PRICING)

    def test_estimate_cost_multipliers(self):
        m = 1_000_000
        sonnet = "claude-sonnet-5-5"
        self.assertAlmostEqual(u.estimate_cost({"input_tokens": m}, sonnet), 3.0)
        self.assertAlmostEqual(u.estimate_cost({"output_tokens": m}, sonnet), 15.0)
        self.assertAlmostEqual(u.estimate_cost({"cache_creation_input_tokens": m}, sonnet), 3.75)
        self.assertAlmostEqual(u.estimate_cost({"cache_read_input_tokens": m}, sonnet), 0.3)
        self.assertEqual(u.estimate_cost({}, sonnet), 0)

    def test_model_family(self):
        self.assertEqual(u.model_family("claude-opus-4-8"), "opus")
        self.assertEqual(u.model_family("claude-haiku-4-5-20251001"), "haiku")
        self.assertEqual(u.model_family("gpt-x"), "gpt")
        self.assertEqual(u.model_family(""), "other")
        self.assertEqual(u.model_family(None), "other")

    def test_total_tokens(self):
        usage = {"input_tokens": 1, "output_tokens": 2,
                 "cache_creation_input_tokens": 3, "cache_read_input_tokens": 4}
        self.assertEqual(u.total_tokens(usage), 10)
        self.assertEqual(u.total_tokens({}), 0)


class Buckets(unittest.TestCase):
    def test_bucket_epoch_floors_to_five_minutes(self):
        t = datetime(2026, 11, 24, 10, 7, 33, tzinfo=timezone.utc)
        e = u.bucket_epoch_for(t)
        self.assertEqual(e % u.BUCKET_SEC, 0)
        self.assertEqual(datetime.fromtimestamp(e, tz=timezone.utc).minute, 5)
        t2 = datetime(2026, 11, 24, 10, 9, 59, tzinfo=timezone.utc)
        self.assertEqual(u.bucket_epoch_for(t2), e)

    def test_project_name(self):
        self.assertEqual(u.project_name("/Users/x/s3"), "s3")
        self.assertEqual(u.project_name("/Users/x/s3/"), "s3")
        self.assertEqual(u.project_name(""), "unknown")
        self.assertEqual(u.project_name(None), "unknown")
        self.assertEqual(u.project_name("/"), "/")


if __name__ == "__main__":
    unittest.main()
