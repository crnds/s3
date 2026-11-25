#!/usr/bin/env python3
"""Serves Claude Code token usage stats parsed from ~/.claude/projects/*/*.jsonl."""

import json
import math
import os
import re
import statistics
import threading
import time
from datetime import datetime, timedelta, timezone

from s3srv.common import (USAGE_PORT as PORT, REPO_DIR, USER_AGENT, JsonHandler, Server,
                          fetch_json, load_secret, log_err)
from s3srv.state import (STATE, CLIENT_RETENTION, activity_gated_loop, is_known_client,
                         note_client, state_set)
from s3srv.power import (battery_guard_loop, battery_save_active, power_snapshot,
                         set_battery_save_manual)
from s3srv.usage_scan import compute_aggregates, scan_loop, scan_once
from s3srv.limits import limits_loop

# ── Note page (board page 6) ──────────────────────────────────────────────
# Free text authored in note.html and rendered in the right-hand pane of the
# board's Note page. It rides along inside /api/usage rather than getting its
# own endpoint, so the board needs no second HTTP call and gets SD caching for
# free via the existing /last_usage.json replay.
NOTE_PATH = os.path.expanduser("~/.s3_note.json")
# The board's pane draws at most 24 cols x 19 rows = 456 glyphs at text size 1.
# 480 sits just above that ceiling (headroom for newlines) and comfortably
# under the firmware's 512-byte buffer. Because sanitize_note() forces ASCII,
# characters == bytes and the two caps are directly comparable.
NOTE_MAX_CHARS = 480
# The editor page sits at the repo root (note.html), next to the control
# panel's server.html.
NOTE_HTML_PATH = os.path.join(REPO_DIR, "note.html")
# note.html is served from this same origin, so its POST carries an Origin
# header — the blanket "reject any Origin" rule /api/battery-save uses (see
# do_POST) can't be reused here. Allowlist the origins this server can
# legitimately be reached on instead, plus "null" for the file:// dev case.
NOTE_ALLOWED_ORIGINS = {
    f"http://127.0.0.1:{PORT}",
    f"http://localhost:{PORT}",
    "null",
}
# ── OTA firmware (S3 board, roadmap O9) ─────────────────────────────
# `pio run -t publish` (tools/ota_publish.py) copies a build here; this server only offers
# what was published, never a half-finished `pio run`. The board compares the
# offered image digest with its own running image and pulls the file from
# /firmware/<board>.bin into its idle app slot. Outside the repo on purpose:
# a build carries the WiFi credentials compiled into it.
FIRMWARE_DIR = os.path.expanduser("~/.s3_firmware")
FIRMWARE_BOARDS = ("s3",)
FIRMWARE_MAX_BYTES = 6553600  # the S3's app slot (default_16MB.csv)
_firmware_cache = {}  # board -> ((mtime_ns, size), info)
# The control panel's "Install on board": board -> {"id", "sha256", "ts"}. The
# id rides in the offer (firmware.<board>.install) until a board reports
# running that build, a newer one is published, or FIRMWARE_REQUEST_TTL_SEC.
FIRMWARE_REQUEST_TTL_SEC = 15 * 60
FIRMWARE_DOWNLOAD_RECENT_SEC = 180  # "installing" in the panel this long after a download
_firmware_requests = {}
# What each board reports on every poll (X-S3-Firmware / X-S3-OTA headers):
# ip -> {"sha256", "ota", "auto", "ts"}; and when each ip last downloaded.
_board_firmware = {}
_firmware_downloads = {}  # ip -> time.time() of the last /firmware/<board>.bin

# Browser pages that may read GET /api/usage cross-origin, besides the file://
# case (Origin: null): pages served from this Mac's own loopback, any port --
# e.g. ~/s3's simulator-s3.html under `python3 -m http.server 8765`. Matched
# exactly, never "*": a website's JS can't send a loopback Origin, so no other
# site can read the usage data.
USAGE_CORS_ORIGIN_RE = re.compile(r"http://(localhost|127\.0\.0\.1|\[::1\])(:[0-9]{1,5})?")
ACTIVE_WINDOW_SEC = 180
# Assumed model context size for the "context window" percent — the jsonl
# events don't carry the model's actual limit, so 200K is hardcoded.
CONTEXT_WINDOW_TOKENS = 200000


