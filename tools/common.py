"""Helpers shared by the scripts in tools/.

Tools are run by path (`python3 tools/x.py`), which puts tools/ at sys.path[0],
so a plain `import common` finds this file from any working directory. Stdlib
only. The PlatformIO hooks (embed_cats.py, ota_publish.py) don't use it.
"""

import http.client
import json
import os
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECRETS_PATH = os.path.join(ROOT, "secrets.local.json")

# ── GIF + GIFSICLE SETTINGS ───────────────────────────────
GIF_MAGIC = (b"GIF87a", b"GIF89a")

# The S3 panel, which every cat GIF is sized for.
BOX_W, BOX_H = 480, 320
# Palette, lossy level and frame cap shared by prepare_cat_gifs.py (download +
# fit) and fit_cats.py (upscale an already-prepared library).
GIF_COLORS = 128
GIF_LOSSY = 80
GIF_MAX_FRAMES = 40

UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) s3-tools/1.0"


def gifsicle_quality_args(colors=GIF_COLORS, lossy=GIF_LOSSY):
    """gifsicle palette/lossy flags; lossy <= 0 leaves lossy compression off."""
    args = ["--colors", str(colors)]
    if lossy and lossy > 0:
        args.append("--lossy=%d" % lossy)
    return args


# ── SECRETS ───────────────────────────────────────────────
def load_secret(name, path=None):
    """Token `name` from the env var of the same name, else from
    secrets.local.json at the repo root (gitignored). Returns None when neither
    has it -- a token is never hardcoded. An unreadable or malformed file
    counts as "not there" (warned on stderr when it exists but won't parse)."""
    value = os.environ.get(name)
    if value:
        return value
    path = path or SECRETS_PATH
    try:
        with open(path) as f:
            data = json.load(f)
    except FileNotFoundError:
        return None
    except (OSError, ValueError) as e:
        print("[warn] failed reading %s: %s: %s" % (os.path.basename(path), type(e).__name__, e),
              file=sys.stderr)
        return None
    if not isinstance(data, dict):
        return None
    return data.get(name) or None


# ── DOWNLOAD ──────────────────────────────────────────────
def download_gif(url, dest=None, timeout=30, user_agent=UA):
    """Fetch one GIF. Returns its bytes, or None on a network error, a body
    that is not a GIF, or (with `dest`) a failed write. With `dest` the bytes
    are also written there."""
    req = urllib.request.Request(url, headers={"User-Agent": user_agent})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = resp.read()
    except (urllib.error.URLError, http.client.HTTPException, OSError, ValueError) as e:
        # HTTPException covers IncompleteRead (a body cut off mid-transfer),
        # which is neither a URLError nor an OSError.
        print("    download failed: %s" % e)
        return None
    if not data.startswith(GIF_MAGIC):
        print("    skipped: response was not a GIF")
        return None
    if dest:
        try:
            with open(dest, "wb") as f:
                f.write(data)
        except OSError as e:
            print("    failed to write %s: %s" % (dest, e))
            return None
    return data
