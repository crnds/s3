import http.client
import json
import sys
import threading
import unittest
from unittest import mock

import helpers  # noqa: F401  (puts server/ on sys.path)

import control_server as cs


class BatterySaveBody(unittest.TestCase):
    """POST /api/battery-save validates its body before proxying it."""

    @classmethod
    def setUpClass(cls):
        cls.server = cs.Server(("127.0.0.1", 0), cs.Handler)
        cls.port = cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def post(self, raw):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=5)
        conn.request("POST", "/api/battery-save", body=raw, headers={"Content-Type": "application/json"})
        resp = conn.getresponse()
        body = json.loads(resp.read())
        conn.close()
        return resp.status, body

    def test_bodies_that_are_not_objects_are_a_400_not_a_dropped_connection(self):
        for raw in (b"5", b"[]", b'"x"', b"null", b"true", b"[1, 2]"):
            status, body = self.post(raw)
            self.assertEqual(status, 400, raw)
            self.assertFalse(body["ok"], raw)

    def test_other_bad_bodies(self):
        self.assertEqual(self.post(b"not json")[0], 400)
        self.assertEqual(self.post(b"{}")[0], 400)                    # no "enabled"
        self.assertEqual(self.post(b'{"enabled": "yes"}')[0], 400)    # not a bool/null
        self.assertEqual(self.post(b'{"enabled": 1}')[0], 400)


class SharedBase(unittest.TestCase):
    """Behaviour that now comes from s3srv.common (ports, JsonHandler)."""

    @classmethod
    def setUpClass(cls):
        cls.server = cs.Server(("127.0.0.1", 0), cs.Handler)
        cls.conn_port = cls.server.server_address[1]
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def call(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.conn_port, timeout=5)
        conn.request(method, path, body=body, headers=headers or {})
        resp = conn.getresponse()
        out = resp.status, dict(resp.getheaders()), resp.read()
        conn.close()
        return out

    def test_urls_come_from_the_shared_port(self):
        self.assertEqual(cs.USAGE_URL, "http://127.0.0.1:8787/api/usage")
        self.assertEqual(cs.ALLOWED_ORIGIN, "http://127.0.0.1:8788")

    def test_panel_page_404_and_null_origin_cors(self):
        status, h, _ = self.call("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", h["Content-Type"])
        self.assertEqual(self.call("GET", "/nope")[0], 404)
        _, h, _ = self.call("GET", "/nope", headers={"Origin": "null"})
        self.assertEqual(h.get("Access-Control-Allow-Origin"), "null")

    def test_body_limit_origin_check_and_proxy(self):
        self.assertEqual(self.call("POST", "/api/battery-save", body=b"x" * 5000)[0], 413)
        self.assertEqual(self.call("POST", "/api/battery-save", body=b'{"enabled": true}',
                                   headers={"Origin": "https://evil.example"})[0], 403)
        with mock.patch.object(cs, "set_battery_save", return_value=(True, "ok", {})) as proxy:
            self.assertEqual(self.call("POST", "/api/battery-save", body=b'{"enabled": false}')[0], 200)
        proxy.assert_called_once_with(False)


if __name__ == "__main__":
    unittest.main()
