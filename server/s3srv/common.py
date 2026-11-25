"""Shared by both servers: logging, repo paths, ports, outbound JSON fetches,
secrets, and the JSON HTTP handler base."""

import json
import os
import sys
import urllib.request
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Repo root: this file is server/s3srv/common.py.
REPO_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SECRETS_PATH = os.path.join(REPO_DIR, "secrets.local.json")

USAGE_PORT = 8787    # usage_server.py, all interfaces (the board polls it over the LAN)
CONTROL_PORT = 8788  # control_server.py, 127.0.0.1 only
# The usage server as this Mac reaches it (control_server's proxy calls).
USAGE_LOCAL_URL = "http://127.0.0.1:%d" % USAGE_PORT

MAX_BODY_BYTES = 4096  # POST bodies here are tiny JSON; anything bigger is bogus/abusive
USER_AGENT = {"User-Agent": "s3usage"}


def _stamp():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def log_err(msg):
    # stderr goes to /tmp/s3usage.err via launchd; without timestamps the
    # log can't answer "when did this loop last run", which is the first
    # question when the displayed percent goes stale.
    print(f"{_stamp()} {msg}", file=sys.stderr, flush=True)


def log_out(msg):
    # stdout (/tmp/s3usage.log): the per-client heartbeat and downloads.
    print(f"{_stamp()} {msg}", flush=True)


def fetch_json(url, timeout, headers=None, data=None):
    """GET (or POST when data is given) url and parse the JSON body.

    Errors propagate (urllib.error.HTTPError carries .code and .headers), so
    each caller keeps its own last-good / backoff policy."""
    req = urllib.request.Request(url, data=data, headers=headers or {},
                                 method="POST" if data is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read())


def load_secret(name):
    """env var `name`, then secrets.local.json (repo root, gitignored), else None."""
    value = os.environ.get(name)
    if value:
        return value
    try:
        with open(SECRETS_PATH) as f:
            return (json.load(f) or {}).get(name) or None
    except (OSError, ValueError, AttributeError):
        return None


class JsonHandler(BaseHTTPRequestHandler):
    """Base for both servers' handlers: quiet access log, one response
    writer with an exact-match CORS hook, and the shared POST body reader."""

    def log_message(self, fmt, *args):
        pass

    def _cors_origin(self):
        """The Origin to echo in Access-Control-Allow-Origin, or None."""
        return None

    def _send(self, body, content_type, status=200):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        origin = self._cors_origin()
        if origin:
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Vary", "Origin")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, doc, status=200):
        self._send(json.dumps(doc).encode("utf-8"), "application/json", status)

    def _is_local(self):
        return self.client_address[0] in ("127.0.0.1", "::1")

    def _read_json_body(self):
        """Returns (doc, None), or (None, True) after having already sent the
        error response. doc may be any JSON value; callers check its type."""
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            length = 0
        if length > MAX_BODY_BYTES:
            self._send_json({"ok": False, "detail": "body too large"}, status=413)
            return None, True
        raw = self.rfile.read(length) if length > 0 else b"{}"
        try:
            return json.loads(raw.decode("utf-8") or "{}"), None
        except (ValueError, UnicodeDecodeError):
            self._send_json({"ok": False, "detail": "invalid JSON"}, status=400)
            return None, True


class Server(ThreadingHTTPServer):
    allow_reuse_address = True  # rebind immediately if launchd restarts us
    daemon_threads = True
