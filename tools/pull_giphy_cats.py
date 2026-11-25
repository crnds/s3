#!/usr/bin/env python3
"""Download and manage a large library of cat GIFs from GIPHY.

Queries GIPHY for several categories of cat GIFs (cartoons, memes, kawaii,
etc.) round-robin, downloads the raw (original size) GIFs into `cats_raw/`
until a target size (default 2.5 GB) is reached, then optionally fits a subset
(default: up to 120 files) to the S3's 480x320 screen with
prepare_cat_gifs.py's resizer.

Three steps, one function each: fetch (`fetch_raw_library`: GIPHY search +
downloads), filter (`select_new_gifs`, `raw_gif_names`: pure, unit-tested)
and write (`write_board_gifs`: resize into the board folder).

It respects GIPHY's rate limits with polite delays and exponential backoff on
429, and it is resumable across runs. The API key comes from GIPHY_API_KEY or
secrets.local.json at the repo root (tools/common.py's load_secret).
"""

import argparse
import hashlib
import json
import os
import random
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

import common
# Resizing helpers live next to this script; running it by path puts tools/ on
# sys.path, so this import cannot silently degrade.
from prepare_cat_gifs import (
    pick_resizer,
    resize_gif,
    next_index,
    existing_hashes,
)

GIPHY_MAX_OFFSET = 4999  # GIPHY search caps offset+limit around 5000 results total
PAGE_SIZE = 50
MAX_API_ERRORS = 5       # consecutive failed searches before downloads stop
DEFAULT_QUERIES = (
    "cartoon cat,kawaii cat,pusheen,cat meme,funny cat,cat animation,simons cat,"
    "garfield,grumpy cat,anime cat,pixel cat,cute kitten,nyan cat,chibi cat,"
    "cat play,cat fail,crying cat,space cat,sleeping cat,cat wiggle"
)


def load_giphy_api_key():
    """GIPHY_API_KEY from the env, else secrets.local.json (repo root)."""
    return common.load_secret("GIPHY_API_KEY")


