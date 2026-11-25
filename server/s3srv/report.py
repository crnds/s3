"""GET /api/usage: the document the board and the simulator poll.

It is the contract with src/net.cpp and simulator-s3.html: renaming a field
means editing all three. tests/server/test_report_contract.py pins its key
tree to tests/server/usage_keys.json. New fields are additive and optional.
"""

import time
from datetime import datetime, timezone

from s3srv.power import power_snapshot
from s3srv.state import CLIENT_RETENTION, STATE

ACTIVE_WINDOW_SEC = 180
# Assumed model context size for the "context window" percent — the jsonl
# events don't carry the model's actual limit, so 200K is hardcoded.
CONTEXT_WINDOW_TOKENS = 200000

EMPTY_AGGREGATES = {
    "today": {"tokens": 0, "cost": 0.0},
    "week": {"tokens": 0, "cost": 0.0},
    "projects": [], "trend": [0] * 7,
}


def _limits_for_request(limits, now_epoch):
    # Countdown to the session reset, computed at request time so it stays
    # accurate between OAuth refreshes. Mutate per-request copies only —
    # STATE["limits"] stays pristine with internal epochs.
    if not limits:
        return limits
    limits = dict(limits)
    limits.pop("fetched_at_epoch", None)
    for key in ("session", "week", "week_model"):
        win = limits.get(key) or {}
        if not isinstance(win, dict):
            continue
        win = dict(win)
        epoch = win.pop("resets_at_epoch", None)
        if epoch:
            remaining = int(epoch - now_epoch)
            if remaining <= 0:
                # Window ended; OAuth loop may not have a fresh snapshot
                # yet. Serve reset rather than a stale percent.
                win["percent"] = 0
                win["resets"] = ""
                remaining = 0
            if key in ("session", "week"):
                win["resets_in_sec"] = remaining
        limits[key] = win
    return limits


def build_report(firmware_offer, now=None):
    """A snapshot of STATE, read under the lock; it writes nothing.

    firmware_offer is firmware.firmware_offer()'s result, passed in so this
    stays a pure read (usage_server.build_report() wires the two together).
    Clients older than CLIENT_RETENTION are left out here and pruned from
    STATE by state.note_client() on the next poll."""
    now = now or datetime.now(timezone.utc)
    power = power_snapshot()  # takes the lock itself
    with STATE["lock"]:
        agg = STATE["aggregates"] or EMPTY_AGGREGATES
        ctx = STATE["context"]
        last_ts = STATE["last_ts"]
        limits = STATE["limits"]
        btc, weather, aqi = STATE["btc"], STATE["weather"], STATE["aqi"]
        note = dict(STATE["note"])
        client_snapshot = list(STATE["clients"].items())

    # active_now is archive-only on the board (not drawn); kept for SD CSV.
    active_now = last_ts is not None and (now - last_ts).total_seconds() <= ACTIVE_WINDOW_SEC

    cutoff_client = now - CLIENT_RETENTION
    clients = [
        {"ip": ip, "last_seen_sec": int((now - ts).total_seconds())}
        for ip, ts in sorted(client_snapshot, key=lambda kv: kv[1], reverse=True)
        if ts >= cutoff_client
    ]

    return {
        "generated_at": now.isoformat(),
        "epoch": int(now.timestamp()),
        "today": agg["today"],
        "week": agg["week"],
        "active_now": active_now,
        "projects": agg["projects"],
        "trend": agg["trend"],
        "limits": _limits_for_request(limits, time.time()),
        "context": ({"tokens": ctx["tokens"],
                     "percent": round(ctx["tokens"] / CONTEXT_WINDOW_TOKENS * 100)}
                    if ctx else None),
        "btc": btc,
        "weather": weather,
        "aqi": aqi,
        # Board page 6. Always an object (never null) so an empty "text" reads
        # as "the user cleared the note" rather than "no data yet" — the
        # firmware keeps its previous value for absent keys.
        "note": note,
        "clients": clients,
        "power": power,
        # Published OTA builds per board ({} when none): additive, and the CYD
        # ignores it.
        "firmware": firmware_offer,
    }
