#!/usr/bin/env python3
"""The S3 usage server (:8787, launchd com.corner.s3usage): Claude Code token
usage from ~/.claude/projects/*/*.jsonl, plan limits, BTC / weather / AQI, the
note and the OTA offer, served to the board as GET /api/usage.

This file is only the entry point: the code is in the s3srv package beside
it (launchd runs this file by absolute path, so sys.path[0] is server/).
"""

import threading
from datetime import timedelta  # noqa: F401  (read as usage_server.timedelta by the contract test)

from s3srv import report, routes
from s3srv.firmware import firmware_offer
from s3srv.limits import limits_loop
from s3srv.market import market_loop, weather_payload  # noqa: F401  (contract test)
from s3srv.notes import load_note
from s3srv.power import battery_guard_loop
from s3srv.state import CLIENT_RETENTION, STATE  # noqa: F401  (contract test)
from s3srv.usage_scan import compute_aggregates, scan_loop, scan_once


def build_report():
    """GET /api/usage. firmware_offer is looked up here, at call time, so the
    contract test can patch usage_server.firmware_offer."""
    return report.build_report(firmware_offer())


def start_http_server():
    return routes.start_http_server(build_report)


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