def format_size(size_bytes):
    """Format bytes to human readable format."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size_bytes < 1024.0:
            return f"{size_bytes:.2f} {unit}"
        size_bytes /= 1024.0
    return f"{size_bytes:.2f} TB"


# ── FILTER (pure) ─────────────────────────────────────────
def select_new_gifs(results, seen_ids):
    """The GIPHY search results worth downloading, in order: each has an id
    not in `seen_ids` (nor earlier in this page) and an original-size URL.
    Returns [(id, url, title)]."""
    picked, ids = [], set(seen_ids)
    for gif in results:
        gif_id = gif.get("id")
        if not gif_id or gif_id in ids:
            continue
        url = ((gif.get("images") or {}).get("original") or {}).get("url")
        if not url:
            continue
        ids.add(gif_id)
        picked.append((gif_id, url, gif.get("title") or "Cat GIF"))
    return picked


def raw_gif_names(names):
    """The downloaded GIPHY files (giphy_<id>.gif) among `names`."""
    return [n for n in names if n.startswith("giphy_") and n.endswith(".gif")]


def board_gif_names(names):
    """The board-ready files (cat_NNN.gif) among `names`."""
    return [n for n in names if n.startswith("cat_") and n.endswith(".gif")]


# ── FETCH ─────────────────────────────────────────────────
def get_existing_raw_gifs(raw_dir):
    """Scan raw_dir for downloaded GIPHY files. Returns (total_bytes, seen_ids)."""
    total_bytes = 0
    seen_ids = set()
    if not os.path.isdir(raw_dir):
        return 0, seen_ids

    for name in raw_gif_names(os.listdir(raw_dir)):
        path = os.path.join(raw_dir, name)
        try:
            size = os.path.getsize(path)
            if size == 0:
                continue
            # A truncated download (e.g. killed mid-write on a previous run)
            # can leave a non-empty file that isn't a real GIF -- size alone
            # would count it toward the target and toward seen_ids,
            # permanently skipping a re-download of that id.
            with open(path, "rb") as f:
                header = f.read(6)
            if not header.startswith(common.GIF_MAGIC):
                continue
            total_bytes += size
            seen_ids.add(name[6:-4])  # 'giphy_<id>.gif' -> id
        except OSError:
            pass
    return total_bytes, seen_ids


def query_giphy_search(api_key, query, limit=PAGE_SIZE, offset=0, timeout=20):
    """Query GIPHY API search endpoint for a specific query and offset.

    Returns:
        (results_list, rate_limit_remaining, err_code)
    """
    safe_query = urllib.parse.quote(query)
    url = f"https://api.giphy.com/v1/gifs/search?api_key={api_key}&q={safe_query}&limit={limit}&offset={offset}&rating=pg-13"

    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) pull_giphy_cats/1.0"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            rate_remaining = resp.headers.get("X-RateLimit-Remaining")
            if rate_remaining is not None:
                try:
                    rate_remaining = int(rate_remaining)
                except ValueError:
                    rate_remaining = None

            data = json.loads(resp.read().decode("utf-8"))
            return data.get("data", []), rate_remaining, 200
    except urllib.error.HTTPError as e:
        print(f"\n[GIPHY API HTTP Error] Status Code: {e.code}")
        # Return code so we can handle 429 specifically
        return [], None, e.code
    except Exception as e:
        print(f"\n[GIPHY API Network/Parse Error] {e}")
        return [], None, 500


def download_gif(url, dest_path, timeout=30):
    """Download one GIF from the GIPHY CDN to dest_path. Returns its size in
    bytes, or None."""
    data = common.download_gif(url, dest_path, timeout=timeout, user_agent="Mozilla/5.0")
    return len(data) if data else None


def fetch_raw_library(api_key, queries, raw_dir, target_bytes, total_bytes, seen_ids,
                      api_sleep=2.0, dl_sleep=0.4):
    """Round-robin GIPHY searches over `queries`, downloading new GIFs into
    raw_dir until total_bytes reaches target_bytes or every query is
    exhausted. Adds each downloaded id to `seen_ids`; returns the new total."""
    target_label = format_size(target_bytes)
    query_offsets = {q: 0 for q in queries}
    exhausted = set()
    consecutive_api_errors = 0

    while total_bytes < target_bytes and len(exhausted) < len(queries):
        for query in [q for q in queries if q not in exhausted]:
            if total_bytes >= target_bytes:
                break

            offset = query_offsets[query]
            if offset > GIPHY_MAX_OFFSET:
                # Past this, GIPHY returns an error rather than an empty page,
                # which would otherwise count toward the *global*
                # consecutive_api_errors abort instead of just retiring this
                # one query.
                print(f"    Offset {offset} past GIPHY's ~{GIPHY_MAX_OFFSET} cap. Query '{query}' is exhausted.")
                exhausted.add(query)
                continue
            print(f"\n--> Querying GIPHY for '{query}' (offset: {offset})...")

            results, rate_rem, code = query_giphy_search(api_key, query, limit=PAGE_SIZE, offset=offset)

            if code == 429:
                consecutive_api_errors += 1
                sleep_time = min(60 * (2 ** (consecutive_api_errors - 1)), 600)
                print(f"    [Rate Limited (429)] Sleeping for {sleep_time} seconds before retrying...")
                time.sleep(sleep_time)
                continue  # same offset next round; not exhausted
            if code != 200:
                consecutive_api_errors += 1
                if consecutive_api_errors >= MAX_API_ERRORS:
                    print("    Too many consecutive API errors. Stopping downloads.")
                    break
                time.sleep(5.0)
                continue

            consecutive_api_errors = 0
            query_offsets[query] += len(results)
            if rate_rem is not None:
                print(f"    [API Quota] X-RateLimit-Remaining: {rate_rem}")
            if not results:
                print(f"    No results returned. Query '{query}' is exhausted.")
                exhausted.add(query)
                continue
            if len(results) < PAGE_SIZE:
                print(f"    Returned {len(results)} results (less than page limit). Query '{query}' is exhausted.")
                exhausted.add(query)

            downloaded = 0
            for gif_id, url, title in select_new_gifs(results, seen_ids):
                if total_bytes >= target_bytes:
                    break
                dest_file = os.path.join(raw_dir, f"giphy_{gif_id}.gif")
                pct = (total_bytes / target_bytes) * 100
                print(f"[{format_size(total_bytes)} / {target_label} ({pct:.1f}%)] Downloading ID: {gif_id}...")
                time.sleep(dl_sleep)  # be gentle on the CDN
                file_size = download_gif(url, dest_file)
                if file_size:
                    total_bytes += file_size
                    seen_ids.add(gif_id)
                    downloaded += 1
                    print(f"    Saved: {os.path.basename(dest_file)} ({format_size(file_size)}) - \"{title}\"")

            print(f"--> Completed batch for '{query}'. Downloaded {downloaded} new GIFs.")
            if total_bytes < target_bytes:
                time.sleep(api_sleep)

        if consecutive_api_errors >= MAX_API_ERRORS:
            break
    return total_bytes


# ── WRITE ─────────────────────────────────────────────────
def write_board_gifs(raw_dir, out_dir, resize_count, width, height, colors, max_frames, lossy):
    """Fit random raw GIFs into out_dir as cat_NNN.gif until it holds
    `resize_count`, skipping any whose fitted bytes are already there.
    Returns how many were added."""
    os.makedirs(out_dir, exist_ok=True)
    current = len(board_gif_names(os.listdir(out_dir)))
    print(f"Current board-ready GIFs in '{out_dir}/' : {current}")
    print(f"Library cap (--resize-count)              : {resize_count}")

    if current >= resize_count:
        print(f"\n[Warning] Board-ready folder already has {current} GIFs (cap {resize_count}).")
        print("To load a fresh batch, clear or rename it first, e.g.:")
        print(f"  mv {out_dir} {out_dir}_old")
        return 0

    resizer, binary = pick_resizer()  # exits with install hints if none
    print(f"Using resizer: {resizer} ({binary})")

    slots = resize_count - current
    print(f"Targeting to generate {slots} new resized GIFs for the board.")

    raw_files = raw_gif_names(os.listdir(raw_dir)) if os.path.isdir(raw_dir) else []
    if not raw_files:
        print("No raw GIFs found to resize!")
        return 0

    resized_hashes = existing_hashes(out_dir)
    print(f"Indexed {len(resized_hashes)} hashes from existing board GIFs to prevent duplicates.")

    random.shuffle(raw_files)  # a randomized, diverse set
    idx = next_index(out_dir)
    added = 0

    print("\nResizing files...")
    for raw_name in raw_files:
        if added >= slots:
            break
        src_path = os.path.join(raw_dir, raw_name)
        dst_name = f"cat_{idx:03d}.gif"
        dst_path = os.path.join(out_dir, dst_name)
        print(f"[{added + 1}/{slots}] Resizing {raw_name} -> {dst_name}...")

        # Resize into a temp file first so a duplicate never lands in out_dir.
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dst = os.path.join(tmp_dir, "temp_resized.gif")
            if not resize_gif(resizer, binary, src_path, tmp_dst,
                              width, height, colors, max_frames, lossy):
                print("    Skipped: Resizer failed on this file")
                continue
            try:
                with open(tmp_dst, "rb") as f:
                    out_hash = hashlib.md5(f.read()).hexdigest()
            except OSError:
                continue
            if out_hash in resized_hashes:
                print("    Skipped: Resized content already exists on board (duplicate)")
                continue
            try:
                shutil.copy2(tmp_dst, dst_path)
            except OSError as e:
                print(f"    Failed to copy to destination: {e}")
                continue

        resized_hashes.add(out_hash)
        print(f"    Saved: {dst_name} ({os.path.getsize(dst_path) // 1024} KB)")
        idx += 1
        added += 1

    print("\n" + "=" * 60)
    print("                    RESIZE SUMMARY")
    print("=" * 60)
    print(f"Added Resized GIFs : {added}")
    print(f"Total in folder    : {current + added} / {resize_count}")
    print(f"Output Directory   : {out_dir}/")
    print("=" * 60)
    return added


# ── MAIN ──────────────────────────────────────────────────
def parse_args(argv=None):
    ap = argparse.ArgumentParser(description="Download and resize cat GIFs from GIPHY.")
    ap.add_argument("--api-key",
                    help="GIPHY API key. Prefer the GIPHY_API_KEY env var or "
                         "secrets.local.json's GIPHY_API_KEY field over this flag -- "
                         "a CLI arg is visible to any local user via `ps`/shell history.")
    ap.add_argument("--target-gb", type=float, default=2.5, help="Target library size in GB (default: 2.5)")
    ap.add_argument("--raw-dir", default="cats_raw", help="Directory for raw GIPHY GIFs (default: cats_raw)")
    ap.add_argument("--out-dir", default="cats", help="Board-ready resized folder (default: ./cats)")
    ap.add_argument("--skip-resize", action="store_true", help="Skip resizing/optimizing for the board")
    ap.add_argument("--resize-count", type=int, default=120,
                    help="Max number of GIFs to have resized for the board (default: 120)")

    # Resizing configuration (passed to prepare_cat_gifs' resizer)
    ap.add_argument("--width", type=int, default=common.BOX_W, help="max width (default: %d)" % common.BOX_W)
    ap.add_argument("--height", type=int, default=common.BOX_H, help="max height (default: %d)" % common.BOX_H)
    ap.add_argument("--colors", type=int, default=common.GIF_COLORS,
                    help="palette colors (default: %d)" % common.GIF_COLORS)
    ap.add_argument("--max-frames", type=int, default=common.GIF_MAX_FRAMES,
                    help="max frames (default: %d)" % common.GIF_MAX_FRAMES)
    ap.add_argument("--lossy", type=int, default=common.GIF_LOSSY,
                    help="gifsicle lossy optimization (default: %d)" % common.GIF_LOSSY)

    # Rate limiting configuration
    ap.add_argument("--api-sleep", type=float, default=2.0, help="Pause (seconds) between API queries (default: 2.0)")
    ap.add_argument("--dl-sleep", type=float, default=0.4, help="Pause (seconds) between GIF downloads (default: 0.4)")
    ap.add_argument("--queries", default=DEFAULT_QUERIES,
                    help="Comma-separated search terms to query from GIPHY")
    return ap.parse_args(argv)


def main():
    args = parse_args()
    api_key = args.api_key or load_giphy_api_key()
    if not api_key:
        print("Error: no GIPHY API key found. Set the GIPHY_API_KEY env var, add "
              "\"GIPHY_API_KEY\" to secrets.local.json, or pass --api-key.")
        sys.exit(1)

    queries = [q.strip() for q in args.queries.split(",") if q.strip()]
    if not queries:
        print("Error: No search queries provided.")
        sys.exit(1)

    target_bytes = int(args.target_gb * 1024 * 1024 * 1024)
    os.makedirs(args.raw_dir, exist_ok=True)

    print("=" * 60)
    print("                GIPHY CAT GIF DOWNLOADER")
    print("=" * 60)
    print(f"Target Library Size : {args.target_gb} GB ({target_bytes:,} bytes)")
    print(f"Raw Storage Dir     : {args.raw_dir}/")
    print(f"Resized Output Dir  : {args.out_dir}/")
    print(f"Queries (Round Robin): {len(queries)} categories")
    print("-" * 60)

    total_bytes, seen_ids = get_existing_raw_gifs(args.raw_dir)
    print(f"Found {len(seen_ids)} existing GIPHY GIFs in '{args.raw_dir}/'")
    print(f"Current library size: {format_size(total_bytes)} / {args.target_gb} GB")

    if total_bytes >= target_bytes:
        print("\nTarget size already met! Skipping downloads.")
    else:
        total_bytes = fetch_raw_library(api_key, queries, args.raw_dir, target_bytes,
                                        total_bytes, seen_ids, args.api_sleep, args.dl_sleep)

    print("\n" + "=" * 60)
    print("                   DOWNLOAD SUMMARY")
    print("=" * 60)
    print(f"Final Library Size: {format_size(total_bytes)}")
    print(f"Total Unique GIFs  : {len(seen_ids)}")
    print(f"Location           : {args.raw_dir}/")
    print("-" * 60)

    if args.skip_resize:
        print("Skipping resizing step as requested.")
        return
    print("\nStarting resizing sweep for the board...")
    write_board_gifs(args.raw_dir, args.out_dir, args.resize_count, args.width, args.height,
                     args.colors, args.max_frames, args.lossy)


if __name__ == "__main__":
    main()
