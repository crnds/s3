"""Mac power: pmset battery reads, battery-save (low-power cadences), and
battery_guard_loop, which owns the HTTP socket."""

import subprocess
import time

from s3srv.common import log_err
from s3srv.state import STATE, wake_loops

# The board polls every ~20s nonstop; that's frequent enough that macOS never
# accumulates a long idle gap and just never commits to real system sleep.
# Harmless on AC power, but if this Mac is ever left running unattended on
# battery it silently drains to empty overnight instead of sleeping. Pausing
# (fully closing) the listening socket below this threshold lets idle sleep
# proceed; the board already has a graceful OFFLINE/cat-mode fallback for
# exactly this "server unreachable" case.
BATTERY_LOW_PCT = 50
# 120s: BATTERY_LOW_PCT (50%) is a huge margin, so a 2-min reaction to being
# unplugged is still safe, and this halves the `pmset` subprocess forks.
BATTERY_CHECK_INTERVAL_SEC = 120


_battery_failure_logged = False


def _battery_failed(why):
    # None from battery_status() reads as "desktop Mac, treat as AC", which
    # silently disables the low-battery guard. A desktop is normal (no battery
    # line); pmset itself failing is not, so say so once.
    global _battery_failure_logged
    if not _battery_failure_logged:
        _battery_failure_logged = True
        log_err(f"battery_status: pmset failed ({why}); treating as AC power, "
                "so the low-battery guard is off until it works")
    return None


def battery_status():
    # Returns (percent, on_ac) or None (desktop Macs / no battery report yet /
    # pmset failing, which is logged once). Parsed from `pmset -g batt` text
    # since the stdlib has no battery API; on_ac comes from the first line,
    # percent from the "NN%" in the battery detail line below it.
    try:
        out = subprocess.run(["/usr/bin/pmset", "-g", "batt"],
                              capture_output=True, text=True, timeout=5)
        if out.returncode != 0:
            return _battery_failed(f"exit {out.returncode}")
        lines = out.stdout.splitlines()
        on_ac = "AC Power" in (lines[0] if lines else "")
        for line in lines[1:]:
            idx = line.find("%")
            if idx == -1:
                continue
            start = idx
            while start > 0 and line[start - 1].isdigit():
                start -= 1
            if start == idx:
                continue
            return int(line[start:idx]), on_ac
        return None
    except Exception as e:
        return _battery_failed(f"{type(e).__name__}: {e}")


def power_snapshot():
    # Read-only view of power + effective battery_save for API/status.
    with STATE["lock"]:
        p = dict(STATE["power"])
    on_ac = bool(p.get("on_ac", True))
    manual = p.get("battery_save_manual")  # None | True | False
    if manual is not None:
        battery_save = bool(manual)
        source = "manual"
    else:
        battery_save = not on_ac
        source = "auto"
    return {
        "on_ac": on_ac,
        "percent": p.get("percent"),
        "paused": bool(p.get("paused")),
        "battery_save": battery_save,
        "battery_save_manual": manual,
        "source": source,
    }


def battery_save_active():
    # Low-power cadences (scan/limits/market). True when the Mac is on
    # battery (auto) or when the control panel forced battery-save on.
    return power_snapshot()["battery_save"]


def set_battery_save_manual(enabled):
    # enabled True/False forces battery-save on/off; None clears the
    # override back to automatic (derived from on_ac). Wake loops so
    # intervals change without waiting out a sleep.
    with STATE["lock"]:
        p = dict(STATE["power"])
        p["battery_save_manual"] = enabled
        STATE["power"] = p
    wake_loops()
    log_err("battery_save: manual override %s"
            % ("on" if enabled is True else "off" if enabled is False else "cleared (auto)"))


def set_power_state(on_ac, percent, paused):
    # Preserve battery_save_manual across pmset samples — the control panel
    # owns that field, not the battery guard.
    with STATE["lock"]:
        prev = STATE["power"]
        STATE["power"] = {
            "on_ac": on_ac,
            "percent": percent,
            "paused": paused,
            "battery_save_manual": prev.get("battery_save_manual"),
        }
        changed = (prev.get("on_ac") != on_ac or prev.get("paused") != paused)
    if changed:
        # Wake gated loops so they pick up the new cadence / hard-park promptly
        # instead of waiting out a full idle timeout.
        wake_loops()


def battery_guard_loop(start_server):
    # start_server() binds and starts the HTTP server and returns it (injected
    # by usage_server.py, so this module doesn't import the routes).
    # Owns the HTTP server's lifecycle: fully unbinds the listening socket
    # while on battery below BATTERY_LOW_PCT (so the board sees OFFLINE
    # instead of the Mac's battery hitting 0%), and rebinds once the Mac is
    # back on AC or the battery has recovered. Also publishes on_ac/paused
    # into STATE["power"] so the other loops can throttle before we hard-stop.
    server = start_server()
    paused = False
    while True:
        time.sleep(BATTERY_CHECK_INTERVAL_SEC)
        status = battery_status()
        if status is None:
            # Desktop / unknown: keep serving, treat as AC for cadence.
            set_power_state(True, None, paused)
            continue
        percent, on_ac = status
        should_pause = (not on_ac) and percent <= BATTERY_LOW_PCT
        try:
            if should_pause and not paused:
                log_err(f"battery_guard: {percent}% on battery, pausing HTTP "
                        f"server so macOS can sleep")
                server.shutdown()
                server.server_close()
                server = None
                paused = True
            elif not should_pause and paused:
                log_err(f"battery_guard: recovered ({percent}%, "
                        f"on_ac={on_ac}), resuming HTTP server")
                server = start_server()
                paused = False
            set_power_state(on_ac, percent, paused)
        except Exception as e:
            log_err(f"battery_guard: {type(e).__name__}: {e}")
