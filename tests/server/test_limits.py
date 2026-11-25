"""parse_limits(): the OAuth usage response -> the snapshot in STATE["limits"]."""
import unittest
from datetime import datetime, timedelta

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import limits

NOW = datetime(2026, 11, 24, 10, 0).astimezone()


def iso(dt):
    return dt.isoformat()


class ParseLimits(unittest.TestCase):
    def test_neither_window_is_none(self):
        self.assertIsNone(limits.parse_limits({}, NOW))
        self.assertIsNone(limits.parse_limits({"limits": []}, NOW))
        self.assertIsNone(limits.parse_limits(["x"], NOW))

    def test_windows(self):
        later_today = NOW + timedelta(hours=2)
        next_week = NOW + timedelta(days=3)
        doc = {"five_hour": {"utilization": 41.6, "resets_at": iso(later_today)},
               "seven_day": {"utilization": 12.2, "resets_at": iso(next_week)}}
        out = limits.parse_limits(doc, NOW)
        self.assertEqual(out["fetched_at_epoch"], NOW.timestamp())
        s, w = out["session"], out["week"]
        self.assertEqual((s["percent"], s["resets"]), (42, later_today.strftime("%H:%M")))
        self.assertEqual(s["resets_at_epoch"], later_today.timestamp())
        # The week shows a date when the reset isn't today; the session never does.
        self.assertEqual(w["resets"], "%s %d, %s" % (next_week.strftime("%b"), next_week.day,
                                                      next_week.strftime("%H:%M")))
        self.assertEqual(w["percent"], 12)
        self.assertIsNone(out["week_model"])
        self.assertIsNone(out["credits"])

    def test_null_resets_at_is_zero_percent_no_countdown(self):
        out = limits.parse_limits({"five_hour": {"utilization": None, "resets_at": None}}, NOW)
        self.assertEqual(out["session"], {"percent": 0, "resets": ""})
        self.assertEqual(out["week"], {"percent": 0, "resets": ""})

    def test_week_model_from_limits_array(self):
        doc = {"seven_day": {}, "limits": [
            {"kind": "session", "percent": 5},
            {"kind": "weekly_scoped", "percent": 33.4, "resets_at": iso(NOW + timedelta(days=2)),
             "scope": {"model": {"display_name": "Fable"}}},
        ]}
        wm = limits.parse_limits(doc, NOW)["week_model"]
        self.assertEqual((wm["name"], wm["percent"]), ("Fable", 33))
        self.assertIn("resets_at_epoch", wm)

    def test_credits_from_spend(self):
        doc = {"five_hour": {}, "spend": {"enabled": True,
                                           "used": {"amount_minor": 41, "exponent": 2},
                                           "limit": {"amount_minor": 1000}}}
        self.assertEqual(limits.parse_limits(doc, NOW)["credits"],
                         {"used": 0.41, "limit": 10.0, "percent": 4})

    def test_credits_from_extra_usage(self):
        doc = {"five_hour": {}, "extra_usage": {"is_enabled": True, "monthly_limit": 2000,
                                                 "used_credits": 500, "utilization": None}}
        self.assertEqual(limits.parse_limits(doc, NOW)["credits"],
                         {"used": 5.0, "limit": 20.0, "percent": 25})

    def test_credits_disabled_is_none(self):
        doc = {"five_hour": {}, "spend": {"enabled": False}, "extra_usage": {"is_enabled": False}}
        self.assertIsNone(limits.parse_limits(doc, NOW)["credits"])


if __name__ == "__main__":
    unittest.main()