# BTC + Bangkok weather are fetched by the Mac (which has ample RAM + a TLS
# stack) and handed to the board inside /api/usage. The CYD has no PSRAM, so its
# 154KB framebuffer leaves too little contiguous heap for an on-device mbedTLS
# handshake — proxying these through the plain-HTTP endpoint it already polls
# avoids TLS on the board entirely.
BTC_URL = "https://api.binance.com/api/v3/ticker/24hr?symbol=BTCUSDT"
# Open-Meteo: current + hourly + daily so the board can render the Weather
# page (next 6h + next 5 days) without a second HTTPS call.
WEATHER_URL = (
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=13.7563&longitude=100.5018"
    "&current=temperature_2m,weather_code"
    "&hourly=temperature_2m,weather_code"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min"
    "&timezone=Asia%2FBangkok&forecast_days=6"
)
# aqicn.org (World Air Quality Index project) — same Bangkok coords as the
# weather fetch above, geo-based so it's not tied to one named station.
AQICN_URL = "https://api.waqi.info/feed/geo:13.7563;100.5018/?token={token}"
# Keyless source when no AQICN_TOKEN is configured: Air4Thai, the Thai Pollution
# Control Department's station network (~80 stations in Bangkok, hourly).
# fetch_aqi_air4thai() converts each station's readings to US AQI and takes the
# median of the stations within AIR4THAI_RADIUS_KM of the weather coords.
AIR4THAI_URL = "https://air4thai.pcd.go.th/services/getNewAQI_JSON.php"
AQI_LAT, AQI_LON = 13.7563, 100.5018
AIR4THAI_RADIUS_KM = 15
AIR4THAI_MIN_STATIONS = 3
AIR4THAI_MAX_AGE_SEC = 3 * 3600
BANGKOK_TZ = timezone(timedelta(hours=7))
# US EPA AQI breakpoints (C_lo, C_hi, I_lo, I_hi), PM2.5 per the 2024 revision.
# Air4Thai units: PM2.5/PM10 ug/m3, O3 ppb. Concentrations are truncated to the
# table's precision first (EPA rule), which closes the gaps between rows.
US_AQI_BREAKPOINTS = {
    "PM25": ((0.0, 9.0, 0, 50), (9.1, 35.4, 51, 100), (35.5, 55.4, 101, 150),
             (55.5, 125.4, 151, 200), (125.5, 225.4, 201, 300), (225.5, 325.4, 301, 500)),
    "PM10": ((0, 54, 0, 50), (55, 154, 51, 100), (155, 254, 101, 150),
             (255, 354, 151, 200), (355, 424, 201, 300), (425, 604, 301, 500)),
    "O3": ((0, 54, 0, 50), (55, 70, 51, 100), (71, 85, 101, 150),
           (86, 105, 151, 200), (106, 200, 201, 300)),
}
# Last resort if Air4Thai is down: Open-Meteo's CAMS model, PM2.5 sub-index
# only. Its combined us_aqi is ruled by modelled afternoon ozone that runs far
# high over Bangkok (165 at 17:00 on 2026-11-24, when the stations' readings
# gave ~86), and that was the number the board showed.
OPENMETEO_AQI_URL = (
    "https://air-quality-api.open-meteo.com/v1/air-quality"
    "?latitude=13.7563&longitude=100.5018&current=us_aqi_pm2_5&timezone=Asia%2FBangkok"
)
AQI_INTERVAL_SEC = 900
AQI_INTERVAL_BATTERY_SEC = 1800
# 120s is plenty for a 320px ticker tile — this loop's TLS handshakes are the
# main new radio load since BTC/weather moved off the board, and 2-min freshness
# reads identically on screen. (The old 10s cadence was ~8640 HTTPS/day of
# radio/TLS work that never changed what the board could usefully show.)
# On battery: stretch further (3 min BTC / 30 min weather) so the Mac isn't
# doing continuous HTTPS while trying to idle; pairs with the board's
# Battery Save 2 min usage-poll floor.
BTC_INTERVAL_SEC = 120
BTC_INTERVAL_BATTERY_SEC = 180
WEATHER_INTERVAL_SEC = 600
WEATHER_INTERVAL_BATTERY_SEC = 1800

