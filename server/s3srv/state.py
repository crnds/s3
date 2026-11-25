"""The usage server's shared state, its lock, client tracking and the
activity gate every background loop runs under."""

import threading
from datetime import datetime, timedelta, timezone

STATE = {
    "offsets": {},      # file path -> byte offset already consumed
    "buckets": {},      # bucket_epoch -> {(project, model): {tokens, cost}}
    "seen": {},         # (message_id, request_id) -> ts, dedupes streamed rewrites
    "last_ts": None,    # exact ts of the newest ingested event (active_now)
    "aggregates": None, # cached output of compute_aggregates(), refreshed once per scan
    "clients": {},      # client ip -> last request ts, shows who is polling us
    "limits": None,     # latest plan-limit snapshot from the OAuth usage endpoint
    "context": None,    # {tokens, ts} — newest assistant event's context size
    "btc": None,        # {price, changePct} from Binance — fetched here so the board needs no TLS
    # Bangkok weather for the status card + Weather page: current + next-6h
    # hourly + next-5d daily, fetched here so the board needs no TLS.
    "weather": None,
    "aqi": None,        # {aqi, source} Bangkok US AQI (fetch_aqi) — fetched here so the board needs no TLS
    # Power / battery-save. battery_guard_loop writes on_ac/percent/paused;
    # battery_save_manual is set by POST /api/battery-save (control panel):
    #   None  → auto (follow AC: unplugged = save on)
    #   True  → force low-power cadences on
    #   False → force full-speed cadences (even on battery)
    # on_ac=True until the first pmset sample so desktop Macs stay full-speed.
    "power": {
        "on_ac": True,
        "percent": None,
        "paused": False,
        "battery_save_manual": None,
    },
    # Note page text + board text size (1-3). Seeded from NOTE_PATH at startup
    # by load_note() and rewritten there on every POST /api/note, so a server
    # restart doesn't wipe what's on the board. Never None — an empty string
    # means "cleared", which the board must be able to tell from "absent".
    "note": {"text": "", "size": 1},
    # Only wakes a loop that is idle-parked in activity_gated_loop's own
    # clear-then-wait (see its comment below) -- it is set on every client
    # poll (note_client) and every power-state change, both far more often than
    # each loop's actual fetch cadence, so it must NOT also interrupt a
    # loop's in-flight interval sleep (each body() computes and sleeps its
    # own sleep_sec — e.g. limits_loop's 180s/300s — via plain time.sleep,
    # deliberately not activity_event.wait). Doing so would fire fetches on
    # every poll instead of on-interval, risking 429s on the OAuth endpoint.
    "activity_event": threading.Event(),
    "lock": threading.Lock(),
}

CLIENT_RETENTION = timedelta(hours=24)
# How long after the last board/sim poll we treat the Mac as "no clients".
CLIENT_ACTIVE_SEC = 180
# Idle park timeout while clients are absent but the HTTP server is still up.
IDLE_WAIT_SEC = 60
# Harder park while battery_guard has unbound the socket — nothing can wake us
# via HTTP, so don't spin every minute just to re-check is_client_active.
PAUSED_WAIT_SEC = 300


def state_set(key, value):
    """Every loop's STATE write goes through here, under the lock."""
    with STATE["lock"]:
        STATE[key] = value


def state_get(key):
    with STATE["lock"]:
        return STATE[key]


def note_client(ip, now=None):
    """Record a GET /api/usage poll: the client registry drives active_now
    and un-parks the activity-gated loops. Clients silent for
    CLIENT_RETENTION are pruned here, so the dict can't grow forever."""
    now = now or datetime.now(timezone.utc)
    cutoff = now - CLIENT_RETENTION
    with STATE["lock"]:
        clients = {c: ts for c, ts in STATE["clients"].items() if ts >= cutoff}
        clients[ip] = now
        STATE["clients"] = clients
        STATE["activity_event"].set()


def is_known_client(ip):
    with STATE["lock"]:
        return ip in STATE["clients"]


def is_client_active():
    now = datetime.now(timezone.utc)
    with STATE["lock"]:
        for ip, ts in STATE["clients"].items():
            if (now - ts).total_seconds() <= CLIENT_ACTIVE_SEC:
                return True
    return False


def is_http_paused():
    with STATE["lock"]:
        return bool(STATE["power"].get("paused"))


def wake_loops():
    STATE["activity_event"].set()


def activity_gated_loop(body):
    # Shared "skip work while no client has polled recently" wrapper for
    # scan_loop/limits_loop/market_loop. The first iteration always runs (so
    # state is populated even before any client connects); after that, idle
    # stretches park on activity_event instead of spinning a poll loop.
    # While battery_guard has unbound the listener, park harder — no HTTP
    # client can set the event until the socket is rebound.
    first_run = True
    while True:
        if not first_run and (is_http_paused() or not is_client_active()):
            # clear-then-recheck: a poll between is_client_active and clear
            # would otherwise lose its wake signal for a full idle timeout.
            STATE["activity_event"].clear()
            if is_http_paused():
                STATE["activity_event"].wait(timeout=PAUSED_WAIT_SEC)
                continue
            if is_client_active():
                continue
            STATE["activity_event"].wait(timeout=IDLE_WAIT_SEC)
            continue
        first_run = False
        body()
