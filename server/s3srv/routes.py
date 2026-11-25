"""The usage server's HTTP side (:8787): the route tables, the handlers, the
poll heartbeat log and start_http_server().

GET /api/usage is open to the LAN (the board polls it); the note, firmware
status and power routes are localhost-only; GET /firmware/<board>.bin goes to
this Mac and to IPs that have polled."""

import re
import threading
import time

from s3srv.common import USAGE_PORT, JsonHandler, Server, log_err, log_out
from s3srv.firmware import (FIRMWARE_BOARDS, firmware_status, note_board_firmware,
                            note_download, read_image, request_firmware_install)
from s3srv.notes import NOTE_ALLOWED_ORIGINS, NOTE_HTML_PATH, note_snapshot, sanitize_note, set_note
from s3srv.power import power_snapshot, set_battery_save_manual
from s3srv.state import is_known_client, note_client

# Browser pages that may read GET /api/usage cross-origin, besides the file://
# case (Origin: null): pages served from this Mac's own loopback, any port --
# e.g. ~/s3's simulator-s3.html under `python3 -m http.server 8765`. Matched
# exactly, never "*": a website's JS can't send a loopback Origin, so no other
# site can read the usage data.
USAGE_CORS_ORIGIN_RE = re.compile(r"http://(localhost|127\.0\.0\.1|\[::1\])(:[0-9]{1,5})?")

HEARTBEAT_LOG_INTERVAL_SEC = 300
# Gap reporting. The throttled heartbeat keeps the log small, but it also
# made real outages invisible: at any poll interval the log looks like one
# tidy line every 300s whether the board polled 60 times in between or zero.
# That is why a "board goes OFFLINE for 1-2 minutes" report could not be
# confirmed or dated from this log at all.
#
# So: also emit one line whenever a client returns after a suspiciously long
# silence. The board's poll interval is not known here, so it is inferred as
# the smallest gap ever seen from that IP (a client cannot poll faster than its
# own interval). A gap of GAP_MULTIPLE times that, and at least
# GAP_MIN_SEC (the firmware's own offline grace, OFFLINE_AFTER_MS), means the
# board almost certainly showed the OFFLINE screen. Low volume by construction:
# silent while things are healthy, one line per actual episode.
GAP_MIN_SEC = 60
GAP_MULTIPLE = 3
_poll_log_lock = threading.Lock()
_last_heartbeat_log = {}  # ip -> monotonic time of last heartbeat line logged
_last_seen_mono = {}  # ip -> monotonic time of that client's previous request
_min_gap_seen = {}    # ip -> smallest inter-request gap, i.e. inferred poll interval


def log_poll(ip, now_mono=None):
    """Heartbeat line for a LAN client's /api/usage poll, throttled to once
    per HEARTBEAT_LOG_INTERVAL_SEC per client -- at the board's ~20s poll
    cadence an unthrottled line grows /tmp/s3usage.log by ~200KB/day for no
    benefit, since `tail -f` just needs proof-of-life -- plus one RESUMED
    line per outage (see GAP_MIN_SEC)."""
    now_mono = time.monotonic() if now_mono is None else now_mono
    lines = []
    with _poll_log_lock:
        prev_seen = _last_seen_mono.get(ip)
        _last_seen_mono[ip] = now_mono
        gap = None if prev_seen is None else now_mono - prev_seen
        if gap is not None:
            floor = _min_gap_seen.get(ip)
            # Update the inferred poll interval before testing, so the very
            # first gaps (which set the baseline) can't read as an outage.
            if floor is None or gap < floor:
                _min_gap_seen[ip] = gap
                floor = gap
            if gap >= GAP_MIN_SEC and gap >= floor * GAP_MULTIPLE:
                # Stamping the heartbeat here also suppresses the routine
                # line below, so an episode reports exactly once.
                _last_heartbeat_log[ip] = now_mono
                lines.append(f"{ip} GET /api/usage  RESUMED after {gap:.0f}s gap "
                             f"(polls ~every {floor:.0f}s) -- board was likely OFFLINE")
        last = _last_heartbeat_log.get(ip, 0.0)
        if now_mono - last >= HEARTBEAT_LOG_INTERVAL_SEC:
            _last_heartbeat_log[ip] = now_mono
            lines.append(f"{ip} GET /api/usage")
    for line in lines:
        log_out(line)


