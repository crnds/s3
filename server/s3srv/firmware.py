"""OTA firmware (S3 board, roadmap O9): the published build on offer, what each
board reports per poll, and the control panel's install requests.

This module owns its four dicts; every read and write of them goes through
_lock (HTTP requests run on ThreadingHTTPServer's threads)."""

import os
import threading
import time
from datetime import datetime, timezone

# `pio run -t publish` (tools/ota_publish.py) copies a build here; this server only offers
# what was published, never a half-finished `pio run`. The board compares the
# offered image digest with its own running image and pulls the file from
# /firmware/<board>.bin into its idle app slot. Outside the repo on purpose:
# a build carries the WiFi credentials compiled into it.
FIRMWARE_DIR = os.path.expanduser("~/.s3_firmware")
FIRMWARE_BOARDS = ("s3",)
FIRMWARE_MAX_BYTES = 6553600  # the S3's app slot (default_16MB.csv)
# The control panel's "Install on board": board -> {"id", "sha256", "ts"}. The
# id rides in the offer (firmware.<board>.install) until a board reports
# running that build, a newer one is published, or FIRMWARE_REQUEST_TTL_SEC.
FIRMWARE_REQUEST_TTL_SEC = 15 * 60
FIRMWARE_DOWNLOAD_RECENT_SEC = 180  # "installing" in the panel this long after a download

_lock = threading.Lock()
_firmware_cache = {}  # board -> ((mtime_ns, size), info)
_firmware_requests = {}  # board -> {"id", "sha256", "ts"}
# What each board reports on every poll (X-S3-Firmware / X-S3-OTA headers):
# ip -> {"sha256", "ota", "auto", "ts"}; and when each ip last downloaded.
_board_firmware = {}
_firmware_downloads = {}  # ip -> time.time() of the last /firmware/<board>.bin


def firmware_path(board):
    return os.path.join(FIRMWARE_DIR, board + ".bin")


def firmware_info(board):
    """{"sha256", "size", "published"} for a published image, or None.

    sha256 is the image digest esptool appends as the file's last 32 bytes --
    the same value the board reads for its own running app partition
    (esp_partition_get_sha256), so equal means "already running this build".
    Cached on (mtime, size): the board asks every poll, the file rarely moves.
    """
    path = firmware_path(board)
    try:
        st = os.stat(path)
    except OSError:
        return None
    key = (st.st_mtime_ns, st.st_size)
    with _lock:
        cached = _firmware_cache.get(board)
    if cached and cached[0] == key:
        return cached[1]
    info = None
    if 1024 < st.st_size <= FIRMWARE_MAX_BYTES:
        try:
            with open(path, "rb") as f:
                magic = f.read(1)
                f.seek(-32, os.SEEK_END)
                digest = f.read(32)
            if magic == b"\xe9" and len(digest) == 32:  # ESP image header magic
                info = {
                    "sha256": digest.hex(),
                    "size": st.st_size,
                    "published": datetime.fromtimestamp(st.st_mtime, timezone.utc).isoformat(),
                }
        except OSError:
            info = None
    with _lock:
        _firmware_cache[board] = (key, info)
    return info


def read_image(board):
    """The published image's bytes for GET /firmware/<board>.bin, or None."""
    if board not in FIRMWARE_BOARDS or not firmware_info(board):
        return None
    try:
        with open(firmware_path(board), "rb") as f:
            return f.read()
    except OSError:
        return None


def _pending_request(board, info):
    # Caller holds _lock.
    req = _firmware_requests.get(board)
    if not req or not info:
        return None
    if req["sha256"] != info["sha256"] or time.time() - req["ts"] > FIRMWARE_REQUEST_TTL_SEC:
        _firmware_requests.pop(board, None)
        return None
    return dict(req)


def firmware_request(board, info):
    """The pending install request for this published build, or None."""
    with _lock:
        return _pending_request(board, info)


def firmware_offer():
    offer = {}
    for board in FIRMWARE_BOARDS:
        info = firmware_info(board)
        if info:
            offer[board] = dict(info)
            req = firmware_request(board, info)
            if req:
                offer[board]["install"] = req["id"]
    return offer


def note_board_firmware(ip, sha256, ota):
    """Record a board's poll headers (X-S3-OTA: "<state>; auto=<0|1>; req=<id>").

    A request is done once a board runs its build, or reports it has acted on
    that request id (req=) and isn't running it -- the install failed, and the
    panel should offer it again rather than show it pending.
    """
    if not sha256 or len(sha256) != 64:
        return
    sha256 = sha256.lower()
    state, auto, handled = ota.split(";")[0].strip(), None, None
    for part in ota.split(";")[1:]:
        key, _, value = part.strip().partition("=")
        if key == "auto":
            auto = value == "1"
        elif key == "req" and value.isdigit():
            handled = int(value)
    with _lock:
        _board_firmware[ip] = {"sha256": sha256, "ota": state[:16], "auto": auto, "ts": time.time()}
        for board, req in list(_firmware_requests.items()):
            if req["sha256"] == sha256 or req["id"] == handled:
                _firmware_requests.pop(board, None)


def note_download(ip):
    with _lock:
        _firmware_downloads[ip] = time.time()


def firmware_status():
    """GET /api/firmware (localhost): the control panel's Firmware section."""
    now = time.time()
    info = firmware_info("s3")
    boards = []
    with _lock:
        for ip, fw in sorted(_board_firmware.items(), key=lambda kv: -kv[1]["ts"]):
            dl = _firmware_downloads.get(ip)
            boards.append({
                "ip": ip,
                "sha256": fw["sha256"],
                "ota": fw["ota"],
                "auto": fw["auto"],
                "last_report_sec": int(now - fw["ts"]),
                "download_sec": int(now - dl) if dl and now - dl <= FIRMWARE_DOWNLOAD_RECENT_SEC else None,
            })
        req = _pending_request("s3", info)
    offer = None
    if info:
        offer = dict(info)
        if req:
            offer["install"] = req["id"]
    return {
        "offer": offer,
        "request": {"id": req["id"], "age_sec": int(now - req["ts"])} if req else None,
        "boards": boards,
        "slot_bytes": FIRMWARE_MAX_BYTES,
    }


def request_firmware_install(board="s3"):
    info = firmware_info(board)
    if not info:
        return False, "no build published"
    with _lock:
        _firmware_requests[board] = {"id": int(time.time()), "sha256": info["sha256"], "ts": time.time()}
    return True, "the board installs %s on its next poll" % info["sha256"][:12]
