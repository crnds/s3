import json
import os
import tempfile
import unittest
from unittest import mock

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import common


class LoadSecret(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = os.path.join(self.tmp.name, "secrets.local.json")
        p = mock.patch.object(common, "SECRETS_PATH", self.path)
        p.start()
        self.addCleanup(p.stop)
        env = mock.patch.dict(os.environ, {}, clear=False)
        env.start()
        self.addCleanup(env.stop)
        os.environ.pop("S3_TEST_SECRET", None)

    def write(self, doc):
        with open(self.path, "w") as f:
            json.dump(doc, f)

    def test_env_wins_over_file(self):
        self.write({"S3_TEST_SECRET": "file"})
        os.environ["S3_TEST_SECRET"] = "env"
        self.assertEqual(common.load_secret("S3_TEST_SECRET"), "env")

    def test_file_when_no_env(self):
        self.write({"S3_TEST_SECRET": "file"})
        self.assertEqual(common.load_secret("S3_TEST_SECRET"), "file")

    def test_missing_or_bad_file_is_none(self):
        self.assertIsNone(common.load_secret("S3_TEST_SECRET"))
        with open(self.path, "w") as f:
            f.write("{not json")
        self.assertIsNone(common.load_secret("S3_TEST_SECRET"))
        self.write(["a list"])
        self.assertIsNone(common.load_secret("S3_TEST_SECRET"))
        self.write({"S3_TEST_SECRET": ""})
        self.assertIsNone(common.load_secret("S3_TEST_SECRET"))


class Paths(unittest.TestCase):
    def test_repo_dir_is_the_repo_root(self):
        self.assertEqual(os.path.realpath(common.REPO_DIR), os.path.realpath(helpers.REPO))
        self.assertTrue(os.path.isfile(os.path.join(common.REPO_DIR, "note.html")))
        self.assertTrue(os.path.isfile(os.path.join(common.REPO_DIR, "server.html")))

    def test_usage_url_is_built_from_the_port(self):
        self.assertEqual(common.USAGE_LOCAL_URL, "http://127.0.0.1:%d" % common.USAGE_PORT)


if __name__ == "__main__":
    unittest.main()