class Handler(JsonHandler):
    # The /api/usage document; start_http_server() sets it
    # (usage_server.build_report).
    report_fn = None

    # Exact paths, then prefixes; anything else is a bare 404.
    GET_ROUTES = {
        "/": "_get_note_html",
        "/note": "_get_note_html",
        "/note.html": "_get_note_html",
        "/api/note": "_get_note",
        "/api/firmware": "_get_firmware_status",
        "/api/usage": "_get_usage",
    }
    GET_PREFIX_ROUTES = (("/firmware/", "_get_firmware_image"),)
    POST_ROUTES = {
        "/api/note": "_post_note",
        "/api/firmware/install": "_post_firmware_install",
        "/api/battery-save": "_post_battery_save",
    }

    def _dispatch(self, routes, prefixes=()):
        name = routes.get(self.path)
        if name is None:
            name = next((n for p, n in prefixes if self.path.startswith(p)), None)
        if name is None:
            self.send_response(404)
            self.end_headers()
            return
        getattr(self, name)()

    def do_GET(self):
        self._dispatch(self.GET_ROUTES, self.GET_PREFIX_ROUTES)

    def do_POST(self):
        self._dispatch(self.POST_ROUTES)

    def do_OPTIONS(self):
        # Only the file:// (Origin: null) case ever preflights: note.html is
        # served same-origin, and same-origin requests skip CORS entirely.
        if self.headers.get("Origin") == "null" and self.path == "/api/note":
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "null")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()
            return
        self.send_response(404)
        self.end_headers()

    def _cors_origin(self):
        # ACAO only for the dev pages: the file:// case (Origin: null) --
        # simulator-s3.html fetches this endpoint directly from disk -- and, for
        # /api/usage only, a page served from this Mac's loopback
        # (USAGE_CORS_ORIGIN_RE). A blanket "*" would let any website's JS read
        # board usage data cross-origin over the LAN/loopback; nothing else
        # needs CORS here (the board is not a browser, and server.html goes
        # through control_server's proxy instead).
        origin = self.headers.get("Origin")
        if origin == "null" or (self.path == "/api/usage" and origin and USAGE_CORS_ORIGIN_RE.fullmatch(origin)):
            return origin
        return None

    # ── guards: each sends the 403 itself and returns False ──
    def _local_only(self, text=False):
        if self._is_local():
            return True
        if text:
            self._send(b"localhost only", "text/plain", status=403)
        else:
            self._send_json({"ok": False, "detail": "localhost only"}, status=403)
        return False

    def _no_origin(self):
        # For routes whose only legitimate caller is control_server.py's
        # server-side urllib proxy, which never sends an Origin header. A
        # same-machine browser tab (any website, not just server.html) CAN
        # still reach 127.0.0.1 directly regardless of its own origin -- the IP
        # check doesn't stop that CSRF path -- so any request carrying an
        # Origin header at all is rejected.
        if self.headers.get("Origin") is None:
            return True
        self._send_json({"ok": False, "detail": "forbidden origin"}, status=403)
        return False

    # ── GET ──
    # The note routes are localhost-only, matching /api/battery-save: the
    # note is writable only from this Mac, so serving the editor to the LAN
    # would just be a form that 403s on save.
    def _get_note_html(self):
        if not self._local_only(text=True):
            return
        # Read fresh from disk per request (same as control_server.py) — this
        # is a single local page, not a hot path, and it means editing
        # note.html doesn't need a server restart.
        try:
            with open(NOTE_HTML_PATH, "rb") as f:
                self._send(f.read(), "text/html; charset=utf-8")
        except OSError:
            self._send(b"note.html not found", "text/plain", status=404)

    def _get_note(self):
        if self._local_only():
            self._send_json(note_snapshot())

    def _get_firmware_status(self):
        if self._local_only():
            self._send_json(firmware_status())

    def _get_firmware_image(self):
        # Only to this Mac and to boards that poll /api/usage: an image holds
        # the WiFi credentials compiled into it, so it isn't for the whole LAN.
        name = self.path[len("/firmware/"):]
        board = name[:-4] if name.endswith(".bin") else ""
        ip = self.client_address[0]
        if board not in FIRMWARE_BOARDS:
            self._send(b"not found", "text/plain", status=404)
            return
        if not (self._is_local() or is_known_client(ip)):
            self._send(b"poll /api/usage first", "text/plain", status=403)
            return
        body = read_image(board)
        if body is None:
            self._send(b"no build published", "text/plain", status=404)
            return
        note_download(ip)
        log_out(f"{ip} GET /firmware/{board}.bin ({len(body)} bytes)")
        self._send(body, "application/octet-stream")

    def _get_usage(self):
        # Only /api/usage counts as a client poll: the client registry drives
        # active_now and un-parks the activity-gated background loops, and a
        # browser loading the note editor is not a dashboard client.
        ip = self.client_address[0]
        note_client(ip)
        # The S3 reports its running build + OTA state on every poll.
        if self.headers.get("X-S3-Firmware"):
            note_board_firmware(ip, self.headers.get("X-S3-Firmware", ""), self.headers.get("X-S3-OTA", ""))
        if ip not in ("127.0.0.1", "::1"):
            log_poll(ip)
        self._send_json(self.report_fn())

    # ── POST ──
    def _post_note(self):
        # Note text from note.html. Localhost-only, same policy as
        # /api/battery-save: anyone on the LAN can *read* the note (it rides
        # in /api/usage, which is how the board gets it), but only this Mac
        # can change what's on the screen.
        if not self._local_only():
            return
        # Unlike /api/battery-save, an Origin header is expected here —
        # note.html is served from this origin, and browsers send Origin on
        # same-origin POSTs. Allowlist instead of blanket-reject, which still
        # blocks the CSRF path (a random site's origin isn't in the set).
        origin = self.headers.get("Origin")
        if origin is not None and origin not in NOTE_ALLOWED_ORIGINS:
            self._send_json({"ok": False, "detail": "forbidden origin"}, status=403)
            return
        doc, err = self._read_json_body()
        if err:
            return
        if not isinstance(doc, dict) or not isinstance(doc.get("text"), str):
            self._send_json(
                {"ok": False, "detail": "body must be {\"text\": str, \"size\": 1-3}"},
                status=400,
            )
            return
        size = doc.get("size", 1)
        if not isinstance(size, int) or isinstance(size, bool) or not 1 <= size <= 3:
            self._send_json(
                {"ok": False, "detail": "size must be an int 1-3"}, status=400
            )
            return
        note = {"text": sanitize_note(doc["text"]), "size": size}
        set_note(note)
        self._send_json({"ok": True, "note": note})

    def _post_firmware_install(self):
        # The control panel's "Install on board", proxied by control_server.
        if not (self._local_only() and self._no_origin()):
            return
        ok, detail = request_firmware_install()
        self._send_json({"ok": ok, "detail": detail}, status=200 if ok else 409)

    def _post_battery_save(self):
        # Manual battery-save override from the localhost control panel.
        # LAN clients (the board) get 403 — power policy stays Mac-local.
        if not (self._local_only() and self._no_origin()):
            return
        doc, err = self._read_json_body()
        if err:
            return
        if not isinstance(doc, dict) or "enabled" not in doc or (
            doc.get("enabled") is not None and not isinstance(doc.get("enabled"), bool)
        ):
            self._send_json(
                {"ok": False, "detail": "body must be {\"enabled\": true|false|null}"},
                status=400,
            )
            return
        set_battery_save_manual(doc["enabled"])
        self._send_json({"ok": True, "power": power_snapshot()})


def start_http_server(report_fn, port=USAGE_PORT, host="0.0.0.0"):
    """Bind, serve on a daemon thread, return the server (battery_guard_loop
    shuts it down and calls this again). report_fn() builds GET /api/usage."""
    Handler.report_fn = staticmethod(report_fn)
    # A bare bind failure here (e.g. EADDRINUSE from the previous instance's
    # socket still tearing down across a fast launchd restart) used to raise
    # straight out of main(), crash-looping launchd every ~10s. Retry with
    # backoff instead so a transient bind failure self-heals.
    delay = 1
    while True:
        try:
            server = Server((host, port), Handler)
            break
        except OSError as e:
            log_err(f"start_http_server: bind failed ({e}), retrying in {delay}s")
            time.sleep(delay)
            delay = min(delay * 2, 30)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"Serving usage stats on http://{host}:{port}/api/usage")
    print(f"Note editor (localhost only) on http://127.0.0.1:{port}/")
    return server
