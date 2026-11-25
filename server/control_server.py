#!/usr/bin/env python3
"""Control panel for the S3 usage server: status, logs, enable/disable, and
the S3 board's firmware (OTA): status, build & publish, install on board.

Serves server.html plus a small JSON API on 127.0.0.1:8788. Bound to
localhost ONLY because /api/enable and /api/disable shell out to launchctl —
never expose this port to the LAN.
"""

import collections
import json
import os
import re
import shutil
import subprocess
import threading
import time
import urllib.error

from s3srv.common import (CONTROL_PORT as PORT, REPO_DIR as S3_REPO, USAGE_LOCAL_URL,
                          JsonHandler, Server, fetch_json)

# The only two contexts server.html legitimately runs in: served by this
# process itself (same-origin, no CORS needed at all), or opened directly via
# file:// for local dev (Origin: null). Anything else hitting the destructive
# POST endpoints below is cross-site and must be rejected outright.
ALLOWED_ORIGIN = "http://127.0.0.1:%d" % PORT

JOB_LABEL = "com.corner.s3usage"
PLIST_PATH = os.path.expanduser("~/Library/LaunchAgents/%s.plist" % JOB_LABEL)
# The panel page is server.html at the repo root, and "Build & publish" runs
# pio there.
HTML_PATH = os.path.join(S3_REPO, "server.html")
USAGE_URL = USAGE_LOCAL_URL + "/api/usage"
BATTERY_SAVE_URL = USAGE_LOCAL_URL + "/api/battery-save"
# Firmware (OTA) for the S3 board: status and "Install on board" are the usage
# server's (localhost-only there); "Build & publish" runs here.
FIRMWARE_URL = USAGE_LOCAL_URL + "/api/firmware"
FIRMWARE_INSTALL_URL = USAGE_LOCAL_URL + "/api/firmware/install"
# launchd's PATH has no PlatformIO; these are where it lives on this Mac.
PIO_CANDIDATES = [
    os.path.expanduser("~/.pio-venv/bin/pio"),
    "/opt/homebrew/bin/pio",
    "/usr/local/bin/pio",
    os.path.expanduser("~/.platformio/penv/bin/pio"),
]
BUILD_TIMEOUT_SEC = 15 * 60
BUILD_LOG_LINES = 200
LOG_OUT = "/tmp/s3usage.log"
LOG_ERR = "/tmp/s3usage.err"
LOG_TAIL_LINES = 40
JSON_HEADERS = {"Content-Type": "application/json"}


