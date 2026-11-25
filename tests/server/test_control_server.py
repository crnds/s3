import http.client
import json
import sys
import threading
import unittest

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


if __name__ == "__main__":
    unittest.main()
