#!/usr/bin/env python3
"""Enlarge small cat GIFs so they fill the S3's 480x320 screen.

The S3 plays the CYD's own library (~/cyd/cats, 120 cats built by
~/cyd/prepare_cat_gifs.py to fit 320x240, many smaller still: 240x240,
320x180...). Copied as-is they would play as a small island in a black frame
on the 480x320 panel. This pass scales every GIF that fits inside 480x320 up until it
touches the box (aspect ratio kept), with gifsicle's Catmull-Rom filter, and
re-applies the same palette/lossy settings. Files already touching the box
are left alone, so it is safe to re-run after adding more cats.

    cp -R ~/cyd/cats ~/s3/cats                 # the CYD's 120 cats (<=320x240)
    python3 tools/fit_cats.py                  # upscale in place (./cats)
    COPYFILE_DISABLE=1 cp -r cats /Volumes/<SD>/cats

Requires gifsicle (brew install gifsicle) and Pillow (to read sizes).
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOX_W, BOX_H = 480, 320


def main():
    ap = argparse.ArgumentParser(description="Upscale small cat GIFs to touch 480x320.")
    ap.add_argument("--dir", default=os.path.join(ROOT, "cats"), help="library folder (default ./cats)")
    ap.add_argument("--colors", type=int, default=128, help="palette size (default 128, as prepare_cat_gifs.py)")
    ap.add_argument("--lossy", type=int, default=80, help="gifsicle lossy level (default 80, as prepare_cat_gifs.py)")
    ap.add_argument("--min-scale", type=float, default=1.05,
                    help="skip GIFs that would grow by less than this factor (default 1.05)")
    args = ap.parse_args()

    if not shutil.which("gifsicle"):
        print("Error: gifsicle not found -- brew install gifsicle")
        sys.exit(1)
    files = sorted(glob.glob(os.path.join(args.dir, "*.gif")))
    files = [f for f in files if not os.path.basename(f).startswith(".")]
    if not files:
        print(f"Error: no GIFs in {args.dir}")
        sys.exit(1)

    grown = 0
    for path in files:
        with Image.open(path) as im:
            w, h = im.size
        scale = min(BOX_W / w, BOX_H / h)
        if scale < args.min_scale:
            continue
        tmp = path + ".tmp"
        cmd = ["gifsicle", "-O3", "--resize-touch", f"{BOX_W}x{BOX_H}", "--resize-method", "catrom",
               "--colors", str(args.colors)]
        if args.lossy > 0:
            cmd.append(f"--lossy={args.lossy}")
        cmd += [path, "-o", tmp]
        r = subprocess.run(cmd, capture_output=True)
        if r.returncode != 0 or not os.path.exists(tmp):
            print(f"  {os.path.basename(path)}: gifsicle failed: {r.stderr.decode('utf-8', 'replace')[:160]}")
            if os.path.exists(tmp):
                os.remove(tmp)
            continue
        before = os.path.getsize(path)
        os.replace(tmp, path)
        with Image.open(path) as im:
            nw, nh = im.size
        print(f"  {os.path.basename(path)}: {w}x{h} -> {nw}x{nh}  "
              f"{before // 1024}KB -> {os.path.getsize(path) // 1024}KB")
        grown += 1

    total = sum(os.path.getsize(f) for f in files)
    print(f"upscaled {grown} of {len(files)} GIFs; library is {total / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