def run(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        return out.returncode, out.stdout, out.stderr
    except Exception as exc:
        return -1, "", str(exc)


def launchd_status():
    code, out, _ = run(["/bin/launchctl", "print", "gui/%d/%s" % (os.getuid(), JOB_LABEL)])
    if code != 0:
        return {"loaded": False, "pid": None, "state": "not loaded", "uptime": None}
    pid = None
    state = None
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("pid = "):
            try:
                pid = int(line.split("=", 1)[1])
            except ValueError:
                pass
        elif line.startswith("state = "):
            state = line.split("=", 1)[1].strip()
    uptime = None
    if pid:
        code, out, _ = run(["/bin/ps", "-o", "etime=", "-p", str(pid)])
        if code == 0:
            uptime = out.strip() or None
    return {"loaded": True, "pid": pid, "state": state or "unknown", "uptime": uptime}


def probe_endpoint():
    t0 = time.time()
    try:
        doc = fetch_json(USAGE_URL, 3)
        return {
            "up": True,
            "latency_ms": int((time.time() - t0) * 1000),
            "generated_at": doc.get("generated_at"),
            "limits_ok": doc.get("limits") is not None,
            "clients": doc.get("clients") or [],
            # Mac power / battery-save (from usage_server STATE["power"]).
            "power": doc.get("power"),
        }
    except Exception as exc:
        return {"up": False, "error": str(exc)}


def set_battery_save(enabled):
    # Proxy to usage_server POST /api/battery-save (localhost-only there
    # too). enabled: True/False forces the mode, None clears the override
    # back to auto — pass through as-is so None serializes to JSON null.
    body = json.dumps({"enabled": enabled}).encode("utf-8")
    try:
        doc = fetch_json(BATTERY_SAVE_URL, 3, headers=JSON_HEADERS, data=body)
        if doc.get("ok"):
            if enabled is True:
                state = "on"
            elif enabled is False:
                state = "off"
            else:
                state = "cleared (auto)"
            return True, "battery saver %s" % state, doc.get("power")
        return False, doc.get("detail") or "usage server rejected request", None
    except Exception as exc:
        return False, "usage server unreachable: %s" % exc, None


def probe_firmware():
    try:
        return fetch_json(FIRMWARE_URL, 3)
    except urllib.error.HTTPError as exc:
        return {"error": "usage server answered %d (restart it for /api/firmware)" % exc.code}
    except Exception as exc:
        return {"error": str(exc)}


def request_install():
    try:
        doc = fetch_json(FIRMWARE_INSTALL_URL, 3, headers=JSON_HEADERS, data=b"{}")
    except urllib.error.HTTPError as exc:
        try:
            doc = json.loads(exc.read())
        except ValueError:
            doc = {"ok": False, "detail": "usage server answered %d" % exc.code}
    except Exception as exc:
        return False, "usage server unreachable: %s" % exc
    return bool(doc.get("ok")), doc.get("detail") or ("requested" if doc.get("ok") else "failed")


# ── Build & publish (`pio run -t publish` in ~/s3), one at a time ──
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
BUILD = {
    "lock": threading.Lock(),
    "running": False,
    "started": None,
    "finished": None,
    "code": None,
    "error": None,
    "lines": collections.deque(maxlen=BUILD_LOG_LINES),
}


def find_pio():
    found = shutil.which("pio")
    if found:
        return found
    for path in PIO_CANDIDATES:
        if os.access(path, os.X_OK):
            return path
    return None


def run_publish(pio):
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([os.path.dirname(pio), "/opt/homebrew/bin", "/usr/local/bin", env.get("PATH", "")])
    env["PLATFORMIO_NO_ANSI"] = "true"
    code, error = None, None
    try:
        proc = subprocess.Popen([pio, "run", "-t", "publish", "-d", S3_REPO], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, env=env, cwd=S3_REPO)
        killer = threading.Timer(BUILD_TIMEOUT_SEC, proc.kill)
        killer.start()
        for line in proc.stdout:
            BUILD["lines"].append(ANSI_RE.sub("", line.rstrip()))
        code = proc.wait()
        killer.cancel()
    except Exception as exc:
        error = str(exc)
    with BUILD["lock"]:
        BUILD.update(running=False, finished=time.time(), code=code, error=error)


def start_publish():
    pio = find_pio()
    if not pio:
        return False, "PlatformIO not found (looked on PATH and in %s)" % ", ".join(PIO_CANDIDATES)
    if not os.path.isdir(S3_REPO):
        return False, "%s not found" % S3_REPO
    with BUILD["lock"]:
        if BUILD["running"]:
            return False, "a build is already running"
        BUILD["lines"].clear()
        BUILD.update(running=True, started=time.time(), finished=None, code=None, error=None)
    threading.Thread(target=run_publish, args=(pio,), daemon=True).start()
    return True, "building and publishing ~/s3"


def build_status():
    now = time.time()
    with BUILD["lock"]:
        lines = list(BUILD["lines"])
        doc = {
            "running": BUILD["running"],
            "elapsed_sec": int((BUILD["finished"] or now) - BUILD["started"]) if BUILD["started"] else None,
            "finished_ago_sec": int(now - BUILD["finished"]) if BUILD["finished"] else None,
            "code": BUILD["code"],
            "error": BUILD["error"],
        }
    doc["lines"] = lines[-80:]
    # ota_publish.py's two result lines, for the panel's summary.
    doc["published"] = next((l for l in reversed(lines) if l.startswith("Published ")), None)
    doc["digest"] = next((l for l in reversed(lines) if l.startswith("Image digest ")), None)
    return doc


def tail_file(path, n=LOG_TAIL_LINES):
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            if size > 8192:
                f.seek(size - 8192)
            data = f.read()
        lines = data.decode("utf-8", errors="replace").splitlines()
        return lines[-n:]
    except OSError:
        return []


def do_enable():
    if not os.path.exists(PLIST_PATH):
        return False, "plist not found at %s -- copy server/com.corner.s3*.plist there (README, step 1)" % PLIST_PATH
    code, out, err = run(["/bin/launchctl", "bootstrap", "gui/%d" % os.getuid(), PLIST_PATH])
    if code == 0:
        return True, "server enabled (launchd job loaded)"
    # Already loaded is not a failure worth alarming over.
    if "already bootstrapped" in err.lower() or "service already loaded" in err.lower():
        return True, "server was already enabled"
    return False, (err or out or "launchctl bootstrap failed").strip()


def do_disable():
    code, out, err = run(["/bin/launchctl", "bootout", "gui/%d/%s" % (os.getuid(), JOB_LABEL)])
    if code == 0:
        return True, "server disabled (launchd job unloaded)"
    if "no such process" in err.lower() or code == 3:
        return True, "server was already disabled"
    return False, (err or out or "launchctl bootout failed").strip()


class Handler(JsonHandler):
    def _origin_allowed(self):
        # No Origin header at all = not a browser cross-origin request (curl,
        # the board, etc.) — allow. Otherwise it must be this server's own
        # origin, or "null" (server.html opened directly via file:// for
        # local dev) — anything else is a cross-site page trying to drive
        # /api/enable /api/disable /api/battery-save.
        origin = self.headers.get("Origin")
        return origin is None or origin == ALLOWED_ORIGIN or origin == "null"

    def _cors_origin(self):
        # ACAO only for the file:// dev case (Origin: null) — same-origin
        # fetches from server.html-as-served-here need no CORS header at all,
        # and a blanket "*" would let any web page read this API cross-origin.
        return "null" if self.headers.get("Origin") == "null" else None

    def do_OPTIONS(self):
        # CORS preflight — the battery-save POST sends a JSON Content-Type
        # header, which makes it a non-simple request browsers preflight.
        self.send_response(204)
        if self._origin_allowed() and self.headers.get("Origin") == "null":
            self.send_header("Access-Control-Allow-Origin", "null")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html", "/server.html"):
            try:
                with open(HTML_PATH, "rb") as f:
                    self._send(f.read(), "text/html; charset=utf-8")
            except OSError:
                self._send(b"server.html not found", "text/plain", status=404)
            return
        if self.path == "/api/status":
            self._send_json({
                "now": time.time(),
                "launchd": launchd_status(),
                "endpoint": probe_endpoint(),
                "logs": {"out": tail_file(LOG_OUT), "err": tail_file(LOG_ERR)},
                "firmware": probe_firmware(),
                "build": build_status(),
            })
            return
        self._send(b"not found", "text/plain", status=404)

    def do_POST(self):
        if not self._origin_allowed():
            self._send(b"forbidden origin", "text/plain", status=403)
            return
        if self.path == "/api/enable":
            ok, detail = do_enable()
            self._send_json({"ok": ok, "detail": detail})
            return
        if self.path == "/api/disable":
            ok, detail = do_disable()
            self._send_json({"ok": ok, "detail": detail})
            return
        if self.path == "/api/firmware/publish":
            ok, detail = start_publish()
            self._send_json({"ok": ok, "detail": detail})
            return
        if self.path == "/api/firmware/install":
            ok, detail = request_install()
            self._send_json({"ok": ok, "detail": detail})
            return
        if self.path == "/api/battery-save":
            # Body: {"enabled": true|false}. Proxied to the usage server.
            doc, err = self._read_json_body()
            if err:
                return
            # json.loads also accepts 5, [], "x", null: only an object has "enabled".
            if not isinstance(doc, dict) or "enabled" not in doc or (
                doc.get("enabled") is not None and not isinstance(doc.get("enabled"), bool)
            ):
                self._send_json(
                    {"ok": False, "detail": "body must be {\"enabled\": true|false|null}"},
                    status=400,
                )
                return
            ok, detail, power = set_battery_save(doc["enabled"])
            self._send_json({"ok": ok, "detail": detail, "power": power})
            return
        self._send(b"not found", "text/plain", status=404)


def main():
    server = Server(("127.0.0.1", PORT), Handler)
    print("Control panel on http://127.0.0.1:%d/ (localhost only)" % PORT)
    server.serve_forever()


if __name__ == "__main__":
    main()
