"""GET /api/usage is the contract with src/net.cpp and simulator-s3.html.

The key tree of build_report() -- every key at every depth, with the first
element standing in for a list -- is pinned in usage_keys.json. A rename or
removal fails here before it reaches the board; adding a field is a deliberate
edit of the snapshot (UPDATE_KEYS=1 python3 -m unittest discover -s tests/server).
"""
import json
import os
import time
import unittest
from datetime import datetime, timezone
from unittest import mock

import helpers
from helpers import u

SNAPSHOT = os.path.join(os.path.dirname(__file__), "usage_keys.json")


def key_tree(v):
    if isinstance(v, dict):
        return {k: key_tree(x) for k, x in sorted(v.items())}
    if isinstance(v, list):
        return [key_tree(v[0])] if v else []
    return "value" if v is not None else "null"


def full_state():
    """Every optional part present, shaped as the fetch_* functions return it."""
    now = datetime.now(timezone.utc)
    win = {"percent": 41, "resets": "6:00pm", "resets_at_epoch": time.time() + 3600}
    return {
        "aggregates": {
            "today": {"tokens": 10, "cost": 0.5},
            "week": {"tokens": 70, "cost": 3.5},
            "projects": [{"name": "s3", "tokens": 70}],
            "trend": [0, 0, 0, 0, 0, 0, 10],
        },
        "context": {"tokens": 50000, "ts": now},
        "last_ts": now,
        "limits": {
            "tz": "Asia/Bangkok",
            "fetched_at_epoch": time.time(),
            "session": dict(win),
            "week": dict(win),
            "week_model": dict(win, name="Fable"),
            "credits": {"used": 0.41, "limit": 10.0, "percent": 4},
        },
        "btc": {"price": 82893.0, "changePct": 1.25},
        "weather": u.weather_payload(
            33.3, 3, high=35, low=26,
            hourly=[{"h": 17, "tempC": 33, "code": 3}],
            daily=[{"wd": 2, "high": 35, "low": 26, "code": 3}]),
        "aqi": {"aqi": 88, "source": "air4thai"},
        "note": {"text": "hi", "size": 1},
        "clients": {"10.0.0.5": now},
    }


class ReportContract(unittest.TestCase):
    def setUp(self):
        saved = dict(u.STATE)
        self.addCleanup(lambda: (u.STATE.clear(), u.STATE.update(saved)))
        u.STATE.update(full_state())
        offer = {"s3": {"sha256": "a" * 64, "size": 3000000,
                        "published": "2026-10-14T12:09:33+00:00", "install": 1}}
        p = mock.patch.object(u, "firmware_offer", return_value=offer)
        p.start()
        self.addCleanup(p.stop)

    def test_key_tree_matches_snapshot(self):
        tree = key_tree(u.build_report())
        if os.environ.get("UPDATE_KEYS"):
            with open(SNAPSHOT, "w") as f:
                json.dump(tree, f, indent=1, sort_keys=True)
                f.write("\n")
        with open(SNAPSHOT) as f:
            self.assertEqual(tree, json.load(f))

    def test_report_is_json_serialisable(self):
        json.dumps(u.build_report())

    def test_internal_limit_fields_are_stripped(self):
        limits = u.build_report()["limits"]
        for k in ("tz", "fetched_at_epoch"):
            self.assertNotIn(k, limits)
        self.assertNotIn("resets_at_epoch", limits["session"])
        self.assertIn("resets_in_sec", limits["session"])
        self.assertIn("resets_in_sec", limits["week"])
        self.assertNotIn("resets_in_sec", limits["week_model"])

    def test_expired_window_reads_as_reset(self):
        u.STATE["limits"]["session"]["resets_at_epoch"] = time.time() - 5
        s = u.build_report()["limits"]["session"]
        self.assertEqual((s["percent"], s["resets"], s["resets_in_sec"]), (0, "", 0))

    def test_state_limits_not_mutated_by_a_report(self):
        u.build_report()
        self.assertIn("resets_at_epoch", u.STATE["limits"]["session"])
        self.assertIn("tz", u.STATE["limits"])

    def test_note_is_always_an_object(self):
        u.STATE["note"] = {"text": "", "size": 1}
        self.assertEqual(u.build_report()["note"], {"text": "", "size": 1})

    def test_empty_state_still_has_the_core_keys(self):
        u.STATE.update(aggregates=None, context=None, last_ts=None, limits=None,
                       btc=None, weather=None, aqi=None, clients={})
        r = u.build_report()
        self.assertEqual(r["today"], {"tokens": 0, "cost": 0.0})
        self.assertEqual(r["trend"], [0] * 7)
        self.assertFalse(r["active_now"])
        self.assertIsNone(r["context"])
        self.assertEqual(r["clients"], [])

    def test_old_clients_are_pruned(self):
        old = datetime.now(timezone.utc) - u.CLIENT_RETENTION - u.timedelta(minutes=1)
        u.STATE["clients"] = {"10.9.9.9": old, "10.0.0.5": datetime.now(timezone.utc)}
        self.assertEqual([c["ip"] for c in u.build_report()["clients"]], ["10.0.0.5"])


if __name__ == "__main__":
    unittest.main()
