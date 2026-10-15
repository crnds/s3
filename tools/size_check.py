#!/usr/bin/env python3
"""Firmware size gate (roadmap O8; CI runs it after `pio run`).

Fails when firmware.bin is past 75% of the app slot, or has grown more than
TOLERANCE bytes over the committed baseline (tools/size_baseline.json) --
growth nobody planned. When growth is planned (new cats, a new font), update
the baseline in the same commit:

    python3 tools/size_check.py --update

A smaller build always passes; --update records the new floor.
"""
import configparser
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENV = "jc3248w535"
BIN = os.path.join(ROOT, ".pio", "build", ENV, "firmware.bin")
BASELINE = os.path.join(ROOT, "tools", "size_baseline.json")
SLOT_LIMIT = 0.75      # of board_upload.maximum_size (the app slot)
TOLERANCE = 32 * 1024  # unplanned growth allowed over the baseline


def slot_bytes():
    ini = configparser.ConfigParser(inline_comment_prefixes=(";",))
    ini.read(os.path.join(ROOT, "platformio.ini"))
    return int(ini["env:" + ENV]["board_upload.maximum_size"])


def main():
    if not os.path.exists(BIN):
        sys.exit("size_check: %s missing -- run `pio run` first" % BIN)
    size = os.path.getsize(BIN)
    slot = slot_bytes()
    if "--update" in sys.argv:
        with open(BASELINE, "w") as f:
            json.dump({"firmware_bin": size}, f, indent=2)
            f.write("\n")
        print("size_check: baseline set to %d bytes (%.1f%% of the %d-byte slot)" % (size, 100.0 * size / slot, slot))
        return
    with open(BASELINE) as f:
        base = json.load(f)["firmware_bin"]
    pct = 100.0 * size / slot
    print("size_check: firmware.bin %d bytes = %.1f%% of the %d-byte app slot (limit %.0f%%)"
          % (size, pct, slot, SLOT_LIMIT * 100))
    print("size_check: baseline %d bytes, change %+d (allowed +%d)" % (base, size - base, TOLERANCE))
    errors = []
    if size > slot * SLOT_LIMIT:
        errors.append("past %.0f%% of the app slot" % (SLOT_LIMIT * 100))
    if size - base > TOLERANCE:
        errors.append("grew %d bytes over the baseline; if planned, run tools/size_check.py --update" % (size - base))
    if errors:
        sys.exit("size_check: FAIL -- " + "; ".join(errors))
    print("size_check: OK")


if __name__ == "__main__":
    main()
