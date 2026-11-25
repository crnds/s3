"""Plan limits: the Claude Code OAuth token from the Keychain, the OAuth usage
endpoint, and limits_loop."""

import json
import subprocess
import time
import urllib.error
from datetime import datetime

from s3srv.common import fetch_json, log_err
from s3srv.power import battery_save_active
from s3srv.state import activity_gated_loop, state_set

# 180s on AC: the display's countdown stays smooth regardless — build_report()
# recomputes resets_in_sec per request from the last snapshot's epoch — so only
# the percent lags slightly, and this cuts the OAuth HTTPS + `security` keychain
# subprocess forks by a third.
LIMITS_INTERVAL_SEC = 180
# On battery (still serving — above BATTERY_LOW_PCT): stretch OAuth a bit.
# Display already recomputes resets_in_sec per request from the last snapshot.
LIMITS_INTERVAL_BATTERY_SEC = 300
# The OAuth endpoint rate-limits aggressive polling; after a 429, back off
# hard — the last-good snapshot plus per-request resets_in_sec keeps the
# board's display fresh in the meantime. Retry-After is respected but clamped:
# the endpoint has sent multi-hour values, which parked this loop (and froze
# the displayed percent) until the next server restart.
LIMITS_429_BACKOFF_SEC = 600
LIMITS_429_BACKOFF_MAX_SEC = 1800
OAUTH_USAGE_URL = "https://api.anthropic.com/api/oauth/usage"


def fmt_reset_time(iso_ts, with_date):
    # Time only, no timezone suffix — the CYD screen is 320px wide and the
    # reset line is rendered at a large, readable size.
    dt = datetime.fromisoformat(iso_ts.replace("Z", "+00:00")).astimezone()
    t = dt.strftime("%H:%M")
    return f"{dt.strftime('%b')} {dt.day}, {t}" if with_date else t


def oauth_token():
    # Read the Claude Code OAuth token from the macOS Keychain at request
    # time; it is never written to disk or included in responses.
    out = subprocess.run(
        ["/usr/bin/security", "find-generic-password", "-s", "Claude Code-credentials", "-w"],
        capture_output=True, text=True, timeout=10,
    )
    if out.returncode != 0:
        return None
    creds = json.loads(out.stdout).get("claudeAiOauth", {})
    # An expired access token would just 401 (only the CLI can refresh it) —
    # skip the request rather than burn the endpoint's rate limit on it.
    expires_at_ms = creds.get("expiresAt")
    if expires_at_ms and expires_at_ms / 1000 <= time.time():
        return None
    return creds.get("accessToken")


def fetch_limits():
    """The OAuth usage snapshot, or None without a usable token. HTTP errors
    propagate (limits_loop backs off on a 429)."""
    token = oauth_token()
    if not token:
        return None
    doc = fetch_json(OAUTH_USAGE_URL, 10, headers={
        "Authorization": f"Bearer {token}",
        "anthropic-beta": "oauth-2025-04-20",
    })
    return parse_limits(doc)


