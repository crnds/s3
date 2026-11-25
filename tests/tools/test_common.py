"""tools/common.py: load_secret lookup order, download_gif against a local file.

    /usr/bin/python3 -m unittest discover -s tests/tools
"""
import json
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools")
sys.path.insert(0, TOOLS)
import common  # noqa: E402

NAME = "S3_TEST_SECRET"


class LoadSecretTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = os.path.join(tmp.name, "secrets.local.json")
        patcher = mock.patch.dict(os.environ)
        patcher.start()
        self.addCleanup(patcher.stop)
        os.environ.pop(NAME, None)

    def write(self, text):
        with open(self.path, "w") as f:
            f.write(text)

    def test_env_wins_over_file(self):
        self.write(json.dumps({NAME: "from-file"}))
        os.environ[NAME] = "from-env"
        self.assertEqual(common.load_secret(NAME, self.path), "from-env")

    def test_empty_env_falls_back_to_file(self):
        self.write(json.dumps({NAME: "from-file"}))
        os.environ[NAME] = ""
        self.assertEqual(common.load_secret(NAME, self.path), "from-file")

    def test_missing_file_is_none(self):
        self.assertIsNone(common.load_secret(NAME, self.path))

    def test_missing_key_is_none(self):
        self.write(json.dumps({"OTHER": "x"}))
        self.assertIsNone(common.load_secret(NAME, self.path))

    def test_empty_value_is_none(self):
        self.write(json.dumps({NAME: ""}))
        self.assertIsNone(common.load_secret(NAME, self.path))

    def test_malformed_or_non_object_file_is_none(self):
        for text in ("{not json", "null", "[1, 2]"):
            self.write(text)
            with mock.patch("sys.stderr"):
                self.assertIsNone(common.load_secret(NAME, self.path), text)

    def test_default_path_is_repo_root(self):
        self.assertEqual(common.SECRETS_PATH,
                         os.path.join(os.path.dirname(TOOLS), "secrets.local.json"))


class DownloadGifTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = tmp.name

    def src(self, data):
        p = os.path.join(self.dir, "src.bin")
        with open(p, "wb") as f:
            f.write(data)
        return pathlib.Path(p).as_uri()

    def test_gif_is_returned_and_written(self):
        data = b"GIF89a" + b"\0" * 10
        dest = os.path.join(self.dir, "out.gif")
        with mock.patch("builtins.print"):
            self.assertEqual(common.download_gif(self.src(data), dest), data)
        with open(dest, "rb") as f:
            self.assertEqual(f.read(), data)

    def test_non_gif_is_rejected_and_not_written(self):
        dest = os.path.join(self.dir, "out.gif")
        with mock.patch("builtins.print"):
            self.assertIsNone(common.download_gif(self.src(b"<html>"), dest))
        self.assertFalse(os.path.exists(dest))

    def test_unreachable_is_none(self):
        missing = pathlib.Path(os.path.join(self.dir, "nope.gif")).as_uri()
        with mock.patch("builtins.print"):
            self.assertIsNone(common.download_gif(missing))


class GifsicleArgsTest(unittest.TestCase):
    def test_lossy_off_at_zero(self):
        self.assertEqual(common.gifsicle_quality_args(64, 0), ["--colors", "64"])
        self.assertEqual(common.gifsicle_quality_args(), ["--colors", "128", "--lossy=80"])


if __name__ == "__main__":
    unittest.main()
