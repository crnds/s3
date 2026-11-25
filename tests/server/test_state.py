import unittest
from datetime import datetime, timedelta, timezone

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import state


class StateTests(unittest.TestCase):
    def setUp(self):
        saved = dict(state.STATE)
        self.addCleanup(lambda: (state.STATE.clear(), state.STATE.update(saved)))
        state.STATE["clients"] = {}

    def test_state_set_and_get(self):
        state.state_set("btc", {"price": 1.0})
        self.assertEqual(state.state_get("btc"), {"price": 1.0})

    def test_note_client_records_wakes_and_prunes(self):
        now = datetime.now(timezone.utc)
        old = now - state.CLIENT_RETENTION - timedelta(minutes=1)
        state.STATE["clients"] = {"10.9.9.9": old, "10.0.0.6": now - timedelta(hours=1)}
        state.STATE["activity_event"].clear()
        state.note_client("10.0.0.5", now)
        self.assertEqual(set(state.STATE["clients"]), {"10.0.0.5", "10.0.0.6"})
        self.assertTrue(state.STATE["activity_event"].is_set())
        self.assertTrue(state.is_known_client("10.0.0.5"))
        self.assertFalse(state.is_known_client("10.9.9.9"))

    def test_client_active_window(self):
        now = datetime.now(timezone.utc)
        state.STATE["clients"] = {"a": now - timedelta(seconds=state.CLIENT_ACTIVE_SEC + 5)}
        self.assertFalse(state.is_client_active())
        state.STATE["clients"]["b"] = now
        self.assertTrue(state.is_client_active())


if __name__ == "__main__":
    unittest.main()
