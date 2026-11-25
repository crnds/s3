"""The usage server's route table over real HTTP on an ephemeral loopback port."""
import contextlib
import io
import json
import os
import tempfile
import unittest
import urllib.error
import urllib.request
from unittest import mock

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import notes, routes, state


class Routes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with contextlib.redirect_stdout(io.StringIO()):
            cls.server = routes.start_http_server(lambda: {"usage": True}, port=0, host="127.0.0.1")
        cls.base = "http://127.0.0.1:%d" % cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def setUp(self):
        saved = dict(state.STATE)
        self.addCleanup(lambda: (state.STATE.clear(), state.STATE.update(saved)))
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        p = mock.patch.object(notes, "NOTE_PATH", os.path.join(tmp.name, "note.json"))
        p.start()
        self.addCleanup(p.stop)

    def call(self, path, body=None, headers=None, method=None):
        req = urllib.request.Request(self.base + path, data=body, headers=headers or {}, method=method)
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                return resp.status, dict(resp.headers), resp.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def test_usage_records_the_client(self):
        status, headers, body = self.call("/api/usage")
        self.assertEqual((status, json.loads(body)), (200, {"usage": True}))
        self.assertIn("127.0.0.1", state.STATE["clients"])
        self.assertNotIn("Access-Control-Allow-Origin", headers)

    def test_cors_exact_allowlist(self):
        _, h, _ = self.call("/api/usage", headers={"Origin": "http://127.0.0.1:8765"})
        self.assertEqual(h.get("Access-Control-Allow-Origin"), "http://127.0.0.1:8765")
        _, h, _ = self.call("/api/usage", headers={"Origin": "https://evil.example"})
        self.assertNotIn("Access-Control-Allow-Origin", h)
        _, h, _ = self.call("/api/note", headers={"Origin": "http://127.0.0.1:8765"})
        self.assertNotIn("Access-Control-Allow-Origin", h)  # loopback only on /api/usage
        _, h, _ = self.call("/api/note", headers={"Origin": "null"})
        self.assertEqual(h.get("Access-Control-Allow-Origin"), "null")

    def test_unknown_paths_404(self):
        self.assertEqual(self.call("/nope")[0], 404)
        self.assertEqual(self.call("/nope", body=b"{}")[0], 404)
        self.assertEqual(self.call("/firmware/cyd.bin")[0], 404)

    def test_note_html_and_round_trip(self):
        status, h, body = self.call("/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", h["Content-Type"])
        status, _, body = self.call("/api/note", body=json.dumps({"text": "hi\tthere", "size": 2}).encode(),
                                    headers={"Content-Type": "application/json"})
        self.assertEqual((status, json.loads(body)["note"]), (200, {"text": "hi there", "size": 2}))
        self.assertEqual(json.loads(self.call("/api/note")[2]), {"text": "hi there", "size": 2})

    def test_note_post_validation(self):
        for body in (b"5", b'{"text": 1}', b'{"text": "x", "size": 4}', b'{"text": "x", "size": true}'):
            self.assertEqual(self.call("/api/note", body=body)[0], 400, body)
        self.assertEqual(self.call("/api/note", body=b"{bad")[0], 400)
        self.assertEqual(self.call("/api/note", body=b'{"text": "x"}',
                                   headers={"Origin": "https://evil.example"})[0], 403)
        self.assertEqual(self.call("/api/note", body=b"x" * 5000)[0], 413)

    def test_battery_save(self):
        self.assertEqual(self.call("/api/battery-save", body=b"5")[0], 400)
        self.assertEqual(self.call("/api/battery-save", body=b'{"enabled": "yes"}')[0], 400)
        self.assertEqual(self.call("/api/battery-save", body=b'{"enabled": true}',
                                   headers={"Origin": "null"})[0], 403)
        with mock.patch.object(routes, "set_battery_save_manual") as setter:
            status, _, body = self.call("/api/battery-save", body=b'{"enabled": null}')
        self.assertEqual(status, 200)
        setter.assert_called_once_with(None)
        self.assertIn("power", json.loads(body))

    def test_firmware_routes(self):
        status, _, body = self.call("/api/firmware")
        self.assertEqual(status, 200)
        self.assertIn("slot_bytes", json.loads(body))
        self.assertEqual(self.call("/api/firmware/install", body=b"{}",
                                   headers={"Origin": "null"})[0], 403)

    def test_preflight(self):
        status, h, _ = self.call("/api/note", headers={"Origin": "null"}, method="OPTIONS")
        self.assertEqual((status, h.get("Access-Control-Allow-Origin")), (204, "null"))
        self.assertEqual(self.call("/api/usage", headers={"Origin": "null"}, method="OPTIONS")[0], 404)


class PollLog(unittest.TestCase):
    def setUp(self):
        for d in (routes._last_heartbeat_log, routes._last_seen_mono, routes._min_gap_seen):
            d.clear()
            self.addCleanup(d.clear)

    def lines(self, ip, t):
        with mock.patch.object(routes, "log_out") as out:
            routes.log_poll(ip, t)
        return [c[0][0] for c in out.call_args_list]

    def test_heartbeat_throttled_and_gap_reported_once(self):
        ip = "10.0.0.5"
        self.assertEqual(self.lines(ip, 1000.0), [ip + " GET /api/usage"])
        self.assertEqual(self.lines(ip, 1020.0), [])         # 20s cadence learnt
        self.assertEqual(self.lines(ip, 1040.0), [])
        out = self.lines(ip, 1140.0)                           # 100s gap = 5x
        self.assertEqual(len(out), 1)
        self.assertIn("RESUMED after 100s gap (polls ~every 20s)", out[0])
        t = 1140.0
        while t + 20 < 1140.0 + routes.HEARTBEAT_LOG_INTERVAL_SEC:
            t += 20
            self.assertEqual(self.lines(ip, t), [], t)       # steady polls stay quiet
        self.assertEqual(self.lines(ip, 1140.0 + routes.HEARTBEAT_LOG_INTERVAL_SEC), [ip + " GET /api/usage"])


if __name__ == "__main__":
    unittest.main()
