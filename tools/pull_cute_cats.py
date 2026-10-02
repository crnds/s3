#!/usr/bin/env python3
"""Fetch candidate cute-cartoon-cat GIFs from GIPHY and build contact sheets to review.

Nothing is written into cats/. Candidates go to a work dir (default: the session
scratchpad); pick the keepers by eye from the sheets, then copy them into cats/ and
run tools/fit_cats.py.

    python3 tools/pull_cute_cats.py --out /tmp/cutecats

The API key comes from GIPHY_API_KEY or ~/cyd/secrets.local.json (the same lookup
~/cyd/pull_giphy_cats.py uses; its helpers are reused here).
"""
import argparse
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.expanduser("~/cyd"))
from pull_giphy_cats import load_giphy_api_key, query_giphy_search, download_gif  # noqa: E402

from PIL import Image, ImageDraw  # noqa: E402

QUERIES = [
    "kawaii cat cartoon",
    "cute cartoon cat animation",
    "chibi cat",
    "pusheen",
    "cute cat illustration loop",
    "cat sticker animated",
    "kawaii kitten",
    "cute cat cartoon loop",
]
MAX_BYTES = 400 * 1024
SKIP_WORDS = ()
MIN_FRAMES, MAX_FRAMES = 2, 40
MIN_SIDE = 200
THUMB = 150
COLS, ROWS = 5, 4


# ── FETCH ─────────────────────────────────────────────────
def pick_candidates(key, per_query, limit, seen_ids=()):
    seen, picked = set(seen_ids), []
    for q in QUERIES:
        results, _, code = query_giphy_search(key, q, limit=50)
        if code != 200:
            print(f"[{q}] HTTP {code}, skipping")
            if code in (401, 403):
                sys.exit("GIPHY rejected the key.")
            continue
        n = 0
        for g in results:
            o = g.get("images", {}).get("original", {})
            try:
                size, frames = int(o["size"]), int(o["frames"])
                w, h = int(o["width"]), int(o["height"])
            except (KeyError, ValueError):
                continue
            if g["id"] in seen or not o.get("url"):
                continue
            if any(w in g.get("title", "").lower() for w in SKIP_WORDS):
                continue
            if size > MAX_BYTES or not MIN_FRAMES <= frames <= MAX_FRAMES or min(w, h) < MIN_SIDE:
                continue
            seen.add(g["id"])
            picked.append({"id": g["id"], "title": g.get("title", ""), "url": o["url"],
                           "query": q, "w": w, "h": h, "frames": frames, "bytes": size})
            n += 1
            if n >= per_query or len(picked) >= limit:
                break
        print(f"[{q}] kept {n}")
        time.sleep(1.0)
        if len(picked) >= limit:
            break
    return picked


def download_all(cands, outdir):
    hashes, kept = set(), []
    for c in cands:
        path = os.path.join(outdir, f"giphy_{c['id']}.gif")
        if not os.path.exists(path) and not download_gif(c["url"], path):
            continue
        digest = hashlib.md5(open(path, "rb").read()).hexdigest()
        if digest in hashes:
            os.remove(path)
            continue
        hashes.add(digest)
        c["file"] = path
        kept.append(c)
        time.sleep(0.4)
    return kept


# ── CONTACT SHEETS ────────────────────────────────────────
def frame(path, which):
    im = Image.open(path)
    im.seek(min(which, im.n_frames - 1))
    return im.convert("RGB")


def sheets(cands, outdir):
    per = COLS * ROWS
    paths = []
    for s in range(0, len(cands), per):
        chunk = cands[s:s + per]
        cell_w, cell_h = THUMB * 2 + 12, THUMB + 18
        sheet = Image.new("RGB", (COLS * cell_w, ROWS * cell_h), (30, 30, 30))
        d = ImageDraw.Draw(sheet)
        for i, c in enumerate(chunk):
            x, y = (i % COLS) * cell_w, (i // COLS) * cell_h
            im = Image.open(c["file"])
            for k, which in enumerate((0, im.n_frames // 2)):
                t = frame(c["file"], which)
                t.thumbnail((THUMB, THUMB))
                sheet.paste(t, (x + 4 + k * (THUMB + 4), y + 16))
            d.text((x + 4, y + 2), f"#{s + i}  {c['w']}x{c['h']} {c['frames']}f {c['bytes'] // 1024}KB", fill=(255, 255, 255))
        p = os.path.join(outdir, f"sheet_{s // per + 1}.png")
        sheet.save(p)
        paths.append(p)
    return paths


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="work dir for downloads + sheets (not cats/)")
    ap.add_argument("--per-query", type=int, default=10)
    ap.add_argument("--limit", type=int, default=60)
    ap.add_argument("--queries", help="comma-separated search terms (replaces the built-in list)")
    ap.add_argument("--max-kb", type=int, help="skip GIFs larger than this (default 400)")
    ap.add_argument("--skip-words", default="", help="comma-separated title words to drop, e.g. pusheen")
    ap.add_argument("--exclude", help="candidates.json from an earlier run; its ids are skipped")
    args = ap.parse_args()

    global QUERIES, MAX_BYTES, SKIP_WORDS
    if args.queries:
        QUERIES = [q.strip() for q in args.queries.split(",") if q.strip()]
    if args.max_kb:
        MAX_BYTES = args.max_kb * 1024
    SKIP_WORDS = tuple(w.strip().lower() for w in args.skip_words.split(",") if w.strip())
    prior = {c["id"] for c in json.load(open(args.exclude))} if args.exclude else set()

    key = load_giphy_api_key()
    if not key:
        sys.exit("No GIPHY_API_KEY (env or ~/cyd/secrets.local.json).")
    os.makedirs(args.out, exist_ok=True)

    cands = download_all(pick_candidates(key, args.per_query, args.limit, prior), args.out)
    with open(os.path.join(args.out, "candidates.json"), "w") as f:
        json.dump(cands, f, indent=1)
    for p in sheets(cands, args.out):
        print("sheet:", p)
    print(f"{len(cands)} candidates in {args.out}")


if __name__ == "__main__":
    main()
