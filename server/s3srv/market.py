"""Market and environment data the board shows but can't fetch itself (no
TLS on the board): BTC (Binance), Bangkok weather (Open-Meteo) and Bangkok
AQI (aqicn, else Air4Thai stations, else Open-Meteo CAMS), and market_loop."""

import math
import statistics
import time
from datetime import datetime, timedelta, timezone

from s3srv.common import USER_AGENT, fetch_json, load_secret, log_err
from s3srv.power import battery_save_active
from s3srv.state import activity_gated_loop, state_set

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
    # load_secret(): AQICN_TOKEN env var, then secrets.local.json (repo root,
    # gitignored) -- same lookup order pull_giphy_cats.py uses for its
    # GIPHY_API_KEY. Warns once when there is none.
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
    return parse_air4thai(fetch_json(AIR4THAI_URL, 8, headers=USER_AGENT))


def parse_air4thai(doc, now=None):
    """Median US AQI of the fresh stations within AIR4THAI_RADIUS_KM, or None
    with fewer than AIR4THAI_MIN_STATIONS. `now` (aware) is for tests."""
    now = now or datetime.now(BANGKOK_TZ)
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
    return parse_openmeteo_weather(fetch_json(WEATHER_URL, 10))


def parse_openmeteo_weather(doc):
    """Open-Meteo forecast JSON -> weather_payload(), or None without a
    current temperature."""
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