_aqicn_token_warned = False


def aqicn_token():
    # AQICN_TOKEN env var, then secrets.local.json (repo root, gitignored) --
    # same lookup order pull_giphy_cats.py uses for its GIPHY_API_KEY, so a
    # missing token here is either unset (never fetched) or added there.
    global _aqicn_token_warned
    token = load_secret("AQICN_TOKEN")
    if not token and not _aqicn_token_warned:
        _aqicn_token_warned = True
        log_err("aqi: no AQICN_TOKEN in env or secrets.local.json -- "
                "using Air4Thai stations instead (free aqicn token: https://aqicn.org/data-platform/token/)")
    return token


def us_aqi(pollutant, conc):
    # One pollutant's US AQI sub-index, or None for a missing (<0) reading.
    if conc is None or conc < 0:
        return None
    table = US_AQI_BREAKPOINTS[pollutant]
    conc = math.floor(conc * 10) / 10 if pollutant == "PM25" else math.floor(conc)
    conc = min(conc, table[-1][1])
    for c_lo, c_hi, i_lo, i_hi in table:
        if conc <= c_hi:
            return round((i_hi - i_lo) / (c_hi - c_lo) * (conc - c_lo) + i_lo)
    return None


def _km(lat1, lon1, lat2, lon2):
    p = math.pi / 180
    a = (math.sin((lat2 - lat1) * p / 2) ** 2
         + math.cos(lat1 * p) * math.cos(lat2 * p) * math.sin((lon2 - lon1) * p / 2) ** 2)
    return 12742 * math.asin(math.sqrt(a))


