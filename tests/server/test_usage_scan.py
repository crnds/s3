"""The jsonl tail: per-line parse, dedupe, bucket insert, and one real scan."""
import json
import os
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from unittest import mock

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import usage_scan as scan

TS = "2026-11-24T10:07:33.000Z"
USAGE = {"input_tokens": 10, "output_tokens": 20,
         "cache_creation_input_tokens": 30, "cache_read_input_tokens": 40}


def line(msg_id="msg_1", req="req_1", ts=TS, usage=USAGE, cwd="/Users/x/s3",
         model="claude-sonnet-5-5"):
    message = {"model": model, "usage": usage}
    if msg_id:
        message["id"] = msg_id
    entry = {"timestamp": ts, "cwd": cwd, "message": message}
    if req:
        entry["requestId"] = req
    return json.dumps(entry).encode()


class ParseLine(unittest.TestCase):
    def test_event_fields(self):
        ev = scan.parse_line(line())
        self.assertEqual(ev["ts"], datetime(2026, 11, 24, 10, 7, 33, tzinfo=timezone.utc))
        self.assertEqual(ev["key"], ("msg_1", "req_1"))
        self.assertEqual((ev["project"], ev["model"], ev["tokens"]), ("s3", "sonnet", 100))
        self.assertEqual(ev["ctx_tokens"], 80)  # input + both cache tiers, no output
        self.assertAlmostEqual(ev["cost"], scan.estimate_cost(USAGE, "claude-sonnet-5-5"))

    def test_str_input_and_padding(self):
        self.assertIsNotNone(scan.parse_line("  " + line().decode() + "  "))

    def test_no_id_means_no_dedupe_key(self):
        self.assertIsNone(scan.parse_line(line(msg_id=None))["key"])

    def test_naive_timestamp_is_utc(self):
        ev = scan.parse_line(line(ts="2026-11-24T10:07:33"))
        self.assertEqual(ev["ts"].tzinfo, timezone.utc)

    def test_lines_without_usage_or_garbage_are_none(self):
        for raw in (b"", b"   ", b"{not json", b"5", b'"x"', b"[]",
                    b'{"message": "str"}',
                    b'{"timestamp": "' + TS.encode() + b'", "message": {"id": "m"}}',
                    line(usage={}), line(usage="x"), line(ts=""), line(ts="yesterday"),
                    json.dumps({"timestamp": 5, "message": {"usage": USAGE}}).encode()):
            self.assertIsNone(scan.parse_line(raw), raw)


class IngestLines(unittest.TestCase):
    def test_dedupe_keeps_first(self):
        seen, batch = {}, scan.new_batch()
        scan.ingest_lines([line(), line(), line(req="req_2")], seen, batch)
        dims = list(batch["buckets"].values())[0]
        self.assertEqual(dims[("s3", "sonnet")]["tokens"], 200)  # 2 distinct, not 3
        self.assertEqual(set(seen), {("msg_1", "req_1"), ("msg_1", "req_2")})

    def test_seen_carries_across_scans(self):
        seen = {("msg_1", "req_1"): datetime.now(timezone.utc)}
        batch = scan.new_batch()
        scan.ingest_lines([line()], seen, batch)
        self.assertEqual(batch["buckets"], {})

    def test_events_without_id_are_all_counted(self):
        seen, batch = {}, scan.new_batch()
        scan.ingest_lines([line(msg_id=None), line(msg_id=None)], seen, batch)
        self.assertEqual(list(batch["buckets"].values())[0][("s3", "sonnet")]["tokens"], 200)
        self.assertEqual(seen, {})

    def test_buckets_and_newest(self):
        seen, batch = {}, scan.new_batch()
        later = "2026-11-24T10:22:00Z"
        scan.ingest_lines([line(msg_id="a", ts=later, usage={"input_tokens": 7}),
                           line(msg_id="b"), line(msg_id="c", cwd="/p/other")], seen, batch)
        self.assertEqual(len(batch["buckets"]), 2)
        first = batch["buckets"][scan.bucket_epoch_for(scan.parse_line(line())["ts"])]
        self.assertEqual(set(first), {("s3", "sonnet"), ("other", "sonnet")})
        self.assertEqual(batch["newest_ctx"]["tokens"], 7)
        self.assertEqual(batch["newest_ts"].minute, 22)


class MergeBuckets(unittest.TestCase):
    def test_sums_into_existing_and_adds_new(self):
        target = {300: {("p", "m"): {"tokens": 1, "cost": 0.5}}}
        new = {300: {("p", "m"): {"tokens": 2, "cost": 0.25}, ("q", "m"): {"tokens": 4, "cost": 1.0}},
               600: {("p", "m"): {"tokens": 8, "cost": 2.0}}}
        out = scan.merge_buckets(target, new)
        self.assertIs(out, target)
        self.assertEqual(target[300][("p", "m")], {"tokens": 3, "cost": 0.75})
        self.assertEqual(target[300][("q", "m")], {"tokens": 4, "cost": 1.0})
        self.assertEqual(target[600][("p", "m")], {"tokens": 8, "cost": 2.0})


class ScanOnce(unittest.TestCase):
    """One real tail over a temp projects dir: partial lines wait, offsets advance."""

    def setUp(self):
        saved = dict(scan.STATE)
        self.addCleanup(lambda: (scan.STATE.clear(), scan.STATE.update(saved)))
        scan.STATE.update(offsets={}, buckets={}, seen={}, context=None, last_ts=None)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        os.makedirs(os.path.join(self.tmp.name, "proj"))
        self.path = os.path.join(self.tmp.name, "proj", "s.jsonl")
        p = mock.patch.object(scan, "LOG_GLOB", os.path.join(self.tmp.name, "*", "*.jsonl"))
        p.start()
        self.addCleanup(p.stop)

    def append(self, data):
        with open(self.path, "ab") as f:
            f.write(data)

    def tokens(self):
        return sum(d["tokens"] for dims in scan.STATE["buckets"].values() for d in dims.values())

    def test_tail(self):
        now = datetime.now(timezone.utc).isoformat()
        self.append(line(msg_id="a", ts=now) + b"\n" + line(msg_id="b", ts=now)[:20])
        self.assertTrue(scan.scan_once())
        self.assertEqual(self.tokens(), 100)  # the partial second line waits
        self.assertEqual(scan.STATE["offsets"][self.path], len(line(msg_id="a", ts=now)) + 1)
        self.append(line(msg_id="b", ts=now)[20:] + b"\n" + line(msg_id="a", ts=now) + b"\n")
        self.assertTrue(scan.scan_once())
        self.assertEqual(self.tokens(), 200)  # b completed; the repeated a deduped
        self.assertFalse(scan.scan_once())    # nothing new
        self.assertIsNotNone(scan.STATE["last_ts"])
        self.assertEqual(scan.STATE["context"]["tokens"], 80)

    def test_old_events_are_dropped_and_vanished_files_forgotten(self):
        old = (datetime.now(timezone.utc) - scan.RETENTION - timedelta(days=1)).isoformat()
        self.append(line(msg_id="old", ts=old) + b"\n")
        scan.scan_once()
        self.assertEqual(scan.STATE["buckets"], {})
        self.assertEqual(scan.STATE["seen"], {})
        os.remove(self.path)
        scan.scan_once()
        self.assertEqual(scan.STATE["offsets"], {})


if __name__ == "__main__":
    unittest.main()