def parse_limits(doc, now=None):
    """The OAuth usage response -> the limits snapshot kept in STATE, or None
    when it has neither window. `now` (aware, local) is for tests."""
    if not isinstance(doc, dict) or ("five_hour" not in doc and "seven_day" not in doc):
        return None
    session = doc.get("five_hour") or {}
    week = doc.get("seven_day") or {}
    now = now or datetime.now().astimezone()

    def window(win, with_date):
        # resets_at is null between windows (right after a reset, before the
        # next activity opens a new one) — that means 0% used, no countdown.
        # Rejecting it wholesale kept the stale pre-reset snapshot on screen.
        if not win.get("resets_at"):
            return {"percent": round(win.get("utilization") or 0), "resets": ""}
        reset_dt = datetime.fromisoformat(win["resets_at"].replace("Z", "+00:00"))
        return {
            "percent": round(win.get("utilization") or 0),
            "resets": fmt_reset_time(
                win["resets_at"],
                with_date=with_date and reset_dt.astimezone().date() != now.date()),
            # Raw epoch so resets_in_sec can be computed fresh per request
            # even when this snapshot is minutes old.
            "resets_at_epoch": reset_dt.timestamp(),
        }

    # Per-model weekly limit (e.g. "Weekly - Fable") from the limits[] array.
    # May disappear from the response entirely — clients hide the row on null.
    week_model = None
    for lim in (doc.get("limits") or []):
        if lim.get("kind") == "weekly_scoped" and lim.get("percent") is not None:
            scope = lim.get("scope") or {}
            name = ((scope.get("model") or {}).get("display_name")) or ""
            week_model = window(
                {"utilization": lim["percent"], "resets_at": lim.get("resets_at")},
                with_date=True)
            week_model["name"] = name
            break

    # Extra-usage credits ("$0.41 of $10.00"). Prefer the structured `spend`
    # object; fall back to the flatter `extra_usage`. Null when disabled.
    credits = None
    spend = doc.get("spend") or {}
    extra = doc.get("extra_usage") or {}
    if spend.get("enabled") and spend.get("used"):
        exp = spend["used"].get("exponent", 2)
        used = spend["used"].get("amount_minor", 0) / (10 ** exp)
        limit = (spend.get("limit") or {}).get("amount_minor", 0) / (10 ** exp)
        pct = spend.get("percent")
        if pct is None:
            pct = round(used / limit * 100) if limit else 0
        credits = {"used": round(used, 2), "limit": round(limit, 2),
                   "percent": round(pct)}
    elif extra.get("is_enabled") and extra.get("monthly_limit") is not None:
        dp = extra.get("decimal_places", 2)
        used = (extra.get("used_credits") or 0) / (10 ** dp)
        limit = (extra.get("monthly_limit") or 0) / (10 ** dp)
        pct = extra.get("utilization")
        if pct is None:
            pct = used / limit * 100 if limit else 0
        credits = {"used": round(used, 2), "limit": round(limit, 2),
                   "percent": round(pct)}

    return {
        # Raw epoch of this fetch; the handler turns it into age_sec per
        # request so clients can see when the snapshot has gone stale.
        "fetched_at_epoch": now.timestamp(),
        "session": window(session, with_date=False),
        "week": window(week, with_date=True),
        "week_model": week_model,
        "credits": credits,
    }


def limits_interval():
    return LIMITS_INTERVAL_BATTERY_SEC if battery_save_active() else LIMITS_INTERVAL_SEC


def limits_loop():
    was_empty = False
    last_fetch = 0.0

    def body():
        nonlocal was_empty, last_fetch
        now = time.time()
        interval = limits_interval()
        sleep_sec = 5
        if now - last_fetch >= interval:
            try:
                limits = fetch_limits()
                if limits:
                    state_set("limits", limits)
                    last_fetch = now
                    if was_empty:
                        log_err("limits_loop: recovered, snapshot refreshed")
                    was_empty = False
                else:
                    # No usable token or null fields in the response; keep the
                    # last-good snapshot. Log the transition only — this repeats
                    # every cycle until the CLI refreshes the keychain token.
                    if not was_empty:
                        log_err("limits_loop: no snapshot (missing/expired token "
                                "or null fields); keeping last-good")
                    was_empty = True
            except Exception as e:
                # keep the last good snapshot; report shows it as-is
                log_err(f"limits_loop: {type(e).__name__}: {e}")
                if isinstance(e, urllib.error.HTTPError) and e.code == 429:
                    try:
                        retry_after = int(e.headers.get("Retry-After") or 0)
                    except ValueError:
                        retry_after = 0
                    sleep_sec = min(max(LIMITS_429_BACKOFF_SEC, retry_after),
                                    LIMITS_429_BACKOFF_MAX_SEC)
                    log_err(f"limits_loop: backing off {sleep_sec}s "
                            f"(Retry-After: {e.headers.get('Retry-After')!r})")
                    last_fetch = now - interval + sleep_sec
            else:
                sleep_sec = interval
        else:
            sleep_sec = max(1, int(interval - (now - last_fetch)))

        time.sleep(sleep_sec)

    activity_gated_loop(body)