def _float_or_none(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def fetch_aqi_air4thai():
    doc = fetch_json(AIR4THAI_URL, 8, headers=USER_AGENT)
    now = datetime.now(BANGKOK_TZ)
    station_aqis = []
    for st in doc.get("stations") or []:
        lat, lon = _float_or_none(st.get("lat")), _float_or_none(st.get("long"))
        if lat is None or lon is None or _km(AQI_LAT, AQI_LON, lat, lon) > AIR4THAI_RADIUS_KM:
            continue
        last = st.get("AQILast") or {}
        try:
            stamp = datetime.strptime(f"{last.get('date')} {last.get('time')}", "%Y-%m-%d %H:%M")
        except ValueError:
            continue
        if (now - stamp.replace(tzinfo=BANGKOK_TZ)).total_seconds() > AIR4THAI_MAX_AGE_SEC:
            continue
        subs = [us_aqi(p, _float_or_none((last.get(p) or {}).get("value"))) for p in US_AQI_BREAKPOINTS]
        subs = [s for s in subs if s is not None]
        if subs:
            station_aqis.append(max(subs))
    if len(station_aqis) < AIR4THAI_MIN_STATIONS:
        log_err(f"aqi: Air4Thai has {len(station_aqis)} fresh stations within {AIR4THAI_RADIUS_KM} km")
        return None
    return {"aqi": round(statistics.median(station_aqis)), "source": "air4thai"}


def fetch_aqi_openmeteo():
    doc = fetch_json(OPENMETEO_AQI_URL, 8, headers=USER_AGENT)
    aqi = (doc.get("current") or {}).get("us_aqi_pm2_5")
    if not isinstance(aqi, (int, float)) or aqi < 0:
        return None
    return {"aqi": round(aqi), "source": "open-meteo"}


def fetch_aqi():
    # aqicn with a token; else Air4Thai stations; else Open-Meteo's model.
    token = aqicn_token()
    if not token:
        try:
            aqi = fetch_aqi_air4thai()
            if aqi:
                return aqi
        except Exception as e:
            log_err(f"aqi: Air4Thai failed: {type(e).__name__}: {e}")
        return fetch_aqi_openmeteo()
    doc = fetch_json(AQICN_URL.format(token=token), 8, headers=USER_AGENT)
    if doc.get("status") != "ok":
        return None
    aqi = (doc.get("data") or {}).get("aqi")
    if not isinstance(aqi, (int, float)) or aqi < 0:
        return None
    return {"aqi": round(aqi), "source": "aqicn"}


def fetch_btc():
    doc = fetch_json(BTC_URL, 8, headers=USER_AGENT)
    price = float(doc.get("lastPrice") or 0)
    if price <= 0:
        return None
    # changePct = Binance's rolling 24h change, drawn beside the price on the
    # S3's BTC card (the CYD ignores it).
    out = {"price": price}
    try:
        out["changePct"] = round(float(doc["priceChangePercent"]), 2)
    except (KeyError, TypeError, ValueError):
        pass
    return out


def wmo_condition(code):
    # Short condition labels for the Weather page header. Buckets match
    # drawWeatherIcon() so icon + text always describe the same condition.
    if code is None or code < 0:
        return ""
    if code == 0:
        return "Clear"
    if code == 1:
        return "Mainly Clear"
    if code == 2:
        return "Partly Cloudy"
    if code == 3:
        return "Overcast"
    if code in (45, 48):
        return "Fog"
    if 51 <= code <= 57:
        return "Drizzle"
    if 61 <= code <= 67:
        return "Rain"
    if 71 <= code <= 77:
        return "Snow"
    if 80 <= code <= 82:
        return "Showers"
    if 85 <= code <= 86:
        return "Snow Showers"
    if code >= 95:
        return "Thunderstorm"
    return "Cloudy"


def _iso_hour(ts):
    # "2026-07-20T17:00" or "2026-07-20 17:00" -> hour int, or None.
    if not ts or len(ts) < 13:
        return None
    try:
        return int(ts[11:13])
    except (TypeError, ValueError):
        return None


def _iso_wday(date_str):
    # YYYY-MM-DD -> tm_wday style (0=Sun .. 6=Sat).
    try:
        dt = datetime.strptime(date_str[:10], "%Y-%m-%d")
        return (dt.weekday() + 1) % 7
    except (TypeError, ValueError):
        return 0


def weather_payload(temp_c, code, high=None, low=None, hourly=None, daily=None):
    # Shared shape for both providers — the board/simulator contract.
    # place is fixed Bangkok (hardcoded locale); not drawn on device.
    out = {
        "tempC": temp_c,
        "code": code if code is not None else -1,
        "condition": wmo_condition(code if code is not None else -1),
        "high": high,
        "low": low,
        "hourly": hourly or [],
        "daily": daily or [],
    }
    return out


def fetch_weather_openmeteo():
    doc = fetch_json(WEATHER_URL, 10)
    cur = doc.get("current") or {}
    if cur.get("temperature_2m") is None:
        return None
    code = cur.get("weather_code", -1)
    temp_c = cur["temperature_2m"]

    # Now + next 10 hours starting at the current hour (or the next future slot if
    # the exact current hour is missing from the series).
    hourly_out = []
    hdoc = doc.get("hourly") or {}
    h_times = hdoc.get("time") or []
    h_temps = hdoc.get("temperature_2m") or []
    h_codes = hdoc.get("weather_code") or []
    cur_time = cur.get("time") or ""
    # If no slot >= cur_time is found (the hourly series doesn't reach "now"),
    # falling back to 0 would silently show the oldest/stale hours at the
    # start of the window instead. Default past the end so the range below
    # comes up empty -- no hourly row beats a wrong one.
    start_i = len(h_times)
    for i, t in enumerate(h_times):
        if t[:13] >= cur_time[:13]:
            start_i = i
            break
    for i in range(start_i, min(start_i + 11, len(h_times))):
        h = _iso_hour(h_times[i])
        if h is None or i >= len(h_temps) or h_temps[i] is None:
            continue
        hc = h_codes[i] if i < len(h_codes) else -1
        hourly_out.append({
            "h": h,
            "tempC": round(h_temps[i]),
            "code": hc if hc is not None else -1,
        })

    # Today + next 4 days (5 rows on the Weather page).
    daily_out = []
    ddoc = doc.get("daily") or {}
    d_times = ddoc.get("time") or []
    d_max = ddoc.get("temperature_2m_max") or []
    d_min = ddoc.get("temperature_2m_min") or []
    d_codes = ddoc.get("weather_code") or []
    high = low = None
    for i in range(min(5, len(d_times))):
        hi = d_max[i] if i < len(d_max) else None
        lo = d_min[i] if i < len(d_min) else None
        dc = d_codes[i] if i < len(d_codes) else -1
        if i == 0:
            high = None if hi is None else round(hi)
            low = None if lo is None else round(lo)
        daily_out.append({
            "wd": _iso_wday(d_times[i]),
            "high": None if hi is None else round(hi),
            "low": None if lo is None else round(lo),
            "code": dc if dc is not None else -1,
        })

    return weather_payload(temp_c, code, high=high, low=low,
                           hourly=hourly_out, daily=daily_out)


def fetch_weather():
    try:
        return fetch_weather_openmeteo()
    except Exception as e:
        log_err(f"weather: Open-Meteo failed: {type(e).__name__}: {e}")
        return None


def market_intervals():
    if battery_save_active():
        return BTC_INTERVAL_BATTERY_SEC, WEATHER_INTERVAL_BATTERY_SEC, AQI_INTERVAL_BATTERY_SEC
    return BTC_INTERVAL_SEC, WEATHER_INTERVAL_SEC, AQI_INTERVAL_SEC


def market_loop():
    # BTC ~2 min on AC / ~3 min on battery; weather 10 min / 30 min; AQI 15 min /
    # 30 min (an hourly-updating station gains nothing from polling faster).
    # Each keeps its last good value on failure so a transient outage doesn't
    # blank tiles.
    last_weather = 0.0
    last_aqi = 0.0

    def body():
        nonlocal last_weather, last_aqi
        btc_iv, weather_iv, aqi_iv = market_intervals()
        try:
            btc = fetch_btc()
            if btc:
                state_set("btc", btc)
        except Exception as e:
            log_err(f"market_loop btc: {type(e).__name__}: {e}")
        now = time.time()
        if now - last_weather >= weather_iv:
            last_weather = now
            try:
                weather = fetch_weather()
                if weather:
                    state_set("weather", weather)
            except Exception as e:
                log_err(f"market_loop weather: {type(e).__name__}: {e}")
        if now - last_aqi >= aqi_iv:
            last_aqi = now
            try:
                aqi = fetch_aqi()
                if aqi:
                    state_set("aqi", aqi)
            except Exception as e:
                log_err(f"market_loop aqi: {type(e).__name__}: {e}")
        time.sleep(btc_iv)

    activity_gated_loop(body)


def sanitize_note(text):
    """Fold arbitrary editor text down to what the board can actually draw.

    The CYD renders with the built-in Adafruit GFX font, which only has glyphs
    for 7-bit printable ASCII. Thai, emoji and box-drawing characters have no
    glyph at all, so passing them through would paint garbage. They become "?"
    rather than being dropped, so a note never silently loses content — and
    note.html warns about the count before you ever get here.
    """
    if not isinstance(text, str):
        return ""
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\t", " ")
    out = []
    for ch in text:
        if ch == "\n" or " " <= ch <= "~":
            out.append(ch)
        else:
            out.append("?")
    return "".join(out)[:NOTE_MAX_CHARS]


def note_snapshot():
    with STATE["lock"]:
        return dict(STATE["note"])


def load_note():
    """Seed STATE["note"] from disk at startup. A missing/corrupt file is not
    an error — it just means "no note yet"."""
    try:
        with open(NOTE_PATH, "r", encoding="utf-8") as f:
            doc = json.load(f)
        note = {
            "text": sanitize_note(doc.get("text", "")),
            "size": min(3, max(1, int(doc.get("size", 1)))),
        }
    except (OSError, ValueError, TypeError):
        return
    with STATE["lock"]:
        STATE["note"] = note


def save_note(note):
    """Persist atomically so a crash mid-write can't leave a truncated file
    that load_note() would then discard on the next boot."""
    tmp = NOTE_PATH + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(note, f)
        os.replace(tmp, NOTE_PATH)
    except OSError as e:
        log_err(f"save_note: {type(e).__name__}: {e}")


def build_report():
    now = datetime.now(timezone.utc)

    with STATE["lock"]:
        agg = STATE["aggregates"] or {
            "today": {"tokens": 0, "cost": 0.0},
            "week": {"tokens": 0, "cost": 0.0},
            "projects": [], "trend": [0] * 7,
        }
        ctx = STATE["context"]
        last_ts = STATE["last_ts"]
        limits = STATE["limits"]
        # Prune clients that haven't polled in 24h so this dict (and the
        # `clients` array in every response) doesn't grow forever.
        cutoff_client = now - CLIENT_RETENTION
        STATE["clients"] = {ip: ts for ip, ts in STATE["clients"].items() if ts >= cutoff_client}
        client_snapshot = list(STATE["clients"].items())

    # active_now is archive-only on the board (not drawn); kept for SD CSV.
    active_now = last_ts is not None and (now - last_ts).total_seconds() <= ACTIVE_WINDOW_SEC

    clients = [
        {"ip": ip, "last_seen_sec": int((now - ts).total_seconds())}
        for ip, ts in sorted(client_snapshot, key=lambda kv: kv[1], reverse=True)
    ]

    # Countdown to the session reset, computed at request time so it stays
    # accurate between OAuth refreshes. Mutate per-request copies only —
    # STATE["limits"] stays pristine with internal epochs.
    if limits:
        limits = dict(limits)
        limits.pop("fetched_at_epoch", None)
        for key in ("session", "week", "week_model"):
            win = limits.get(key) or {}
            if not isinstance(win, dict):
                continue
            win = dict(win)
            epoch = win.pop("resets_at_epoch", None)
            if epoch:
                remaining = int(epoch - time.time())
                if remaining <= 0:
                    # Window ended; OAuth loop may not have a fresh snapshot
                    # yet. Serve reset rather than a stale percent.
                    win["percent"] = 0
                    win["resets"] = ""
                    remaining = 0
                if key in ("session", "week"):
                    win["resets_in_sec"] = remaining
            limits[key] = win

    return {
        "generated_at": now.isoformat(),
        "epoch": int(now.timestamp()),
        "today": agg["today"],
        "week": agg["week"],
        "active_now": active_now,
        "projects": agg["projects"],
        "trend": agg["trend"],
        "limits": limits,
        "context": ({"tokens": ctx["tokens"],
                     "percent": round(ctx["tokens"] / CONTEXT_WINDOW_TOKENS * 100)}
                    if ctx else None),
        "btc": STATE["btc"],
        "weather": STATE["weather"],
        "aqi": STATE["aqi"],
        # Board page 6. Always an object (never null) so an empty "text" reads
        # as "the user cleared the note" rather than "no data yet" — the
        # firmware keeps its previous value for absent keys.
        "note": STATE["note"],
        "clients": clients,
        "power": power_snapshot(),
        # Published OTA builds per board ({} when none): additive, and the CYD
        # ignores it.
        "firmware": firmware_offer(),
    }


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
    _firmware_cache[board] = (key, info)
    return info


def firmware_request(board, info):
    """The pending install request for this published build, or None."""
    req = _firmware_requests.get(board)
    if not req or not info:
        return None
    if req["sha256"] != info["sha256"] or time.time() - req["ts"] > FIRMWARE_REQUEST_TTL_SEC:
        _firmware_requests.pop(board, None)
        return None
    return req


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
    _board_firmware[ip] = {"sha256": sha256, "ota": state[:16], "auto": auto, "ts": time.time()}
    for board, req in list(_firmware_requests.items()):
        if req["sha256"] == sha256 or req["id"] == handled:
            _firmware_requests.pop(board, None)


def firmware_status():
    """GET /api/firmware (localhost): the control panel's Firmware section."""
    now = time.time()
    boards = []
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
    offer = firmware_offer().get("s3")
    req = firmware_request("s3", firmware_info("s3"))
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
    _firmware_requests[board] = {"id": int(time.time()), "sha256": info["sha256"], "ts": time.time()}
    return True, "the board installs %s on its next poll" % info["sha256"][:12]


HEARTBEAT_LOG_INTERVAL_SEC = 300
_last_heartbeat_log = {}  # ip -> monotonic time of last heartbeat line logged

# Gap reporting. The throttled heartbeat above keeps the log small, but it also
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
_last_seen_mono = {}  # ip -> monotonic time of that client's previous request
_min_gap_seen = {}    # ip -> smallest inter-request gap, i.e. inferred poll interval


class Handler(JsonHandler):
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

    def _serve_note_html(self):
        # Read fresh from disk per request (same as control_server.py) — this
        # is a single local page, not a hot path, and it means editing
        # note.html doesn't need a server restart.
        try:
            with open(NOTE_HTML_PATH, "rb") as f:
                self._send(f.read(), "text/html; charset=utf-8")
        except OSError:
            self._send(b"note.html not found", "text/plain", status=404)

    def _serve_firmware(self):
        # Only to this Mac and to boards that poll /api/usage: an image holds
        # the WiFi credentials compiled into it, so it isn't for the whole LAN.
        name = self.path[len("/firmware/"):]
        board = name[:-4] if name.endswith(".bin") else ""
        ip = self.client_address[0]
        known = is_known_client(ip)
        if board not in FIRMWARE_BOARDS:
            self._send(b"not found", "text/plain", status=404)
            return
        if not (self._is_local() or known):
            self._send(b"poll /api/usage first", "text/plain", status=403)
            return
        if not firmware_info(board):
            self._send(b"no build published", "text/plain", status=404)
            return
        try:
            with open(firmware_path(board), "rb") as f:
                body = f.read()
        except OSError:
            self._send(b"no build published", "text/plain", status=404)
            return
        _firmware_downloads[ip] = time.time()
        print(f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')} {ip} GET /firmware/{board}.bin "
              f"({len(body)} bytes)", flush=True)
        self._send(body, "application/octet-stream")

    def do_GET(self):
        # The note routes are localhost-only, matching /api/battery-save:
        # the note is writable only from this Mac, so serving the editor to
        # the LAN would just be a form that 403s on save.
        if self.path in ("/", "/note", "/note.html"):
            if not self._is_local():
                self._send(b"localhost only", "text/plain", status=403)
                return
            self._serve_note_html()
            return
        if self.path == "/api/note":
            if not self._is_local():
                self._send_json({"ok": False, "detail": "localhost only"}, status=403)
                return
            self._send_json(note_snapshot())
            return
        if self.path.startswith("/firmware/"):
            self._serve_firmware()
            return
        if self.path == "/api/firmware":
            if not self._is_local():
                self._send_json({"ok": False, "detail": "localhost only"}, status=403)
                return
            self._send_json(firmware_status())
            return
        if self.path != "/api/usage":
            self.send_response(404)
            self.end_headers()
            return
        # Only /api/usage counts as a client poll: the client registry drives
        # active_now and un-parks the activity-gated background loops, and a
        # browser loading the note editor is not a dashboard client.
        ip = self.client_address[0]
        note_client(ip)
        # The S3 reports its running build + OTA state on every poll.
        if self.headers.get("X-S3-Firmware"):
            note_board_firmware(ip, self.headers.get("X-S3-Firmware", ""), self.headers.get("X-S3-OTA", ""))
        if ip not in ("127.0.0.1", "::1"):
            # Heartbeat line, throttled to once per HEARTBEAT_LOG_INTERVAL_SEC
            # per client -- at the board's ~20s poll cadence an unthrottled
            # line here grows /tmp/s3usage.log by ~200KB/day for no benefit,
            # since `tail -f` just needs proof-of-life, not every single poll.
            now_mono = time.monotonic()
            stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

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
                    print(
                        f"{stamp} {ip} GET /api/usage  RESUMED after {gap:.0f}s gap "
                        f"(polls ~every {floor:.0f}s) -- board was likely OFFLINE",
                        flush=True,
                    )

            last = _last_heartbeat_log.get(ip, 0.0)
            if now_mono - last >= HEARTBEAT_LOG_INTERVAL_SEC:
                _last_heartbeat_log[ip] = now_mono
                print(f"{stamp} {ip} GET /api/usage", flush=True)
        self._send_json(build_report())

    def _do_post_note(self):
        # Note text from note.html. Localhost-only, same policy as
        # /api/battery-save: anyone on the LAN can *read* the note (it rides
        # in /api/usage, which is how the board gets it), but only this Mac
        # can change what's on the screen.
        if not self._is_local():
            self._send_json({"ok": False, "detail": "localhost only"}, status=403)
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
        with STATE["lock"]:
            STATE["note"] = note
        save_note(note)
        self._send_json({"ok": True, "note": note})

    def do_POST(self):
        if self.path == "/api/note":
            self._do_post_note()
            return
        if self.path == "/api/firmware/install":
            # The control panel's "Install on board", proxied by
            # control_server (no Origin, same rule as battery-save below).
            if not self._is_local():
                self._send_json({"ok": False, "detail": "localhost only"}, status=403)
                return
            if self.headers.get("Origin") is not None:
                self._send_json({"ok": False, "detail": "forbidden origin"}, status=403)
                return
            ok, detail = request_firmware_install()
            self._send_json({"ok": ok, "detail": detail}, status=200 if ok else 409)
            return
        # Manual battery-save override from the localhost control panel.
        # LAN clients (the board) get 403 — power policy stays Mac-local.
        if self.path != "/api/battery-save":
            self.send_response(404)
            self.end_headers()
            return
        if not self._is_local():
            self._send_json({"ok": False, "detail": "localhost only"}, status=403)
            return
        # The only legitimate caller is control_server.py's server-side
        # urllib proxy, which never sends an Origin header. A same-machine
        # browser tab (any website, not just server.html) CAN still reach
        # 127.0.0.1 directly regardless of its own origin -- the IP check
        # above doesn't stop that CSRF path -- so any request carrying an
        # Origin header at all is rejected here.
        if self.headers.get("Origin") is not None:
            self._send_json({"ok": False, "detail": "forbidden origin"}, status=403)
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


def start_http_server():
    # A bare bind failure here (e.g. EADDRINUSE from the previous instance's
    # socket still tearing down across a fast launchd restart) used to raise
    # straight out of main(), crash-looping launchd every ~10s. Retry with
    # backoff instead so a transient bind failure self-heals.
    delay = 1
    while True:
        try:
            server = Server(("0.0.0.0", PORT), Handler)
            break
        except OSError as e:
            log_err(f"start_http_server: bind failed ({e}), retrying in {delay}s")
            time.sleep(delay)
            delay = min(delay * 2, 30)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"Serving usage stats on http://0.0.0.0:{PORT}/api/usage")
    print(f"Note editor (localhost only) on http://127.0.0.1:{PORT}/")
    return server


def main():
    load_note()
    scan_once()
    compute_aggregates()
    threading.Thread(target=scan_loop, daemon=True).start()
    threading.Thread(target=limits_loop, daemon=True).start()
    threading.Thread(target=market_loop, daemon=True).start()
    battery_guard_loop(start_http_server)


if __name__ == "__main__":
    main()
