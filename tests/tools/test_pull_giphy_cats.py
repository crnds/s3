"""pull_giphy_cats.py's pure filter step (no network, no gifsicle).

    /usr/bin/python3 -m unittest discover -s tests/tools
"""
import os
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools")
sys.path.insert(0, TOOLS)
import pull_giphy_cats as pg  # noqa: E402


def gif(gif_id, url="https://media.example/x.gif", title="t"):
    return {"id": gif_id, "title": title, "images": {"original": {"url": url}}}


class SelectNewGifsTest(unittest.TestCase):
    def test_keeps_order_and_fields(self):
        got = pg.select_new_gifs([gif("a", "u1", "A"), gif("b", "u2", "B")], set())
        self.assertEqual(got, [("a", "u1", "A"), ("b", "u2", "B")])

    def test_skips_seen_and_page_duplicates(self):
        got = pg.select_new_gifs([gif("a"), gif("b"), gif("b"), gif("c")], {"a"})
        self.assertEqual([g[0] for g in got], ["b", "c"])

    def test_does_not_mutate_seen(self):
        seen = {"a"}
        pg.select_new_gifs([gif("b")], seen)
        self.assertEqual(seen, {"a"})

    def test_skips_missing_id_or_url(self):
        results = [
            {"images": {"original": {"url": "u"}}},      # no id
            {"id": "n1"},                                # no images
            {"id": "n2", "images": None},
            {"id": "n3", "images": {"original": None}},
            {"id": "n4", "images": {"original": {}}},    # no url
            gif("ok"),
        ]
        self.assertEqual([g[0] for g in pg.select_new_gifs(results, set())], ["ok"])

    def test_default_title(self):
        self.assertEqual(pg.select_new_gifs([gif("a", title="")], set())[0][2], "Cat GIF")


class NameFiltersTest(unittest.TestCase):
    NAMES = ["giphy_abc.gif", "giphy_abc.gif.tmp", "cat_001.gif", "cat_002.png",
             "notes.txt", "giphy_.gif"]

    def test_raw_names(self):
        self.assertEqual(pg.raw_gif_names(self.NAMES), ["giphy_abc.gif", "giphy_.gif"])

    def test_board_names(self):
        self.assertEqual(pg.board_gif_names(self.NAMES), ["cat_001.gif"])


class ExistingRawGifsTest(unittest.TestCase):
    def test_counts_only_real_gifs(self):
        with tempfile.TemporaryDirectory() as d:
            files = {
                "giphy_good.gif": b"GIF89a" + b"\0" * 94,   # 100 bytes, counted
                "giphy_trunc.gif": b"<html>",               # not a GIF
                "giphy_empty.gif": b"",
                "cat_001.gif": b"GIF89a",                   # not a raw download
            }
            for name, data in files.items():
                with open(os.path.join(d, name), "wb") as f:
                    f.write(data)
            self.assertEqual(pg.get_existing_raw_gifs(d), (100, {"good"}))

    def test_missing_dir(self):
        self.assertEqual(pg.get_existing_raw_gifs("/nonexistent/s3-test"), (0, set()))


class FetchAndWriteOfflineTest(unittest.TestCase):
    """fetch / write with GIPHY, the CDN, sleeps and the resizer faked."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.raw = os.path.join(tmp.name, "raw")
        self.out = os.path.join(tmp.name, "out")
        os.makedirs(self.raw)
        for p in (mock.patch("builtins.print"), mock.patch.object(pg.time, "sleep")):
            p.start()
            self.addCleanup(p.stop)

    def test_fetch_downloads_new_ids_until_exhausted(self):
        pages = {"q": [gif("a", "ua"), gif("b", "ub"), gif("seen", "us")]}

        def search(key, query, limit, offset):
            return (pages[query] if offset == 0 else []), None, 200

        def download(url, dest):
            with open(dest, "wb") as f:
                f.write(b"GIF89a1234")
            return 10

        seen = {"seen"}
        with mock.patch.object(pg, "query_giphy_search", side_effect=search), \
                mock.patch.object(pg, "download_gif", side_effect=download) as dl:
            total = pg.fetch_raw_library("k", ["q"], self.raw, 10 ** 6, 0, seen)
        self.assertEqual(total, 20)
        self.assertEqual(seen, {"seen", "a", "b"})
        self.assertEqual([c.args[0] for c in dl.call_args_list], ["ua", "ub"])
        self.assertEqual(sorted(os.listdir(self.raw)), ["giphy_a.gif", "giphy_b.gif"])

    def test_write_fills_to_cap_and_skips_duplicates(self):
        for name, body in (("giphy_a.gif", b"A"), ("giphy_b.gif", b"B"),
                           ("giphy_dup.gif", b"A"), ("giphy_c.gif", b"C")):
            with open(os.path.join(self.raw, name), "wb") as f:
                f.write(b"GIF89a" + body)

        def fake_resize(resizer, binary, src, dst, *rest):
            with open(src, "rb") as s, open(dst, "wb") as d:
                d.write(s.read())
            return True

        with mock.patch.object(pg, "pick_resizer", return_value=("fake", "fake")), \
                mock.patch.object(pg, "resize_gif", side_effect=fake_resize):
            added = pg.write_board_gifs(self.raw, self.out, 3, 480, 320, 128, 40, 80)
            again = pg.write_board_gifs(self.raw, self.out, 3, 480, 320, 128, 40, 80)
        self.assertEqual(added, 3)
        self.assertEqual(again, 0)
        self.assertEqual(sorted(os.listdir(self.out)), ["cat_001.gif", "cat_002.gif", "cat_003.gif"])
        bodies = set()
        for n in os.listdir(self.out):
            with open(os.path.join(self.out, n), "rb") as f:
                bodies.add(f.read())
        self.assertEqual(bodies, {b"GIF89aA", b"GIF89aB", b"GIF89aC"})


if __name__ == "__main__":
    unittest.main()
