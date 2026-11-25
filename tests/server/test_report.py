"""report.build_report() is a pure read of STATE (the key tree is pinned by
test_report_contract.py)."""
import unittest
from datetime import datetime, timedelta, timezone

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import report, state


class PureSnapshot(unittest.TestCase):
    def setUp(self):
        saved = dict(state.STATE)
        self.addCleanup(lambda: (state.STATE.clear(), state.STATE.update(saved)))

    def test_old_clients_hidden_but_not_pruned(self):
        now = datetime.now(timezone.utc)
        old = now - state.CLIENT_RETENTION - timedelta(minutes=1)
        state.STATE["clients"] = {"10.9.9.9": old, "10.0.0.5": now}
        r = report.build_report({}, now)
        self.assertEqual([c["ip"] for c in r["clients"]], ["10.0.0.5"])
        self.assertIn("10.9.9.9", state.STATE["clients"])  # pruning is note_client's job

    def test_offer_and_note_are_passed_through_as_copies(self):
        state.STATE["note"] = {"text": "x", "size": 2}
        r = report.build_report({"s3": {"sha256": "a"}})
        self.assertEqual(r["firmware"], {"s3": {"sha256": "a"}})
        r["note"]["text"] = "changed"
        self.assertEqual(state.STATE["note"]["text"], "x")


if __name__ == "__main__":
    unittest.main()
