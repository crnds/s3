import os
import tempfile
import time
import unittest

import helpers
from helpers import u

SHA_A = "a" * 64
SHA_B = "b" * 64


def write_image(path, digest=b"\x11" * 32, size=4096, magic=b"\xe9"):
    body = magic + b"\x00" * (size - 1 - 32) + digest
    with open(path, "wb") as f:
        f.write(body)


class FirmwareInfo(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self._dir, u.FIRMWARE_DIR = u.FIRMWARE_DIR, self.tmp.name
        self._cache = dict(u._firmware_cache)
        u._firmware_cache.clear()
        self.addCleanup(self._restore)

    def _restore(self):
        u.FIRMWARE_DIR = self._dir
        u._firmware_cache.clear()
        u._firmware_cache.update(self._cache)

    def path(self):
        return os.path.join(self.tmp.name, "s3.bin")

    def test_missing_file(self):
        self.assertIsNone(u.firmware_info("s3"))

    def test_digest_is_last_32_bytes(self):
        write_image(self.path(), digest=bytes(range(32)))
        info = u.firmware_info("s3")
        self.assertEqual(info["sha256"], bytes(range(32)).hex())
        self.assertEqual(info["size"], 4096)
        self.assertIn("published", info)

    def test_rejects_bad_magic_tiny_and_oversize(self):
        write_image(self.path(), magic=b"\x00")
        self.assertIsNone(u.firmware_info("s3"))
        u._firmware_cache.clear()
        with open(self.path(), "wb") as f:
            f.write(b"\xe9" * 512)
        self.assertIsNone(u.firmware_info("s3"))
        u._firmware_cache.clear()
        write_image(self.path(), size=u.FIRMWARE_MAX_BYTES + 1)
        self.assertIsNone(u.firmware_info("s3"))

    def test_cache_follows_the_file(self):
        write_image(self.path(), digest=b"\x01" * 32)
        first = u.firmware_info("s3")
        self.assertIs(u.firmware_info("s3"), first)  # cached on (mtime, size)
        write_image(self.path(), digest=b"\x02" * 32, size=5000)
        self.assertEqual(u.firmware_info("s3")["sha256"], ("02" * 32))

    def test_offer_carries_install_id_only_while_pending(self):
        write_image(self.path())
        sha = u.firmware_info("s3")["sha256"]
        u._firmware_requests.pop("s3", None)
        self.addCleanup(u._firmware_requests.pop, "s3", None)
        self.assertNotIn("install", u.firmware_offer()["s3"])
        u._firmware_requests["s3"] = {"id": 7, "sha256": sha, "ts": time.time()}
        self.assertEqual(u.firmware_offer()["s3"]["install"], 7)


class FirmwareRequest(unittest.TestCase):
    def setUp(self):
        u._firmware_requests.clear()
        self.addCleanup(u._firmware_requests.clear)

    def test_no_request_or_no_info(self):
        self.assertIsNone(u.firmware_request("s3", {"sha256": SHA_A}))
        u._firmware_requests["s3"] = {"id": 1, "sha256": SHA_A, "ts": time.time()}
        self.assertIsNone(u.firmware_request("s3", None))

    def test_live_request_is_returned(self):
        req = {"id": 1, "sha256": SHA_A, "ts": time.time()}
        u._firmware_requests["s3"] = req
        self.assertIs(u.firmware_request("s3", {"sha256": SHA_A}), req)

    def test_newer_build_or_age_clears_it(self):
        u._firmware_requests["s3"] = {"id": 1, "sha256": SHA_A, "ts": time.time()}
        self.assertIsNone(u.firmware_request("s3", {"sha256": SHA_B}))
        self.assertNotIn("s3", u._firmware_requests)
        old = time.time() - u.FIRMWARE_REQUEST_TTL_SEC - 1
        u._firmware_requests["s3"] = {"id": 1, "sha256": SHA_A, "ts": old}
        self.assertIsNone(u.firmware_request("s3", {"sha256": SHA_A}))
        self.assertNotIn("s3", u._firmware_requests)


class BoardReports(unittest.TestCase):
    def setUp(self):
        for d in (u._board_firmware, u._firmware_requests):
            d.clear()
            self.addCleanup(d.clear)

    def test_header_parse(self):
        u.note_board_firmware("10.0.0.5", SHA_A.upper(), "current; auto=1; req=42")
        rec = u._board_firmware["10.0.0.5"]
        self.assertEqual((rec["sha256"], rec["ota"], rec["auto"]), (SHA_A, "current", True))
        u.note_board_firmware("10.0.0.5", SHA_A, "ready; auto=0")
        self.assertIs(u._board_firmware["10.0.0.5"]["auto"], False)

    def test_bad_digest_ignored(self):
        u.note_board_firmware("10.0.0.5", "", "current")
        u.note_board_firmware("10.0.0.5", "abc", "current")
        self.assertEqual(u._board_firmware, {})

    def test_request_done_when_board_runs_it(self):
        u._firmware_requests["s3"] = {"id": 9, "sha256": SHA_A, "ts": time.time()}
        u.note_board_firmware("10.0.0.5", SHA_B, "current")
        self.assertIn("s3", u._firmware_requests)  # not running it yet
        u.note_board_firmware("10.0.0.5", SHA_A, "current")
        self.assertNotIn("s3", u._firmware_requests)

    def test_request_done_when_board_reports_it_handled_it(self):
        u._firmware_requests["s3"] = {"id": 9, "sha256": SHA_A, "ts": time.time()}
        u.note_board_firmware("10.0.0.5", SHA_B, "failed; auto=1; req=9")
        self.assertNotIn("s3", u._firmware_requests)


if __name__ == "__main__":
    unittest.main()
